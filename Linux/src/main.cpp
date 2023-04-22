#include "RingBuffer.hpp"
#include "UVC.hpp"

#include <GLFW/glfw3.h>
#include <csignal>
#include <fmt/core.h>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <scope_guard.hpp>
#include <semaphore.h>
#include <ssco.hpp>
#include <stdexcept>
#include <thread>
#include <unistd.h>

bool shouldClose = false;
void signalHandler(int signum) {
    shouldClose = true;
}

const timespec ONE_SECOND{1, 0};

RB::Ringbuffer< uvc_frame_t *, 256 > frames;

sem_t frameSemaphore;
void video_callback(uvc_frame_t *frame, void *ptr) {
    uvc_frame_t *copy = uvc_allocate_frame(frame->width * frame->height * 3);
    uvc_duplicate_frame(frame, copy);

    auto result = frames.insert(copy);
    if (!result) {
        fmt::print(
            stderr,
            "A framebuffer overflow has occurred.\n"
            "What this likely means is that your computer is way too slow.\n"
        );
    }
    sem_post(&frameSemaphore);
}

void window_size_callback(GLFWwindow *window, int width, int height) {
    fmt::print("Window resized to {}x{}.\n", width, height);
}

struct RAIIFile {
        FILE *f = NULL;

        ~RAIIFile() {
            if (f) {
                fclose(f);
            }
        }
};

int main(int argc, char **argv) {
    int status = 0;

    if (!glfwInit()) {
        // Initialization failed
        fmt::print(stderr, "Failed to initialize window.\n");
        return EXIT_FAILURE;
    }

    SCOPE_EXIT {
        glfwTerminate();
    };

    signal(SIGINT, signalHandler);

    using O = SSCO::Option;
    SSCO::Options ssco({
        O{"help",
          'h',
          "Show this message and exit.",
          false,
          [&](SSCO::Result &_) {
              ssco.printHelp(std::cout);
              exit(0);
          }},
        O{"list_sound",
          std::nullopt,
          "List sound devices and exit.",
          false,
          [&](SSCO::Result &_) {
              fmt::print(stderr, "Audio support not currently available.");
              exit(0);
          }},
        O{"diagnostic_data",
          'd',
          "File to store diagnostic data in (optional).",
          true},

        O{"audio_in", 'i', "ID of audio device to use as an input.", true},
        O{"audio_out", 'o', "ID of audio device to use as an output.", true},
        O{"audio_latency",
          'l',
          "Floating point value determining software audio latency in seconds. "
          "[Default: 0.1s]",
          true},

        O{"video_width", 'w', "Video width. [Default: 1920]", true},
        O{"video_height", 'h', "Video height. [Default: 1080]", true},
        O{"video_framerate", 'f', "Video framerate. [Default: 60]", true},

        {"no_audio", 'A', "Disable audio loopback.", false},
        // {"no_video", 'V', "Disable video loopback.", false }
    });

    auto opts = ssco.process(argc, argv);

    if (!opts.has_value()) {
        ssco.printHelp(std::cout);
        return 64;
    }

    auto options = opts.value().options;

    // Process CLI Options
    RAIIFile diagnosticDataFile;
    if (options.find("diagonstic_data") != options.end()) {
        diagnosticDataFile.f = fopen(options["diagonstic_data"].c_str(), "w");
    }

    int audioInIndex = -1;
    if (options.find("audio_in") != options.end()) {
        audioInIndex = std::atoi(options["audio_in"].c_str());
    }

    int audioOutIndex = -1;
    if (options.find("audio_out") != options.end()) {
        audioOutIndex = std::atoi(options["audio_out"].c_str());
    }

    auto latency = 0.1;
    if (options.find("audio_latency") != options.end()) {
        latency = std::atof(options["audio_latency"].c_str());
    }

    auto width = 1920;
    if (options.find("video_width") != options.end()) {
        width = std::atoi(options["video_width"].c_str());
    }

    auto height = 1080;
    if (options.find("video_height") != options.end()) {
        height = std::atoi(options["video_height"].c_str());
    }

    auto fps = 60;
    if (options.find("fps") != options.end()) {
        fps = std::atoi(options["fps"].c_str());
    }

    auto audio = true;
    if (options.find("no_audio") != options.end()) {
        fmt::print(stderr, "Audio loopback disabled.\n");
        audio = false;
    }

    auto video = true;
    if (options.find("no_video") != options.end()) {
        fmt::print(stderr, "Video preview disabled.\n");
        video = false;
    }

    // Audio
    if (audio) {
        fmt::print(stderr, "Audio support not currently available.");
    }

    // Video
    // if (video) {
    auto window = glfwCreateWindow(width, height, "UVC Viewer", NULL, NULL);
    if (!window) {
        fmt::print(stderr, "Failed to open a window.\n");
        return EXIT_FAILURE;
    }

    int fbWidth, fbHeight;
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
    glViewport(0, 0, fbWidth, fbHeight);
    fmt::print(
        stderr,
        "Created a window of size {}x{} (with framebuffer {}x{}).\n",
        width,
        height,
        fbWidth,
        fbHeight
    );

    glfwSetWindowSizeCallback(window, window_size_callback);
    glfwMakeContextCurrent(window);

    auto uvcContext = UVC::Context();
    fmt::print(stderr, "Searching for video devices...\n");
    auto uvcDevice = uvcContext.getDevice();
    fmt::print(stderr, "Found video device.\n");
    auto uvcHandle = uvcDevice.getHandle();
    if (diagnosticDataFile.f)
        uvcHandle.printDiagnostics(diagnosticDataFile.f);

    auto control =
        uvcHandle.getControl(UVC_FRAME_FORMAT_BGR, width, height, fps);
    if (diagnosticDataFile.f)
        control.printData(diagnosticDataFile.f);

    sem_init(&frameSemaphore, 0, 0);

    fmt::print(
        stderr,
        "Streaming...\nPress Ctrl+C or close the window to exit.\n"
    );

    uvcHandle.start(control, video_callback);

    uvc_frame_t *frame;
    while (!glfwWindowShouldClose(window)) {
        sem_timedwait(&frameSemaphore, &ONE_SECOND);
        if (auto popped = frames.remove(&frame)) {
            glClearColor(1.f, 1.f, 1.f, 1.f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            uint8_t *frameBuffer = (uint8_t *)frame->data;
            glDrawPixels(width, height, GL_BGR, GL_UNSIGNED_BYTE, frameBuffer);
            glfwSwapBuffers(window);
            uvc_free_frame(frame);
        } else {
            // std::cerr << "Underflow" << std::endl;
        }
        if (shouldClose) {
            glfwSetWindowShouldClose(window, 1);
        }
        glfwPollEvents();
    }
}