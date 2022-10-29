#include <semaphore.h>
#include <unistd.h>

#include <opencv2/core/core_c.h>
#include <opencv2/highgui/highgui_c.h>
#include <ssco.hpp>

#include <csignal>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

#include "SoundIO.hpp"
#include "UVC.hpp"

static sem_t closingSemaphore;
void signalHandler(int signum) { sem_post(&closingSemaphore); }

void video_callback(uvc_frame_t *frame, void *ptr) {
  uvc_frame_t *bgr;
  uvc_error_t ret;
  /* We'll convert the image from YUV/JPEG to BGR, so allocate space */
  bgr = uvc_allocate_frame(frame->width * frame->height * 3);
  if (!bgr) {
    printf("unable to allocate bgr frame!");
    return;
  }
  /* Do the BGR conversion */
  ret = uvc_any2bgr(frame, bgr);
  if (ret) {
    uvc_perror(ret, "uvc_any2bgr");
    uvc_free_frame(bgr);
    return;
  }

  auto cvImg =
      cvCreateImageHeader(cvSize(bgr->width, bgr->height), IPL_DEPTH_8U, 3);

  cvSetData(cvImg, bgr->data, bgr->width * 3);

  cvShowImage("UVC Viewer", cvImg);
  cvWaitKey(10);

  cvReleaseImageHeader(&cvImg);
  uvc_free_frame(bgr);
}

struct SoundIoRingBuffer *ring_buffer = NULL;

static void read_callback(struct SoundIoInStream *instream, int frame_count_min,
                          int frame_count_max) {
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

    if ((err = soundio_instream_begin_read(instream, &areas, &frame_count))) {
      throw std::runtime_error("Read error.");
    }

    if (!frame_count)
      break;

    if (!areas) {
      // Due to an overflow there is a hole. Fill the ring buffer with
      // silence for the size of the hole.
      memset(write_ptr, 0, frame_count * instream->bytes_per_frame);
      fprintf(stderr, "Dropped %d frames due to internal overflow\n",
              frame_count);
    } else {
      for (int frame = 0; frame < frame_count; frame += 1) {
        for (int ch = 0; ch < instream->layout.channel_count; ch += 1) {
          memcpy(write_ptr, areas[ch].ptr, instream->bytes_per_sample);
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

static void write_callback(struct SoundIoOutStream *outstream,
                           int frame_count_min, int frame_count_max) {
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
      if ((err = soundio_outstream_begin_write(outstream, &areas,
                                               &frame_count))) {
        throw std::runtime_error("Begin write error.");
      }
      if (frame_count <= 0)
        return;
      for (int frame = 0; frame < frame_count; frame += 1) {
        for (int ch = 0; ch < outstream->layout.channel_count; ch += 1) {
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
             soundio_outstream_begin_write(outstream, &areas, &frame_count))) {
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

  soundio_ring_buffer_advance_read_ptr(ring_buffer,
                                       read_count * outstream->bytes_per_frame);
}

static void underflow_callback(struct SoundIoOutStream *outstream) {
  static int count = 0;
  fprintf(stderr, "Audio Underflow %d\r", ++count);
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
  sem_init(&closingSemaphore, 0, 0);
  signal(SIGINT, signalHandler);

  using O = SSCO::Option;
  SSCO::Options ssco({
      O{"version", 'v', "Show the current version of this app.", false,
        [&](SSCO::Result &_) {
          std::cout << "0.1.0" << std::endl;
          exit(0);
        }},
      O{"help", 'h', "Show this message and exit.", false,
        [&](SSCO::Result &_) {
          ssco.printHelp(std::cout);
          exit(0);
        }},
      O{"list_sound", std::nullopt, "List sound devices and exit.", false,
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
      O{"diagnostic_data", 'd', "File to store diagnostic data in (optional).",
        true},

      O{"audio_in", 'i', "ID of audio device to use as an input.", true},
      O{"audio_out", 'o', "ID of audio device to use as an output.", true},
      O{"audio_latency", 'l',
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
      audioInDevice, {48000, 44100, 96000, 24000, 0});

  auto format = audioOutDevice.getBestFormat(
      audioOutDevice, {
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
                      });

  std::cerr << "Routing audio from " << audioInDevice.getName() << " to "
            << audioOutDevice.getName() << "..." << std::endl;
  std::cerr << "Running sample rate " << sampleRate << " with format "
            << SoundIO::Context::formatName(format) << "." << std::endl;

  auto instream = audioInDevice.createInStream(format, sampleRate, *layout,
                                               latency, read_callback);
  auto outstream = audioOutDevice.createOutStream(
      format, sampleRate, *layout, latency, write_callback, underflow_callback);

  sioContext.prepareGlobalBuffer(ring_buffer, instream, outstream, latency);

  instream.start();
  outstream.start();
  sioContext.flushEvents();
  // }

  // Video
  // if (video) {
  auto uvcContext = UVC::Context();
  std::cerr << "Searching for video devices..." << std::endl;
  auto uvcDevice = uvcContext.getDevice();
  auto uvcHandle = uvcDevice.getHandle();
  if (diagnosticDataFile.f)
    uvcHandle.printDiagnostics(diagnosticDataFile.f);

  auto control =
      uvcHandle.getControl(UVC_FRAME_FORMAT_YUYV, width, height, fps);
  if (diagnosticDataFile.f)
    control.printData(diagnosticDataFile.f);

  uvcHandle.start(control, video_callback);
  // }

  // Wait on close semaphore
  sem_wait(&closingSemaphore);
}