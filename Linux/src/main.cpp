#include "RingBuffer.hpp"
#include "SoundIO.hpp"
#include "UVC.hpp"

#include <GLFW/glfw3.h>
#include <csignal>
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

RB::Ringbuffer< uvc_frame_t *, 256 > frames;

void video_callback(uvc_frame_t *frame, void *ptr) {
    auto bgr = uvc_allocate_frame(frame->width * frame->height * 3);
    if (!bgr) {
        std::cerr << "Failed to allocate bgr frame" << std::endl;
        return;
    }

    printf(
        "callback! frame_format = %d, width = %d, height = %d, length = %lu, ptr = %p\n",
        frame->frame_format,
        frame->width,
        frame->height,
        frame->data_bytes,
        ptr
    );

    // auto dn = fopen("/dev/null", "w");
    // for (size_t i = 0; i < frame->data_bytes; i += 1) {
    //     fprintf(stdout, "%p\n", ((uint8_t *)frame->data) + i);
    // }

    // if (auto error = uvc_any2bgr(frame, bgr)) {
    //     uvc_perror(error, "uvc_any2bgr");
    //     uvc_free_frame(bgr);
    //     return;
    // }

    auto result = frames.insert(bgr);
    if (!result) {
        std::cerr << "fb overflow" << std::endl;
    }
}

struct SoundIoRingBuffer *ring_buffer = NULL;

static void read_callback(
    struct SoundIoInStream *instream,
    int frame_count_min,
    int frame_count_max
) {
    struct SoundIoChannelArea *areas;
    int err;
    char *write_ptr = soundio_ring_buffer_write_ptr(ring_buffer);
    int free_bytes = soundio_ring_buffer_free_count(ring_buffer);
    int free_count = free_bytes / instream->bytes_per_frame;

    if (frame_count_min > free_count) {
        throw std::runtime_error("Ring buffer overflow");
    }

    int write_frames = std::min(free_count, frame_count_max);
    int frames_left = write_frames;

    for (;;) {
        int frame_count = frames_left;

        if ((err =
                 soundio_instream_begin_read(instream, &areas, &frame_count))) {
            throw std::runtime_error("Read error.");
        }

        if (!frame_count)
            break;

        if (!areas) {
            // Due to an overflow there is a hole. Fill the ring buffer with
            // silence for the size of the hole.
            memset(write_ptr, 0, frame_count * instream->bytes_per_frame);
            fprintf(
                stderr,
                "Dropped %d frames due to internal overflow\n",
                frame_count
            );
        } else {
            for (int frame = 0; frame < frame_count; frame += 1) {
                for (int ch = 0; ch < instream->layout.channel_count; ch += 1) {
                    memcpy(
                        write_ptr,
                        areas[ch].ptr,
                        instream->bytes_per_sample
                    );
                    areas[ch].ptr += areas[ch].step;
                    write_ptr += instream->bytes_per_sample;
                }
            }
        }

        if ((err = soundio_instream_end_read(instream))) {
            throw std::runtime_error("End read error.");
        }

        frames_left -= frame_count;
        if (frames_left <= 0)
            break;
    }

    int advance_bytes = write_frames * instream->bytes_per_frame;
    soundio_ring_buffer_advance_write_ptr(ring_buffer, advance_bytes);
}

static void write_callback(
    struct SoundIoOutStream *outstream,
    int frame_count_min,
    int frame_count_max
) {
    struct SoundIoChannelArea *areas;
    int frames_left;
    int frame_count;
    int err;

    char *read_ptr = soundio_ring_buffer_read_ptr(ring_buffer);
    int fill_bytes = soundio_ring_buffer_fill_count(ring_buffer);
    int fill_count = fill_bytes / outstream->bytes_per_frame;

    if (frame_count_min > fill_count) {
        // Ring buffer does not have enough data, fill with zeroes.
        frames_left = frame_count_min;
        for (;;) {
            frame_count = frames_left;
            if (frame_count <= 0)
                return;
            if ((err = soundio_outstream_begin_write(
                     outstream,
                     &areas,
                     &frame_count
                 ))) {
                throw std::runtime_error("Begin write error.");
            }
            if (frame_count <= 0)
                return;
            for (int frame = 0; frame < frame_count; frame += 1) {
                for (int ch = 0; ch < outstream->layout.channel_count;
                     ch += 1) {
                    memset(areas[ch].ptr, 0, outstream->bytes_per_sample);
                    areas[ch].ptr += areas[ch].step;
                }
            }
            if ((err = soundio_outstream_end_write(outstream))) {
                throw std::runtime_error("End write error.");
            }
            frames_left -= frame_count;
        }
    }

    int read_count = std::min(frame_count_max, fill_count);
    frames_left = read_count;

    while (frames_left > 0) {
        int frame_count = frames_left;

        if ((err =
                 soundio_outstream_begin_write(outstream, &areas, &frame_count)
            )) {
            throw std::runtime_error("Begin write error.");
        }

        if (frame_count <= 0)
            break;

        for (int frame = 0; frame < frame_count; frame += 1) {
            for (int ch = 0; ch < outstream->layout.channel_count; ch += 1) {
                memcpy(areas[ch].ptr, read_ptr, outstream->bytes_per_sample);
                areas[ch].ptr += areas[ch].step;
                read_ptr += outstream->bytes_per_sample;
            }
        }

        if ((err = soundio_outstream_end_write(outstream))) {
            throw std::runtime_error("End write error.");
        }

        frames_left -= frame_count;
    }

    soundio_ring_buffer_advance_read_ptr(
        ring_buffer,
        read_count * outstream->bytes_per_frame
    );
}

static void underflow_callback(struct SoundIoOutStream *outstream) {
    static int count = 0;
    // fprintf(stderr, "Audio Underflow %d\r", ++count);
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
        std::cerr << "Failed to initialize window." << std::endl;
        return EXIT_FAILURE;
    }

    SCOPE_EXIT {
        glfwTerminate();
    };

    signal(SIGINT, signalHandler);

    using O = SSCO::Option;
    SSCO::Options ssco({
        O{"version",
          'v',
          "Show the current version of this app.",
          false,
          [&](SSCO::Result &_) {
              std::cout << "0.1.0" << std::endl;
              exit(0);
          }},
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
              auto sioContext = SoundIO::Context();

              std::cout << "Inputs" << std::endl;
              for (auto i = 0; i < sioContext.inputDeviceCount(); i += 1) {
                  auto device = sioContext.inputDeviceByIndex(i);
                  std::cout << i << ": " << device.getID()
                            << " :: " << device.getName() << std::endl;
              }
              std::cout << "Outputs" << std::endl;
              for (auto i = 0; i < sioContext.outputDeviceCount(); i += 1) {
                  auto device = sioContext.outputDeviceByIndex(i);
                  std::cout << i << ": " << device.getID()
                            << " :: " << device.getName() << std::endl;
              }
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
          "[Default: 0.05s]",
          true},

        O{"video_width", 'w', "Video width. [Default: 1280]", true},
        O{"video_height", 'h', "Video height. [Default: 720]", true},
        O{"video_framerate", 'f', "Video fremerate. [Default: 60]", true},

        // {"no_audio", 'A', "Disable audio loopback.", false },
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

    auto sioContext = SoundIO::Context();
    int audioInIndex = sioContext.defaultInputIndex();
    if (options.find("audio_in") != options.end()) {
        audioInIndex = std::atoi(options["audio_in"].c_str());
    }

    int audioOutIndex = sioContext.defaultOutputIndex();
    if (options.find("audio_out") != options.end()) {
        audioOutIndex = std::atoi(options["audio_out"].c_str());
    }

    auto latency = 0.05;
    if (options.find("audio_latency") != options.end()) {
        latency = std::atof(options["audio_latency"].c_str());
    }

    auto width = 1280;
    if (options.find("video_width") != options.end()) {
        width = std::atoi(options["video_width"].c_str());
    }

    auto height = 720;
    if (options.find("video_height") != options.end()) {
        height = std::atoi(options["video_height"].c_str());
    }

    auto fps = 60;
    if (options.find("fps") != options.end()) {
        fps = std::atoi(options["fps"].c_str());
    }

    auto audio = true;
    if (options.find("no_audio") != options.end()) {
        std::cerr << "Audio loopback disabled." << std::endl;
        audio = false;
    }

    auto video = true;
    if (options.find("no_video") != options.end()) {
        std::cerr << "Video preview disabled." << std::endl;
        video = false;
    }

    // Audio
    // if (audio) {
    auto audioInDevice = sioContext.inputDeviceByIndex(audioInIndex);
    auto audioOutDevice = sioContext.outputDeviceByIndex(audioOutIndex);

    auto layout = audioOutDevice.getBestLayout(audioInDevice);
    auto sampleRate = audioOutDevice.getBestSampleRate(
        audioInDevice,
        {48000, 44100, 96000, 24000, 0}
    );

    auto format = audioOutDevice.getBestFormat(
        audioOutDevice,
        {
            SoundIoFormatFloat32NE, SoundIoFormatFloat32FE,
            SoundIoFormatS32NE,     SoundIoFormatS32FE,
            SoundIoFormatS24NE,     SoundIoFormatS24FE,
            SoundIoFormatS16NE,     SoundIoFormatS16FE,
            SoundIoFormatFloat64NE, SoundIoFormatFloat64FE,
            SoundIoFormatU32NE,     SoundIoFormatU32FE,
            SoundIoFormatU24NE,     SoundIoFormatU24FE,
            SoundIoFormatU16NE,     SoundIoFormatU16FE,
            SoundIoFormatS8,        SoundIoFormatU8,
            SoundIoFormatInvalid,
        }
    );

    std::cerr << "Routing audio from " << audioInDevice.getName() << " to "
              << audioOutDevice.getName() << "..." << std::endl;
    std::cerr << "Running sample rate " << sampleRate << " with format "
              << SoundIO::Context::formatName(format) << "." << std::endl;

    auto instream = audioInDevice.createInStream(
        format,
        sampleRate,
        *layout,
        latency,
        read_callback
    );
    auto outstream = audioOutDevice.createOutStream(
        format,
        sampleRate,
        *layout,
        latency,
        write_callback,
        underflow_callback
    );

    sioContext.prepareGlobalBuffer(ring_buffer, instream, outstream, latency);

    instream.start();
    outstream.start();
    sioContext.flushEvents();
    // }

    // Video
    // if (video) {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); // OpenGL 3.0
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0); // OpenGL 3.0
    auto window = glfwCreateWindow(width, height, "UVC Viewer", NULL, NULL);
    if (!window) {
        std::cerr << "Failed to open a window." << std::endl;
        return EXIT_FAILURE;
    }

    int fbWidth, fbHeight;
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
    glViewport(0, 0, fbWidth, fbHeight);
    std::cout << "Created a window with width " << width << " height " << height
              << " fbWidth " << fbWidth << " fbHeight " << fbHeight
              << std::endl;

    glfwMakeContextCurrent(window);

    auto uvcContext = UVC::Context();
    std::cerr << "Searching for video devices..." << std::endl;
    auto uvcDevice = uvcContext.getDevice();
    std::cerr << "Found video device." << std::endl;
    auto uvcHandle = uvcDevice.getHandle();
    if (diagnosticDataFile.f)
        uvcHandle.printDiagnostics(diagnosticDataFile.f);

    auto control =
        uvcHandle.getControl(UVC_FRAME_FORMAT_BGR, width, height, fps);
    std::cerr << "Control" << std::endl;
    if (diagnosticDataFile.f)
        control.printData(diagnosticDataFile.f);

    uvcHandle.start(control, video_callback);

    std::cerr << "Streaming..." << std::endl;
    std::cerr << std::flush;
    uvc_frame_t *frame;

    const uint8_t oneredpixel[] = {0, 0, 255};

    while (!glfwWindowShouldClose(window)) {
        if (auto popped = frames.remove(&frame)) {
            fprintf(stderr, "got frame\n");
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            uint8_t *frameBuffer = (uint8_t *)frame->data;
            std::cerr << frameBuffer[30] << std::endl;

            glDrawPixels(1, 1, GL_BGR, GL_UNSIGNED_BYTE, oneredpixel);
            glDrawPixels(100, 100, GL_BGR, GL_UNSIGNED_BYTE, frame->data);
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

    // }
}