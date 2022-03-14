#ifndef _soundio_hpp
#define _soundio_hpp

#include <vector>
#include <thread>
#include <cstring> // memset
#include <stdexcept>

#include <soundio/soundio.h>

namespace SoundIO {
    typedef SoundIoChannelLayout Layout;
    typedef SoundIoFormat Format;
    typedef SoundIoRingBuffer GlobalRingBuffer;

    typedef void (*ReadCallback)(struct SoundIoInStream *, int frame_count_min, int frame_count_max);
    typedef void (*WriteCallback)(struct SoundIoOutStream *, int frame_count_min, int frame_count_max);
    typedef void (*UnderflowCallback)(struct SoundIoOutStream *);

    struct InStream {
        SoundIoInStream *internal = NULL;

        InStream(SoundIoInStream *ptr, Format format, int sampleRate, Layout layout, double latency, ReadCallback readCallback): internal(ptr) {
            internal->format = format;
            internal->sample_rate = sampleRate;
            internal->layout = layout;
            internal->software_latency = latency;
            internal->read_callback = readCallback;

            auto error = soundio_instream_open(internal);
            if (error) {
                throw std::runtime_error("Failed to open instream");
            }
        }

        ~InStream() {
            if (internal) {
                soundio_instream_destroy(internal);
            }
        }

        ///This is a managed RAII resource. this object is not copyable
        InStream(InStream const&) = delete;
        InStream& operator=(InStream const&) = delete;

        int getSampleRate() {
            return internal->sample_rate;
        }

        int getBytesPerFrame() {
            return internal->bytes_per_frame;
        }

        void start() {
            auto error = soundio_instream_start(internal);
            if (error) {
                throw std::runtime_error("Couldn't start input stream.");
            }
        }
    };

    struct OutStream {
        SoundIoOutStream *internal = NULL;

        OutStream(SoundIoOutStream *ptr, Format format, int sampleRate, Layout layout, double latency, WriteCallback writeCallback, UnderflowCallback underflowCallback): internal(ptr) {
            internal->format = format;
            internal->sample_rate = sampleRate;
            internal->layout = layout;
            internal->software_latency = latency;
            internal->write_callback = writeCallback;
            internal->underflow_callback = underflowCallback;

            auto error = soundio_outstream_open(internal);
            if (error) {
                throw std::runtime_error("Failed to open outstream");
            }
        }

        ~OutStream() {
            if (internal) {
                soundio_outstream_destroy(internal);
            }
        }

        ///This is a managed RAII resource. this object is not copyable
        OutStream(OutStream const&) = delete;
        OutStream& operator=(OutStream const&) = delete;

        int getSampleRate() {
            return internal->sample_rate;
        }

        int getBytesPerFrame() {
            return internal->bytes_per_frame;
        }

        void start() {
            auto error = soundio_outstream_start(internal);
            if (error) {
                throw std::runtime_error("Couldn't start input stream.");
            }
        }
    };

    struct Device {
        SoundIoDevice *internal = NULL;
        
        Device(SoundIoDevice *ptr): internal(ptr) {
            soundio_device_sort_channel_layouts(internal);
        }

        ~Device() {
            if (internal) {
                soundio_device_unref(internal);
            }
        };

        ///This is a managed RAII resource. this object is not copyable
        Device(Device const&) = delete;
        Device& operator=(Device const&) = delete;

        std::string getID() {
            return std::string(internal->id);
        }

        std::string getName() {
            return std::string(internal->name);
        }

        bool supportsSampleRate(int sampleRate) {
            return soundio_device_supports_sample_rate(internal, sampleRate);
        }

        bool supportsFormat(Format format) {
            return soundio_device_supports_format(internal, format);
        }

        const Layout* getBestLayout(Device &matchingDevice) {
            auto layout = soundio_best_matching_channel_layout(
                internal->layouts, internal->layout_count,
                matchingDevice.internal->layouts, matchingDevice.internal->layout_count
            );
            if (!layout) {
                throw std::runtime_error("No matching layout found between the two devices.");
            }
            return layout;
        }

        int getBestSampleRate(Device &matchingDevice, std::vector<int> sampleRates) {
            for (auto rate: sampleRates) {
                if (supportsSampleRate(rate) && matchingDevice.supportsSampleRate(rate)) {
                    return rate;
                }
            }
            return 0;
        }

        Format getBestFormat(Device &matchingDevice, std::vector<Format> formats) {
            for (auto format: formats) {
                if (supportsFormat(format) && matchingDevice.supportsFormat(format)) {
                    return format;
                }
            }
            return SoundIoFormatInvalid;
        }

        InStream createInStream(Format format, int sampleRate, Layout layout, double latency, ReadCallback readCallback) {
            auto instream = soundio_instream_create(internal);
            if (!instream) {
                throw std::runtime_error("Failed to create instream.");
            }
            return InStream(instream, format, sampleRate, layout, latency, readCallback);
        }

        OutStream createOutStream(Format format, int sampleRate, Layout layout, double latency, WriteCallback writeCallback, UnderflowCallback underflowCallback) {
            auto outstream = soundio_outstream_create(internal);
            if (!outstream) {
                throw std::runtime_error("Failed to create outstream.");
            }
            return OutStream(outstream, format, sampleRate, layout, latency, writeCallback, underflowCallback);
        }


    };
    
    struct Context {
        SoundIo *internal = NULL;
        double latency;

        Context() {
            internal = soundio_create();
            if (internal == NULL) {
                throw std::runtime_error("Failed to create SoundIO context.");
            }
            auto err = soundio_connect(internal);
            if (err) {
                throw std::runtime_error("Failed to connect to SoundIO backend.");
            }

            soundio_flush_events(internal);
        }

        ~Context() {
            if (internal) {
                soundio_destroy(internal);
            }
        }

        ///This is a managed RAII resource. this object is not copyable
        Context(Context const&) = delete;
        Context& operator=(Context const&) = delete;

        int inputDeviceCount() {
            auto count = soundio_input_device_count(internal);
            if (count < 0) {
                throw std::runtime_error("Failed to get input device count.");
            }
            return count;
        }

        int outputDeviceCount() {
            auto count = soundio_output_device_count(internal);
            if (count < 0) {
                throw std::runtime_error("Failed to get output device count.");
            }
            return count;
        }

        int defaultInputIndex() {
            auto index = soundio_default_input_device_index(internal);
            if (index < 0) {
                throw std::runtime_error("Failed to get a default input- no inputs?");
            }
            return index;
        }

        int defaultOutputIndex() {
            auto index = soundio_default_output_device_index(internal);
            if (index < 0) {
                throw std::runtime_error("Failed to get a default output- no outputs?");
            }
            return index;
        }

        Device inputDeviceByIndex(int index) {
            return Device(soundio_get_input_device(internal, index));

        }

        Device outputDeviceByIndex(int index) {
            return Device(soundio_get_output_device(internal, index));
        }

        void prepareGlobalBuffer(GlobalRingBuffer* &buffer, InStream &in, OutStream& out, double latency) {
            int capacity = 2 * latency * in.getSampleRate() * in.getBytesPerFrame();

            buffer = soundio_ring_buffer_create(internal, capacity);

            if (!buffer) {
                throw std::runtime_error("Failed to create buffer.");
            }

            int fill_count = latency * out.getSampleRate() * out.getBytesPerFrame();

            auto buf = soundio_ring_buffer_write_ptr(buffer);
            
            memset(buf, 0, fill_count);

            soundio_ring_buffer_advance_write_ptr(buffer, fill_count);
        }

        void flushEvents() {
            soundio_flush_events(internal);
        }

        void waitForEvent() {
            soundio_wait_events(internal);
        }

        void wakeup() {
            soundio_wakeup(internal);
        }

        static std::string formatName(Format format) {
            return soundio_format_string(format);
        }
    };
}

#endif // _soundio_hpp