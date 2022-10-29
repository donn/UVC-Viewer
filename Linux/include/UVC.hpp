#ifndef _uvc_hpp
#define _uvc_hpp

#include <libuvc/libuvc.h>

#include <stdexcept>

namespace UVC {
    struct Control: uvc_stream_ctrl_t {
        Control() {}

        void printData(FILE *stream) {
            uvc_print_stream_ctrl(this, stream);
        }
    };

    struct Handle {
        uvc_device_handle_t *internal = NULL;
        bool streaming = false;

        Handle(uvc_device_handle_t *ptr): internal(ptr) {}

        ~Handle() {
            if (streaming) {
                uvc_stop_streaming(internal);
            }
            if (internal) {
                uvc_close(internal);
            }
        }

        ///This is a managed RAII resource. this object is not copyable
        Handle(Handle const&) = delete;
        Handle& operator=(Handle const&) = delete;

        void printDiagnostics(FILE *stream) {
            uvc_print_diag(internal, stream);
        }

        Control getControl(uvc_frame_format uff, int width, int height, int fps) {
            Control control;

            auto error = uvc_get_stream_ctrl_format_size(
                internal,
                &control,
                uff,
                width,
                height,
                fps
            );
            
            if (error < 0) {
                throw std::runtime_error(uvc_strerror(error));
            }

            return control;
        }

        void start(Control& control, uvc_frame_callback_t callback, void* userPointer = NULL) {
            auto error = uvc_start_streaming(internal, (uvc_stream_ctrl_t*)&control, callback, userPointer, 0);

            if (error < 0) {
                uvc_perror(error, "start_streaming");
                throw std::runtime_error("Failed to start stream.");
            }

            streaming = true;

        }

        void endStream() {
            streaming = false;
            uvc_stop_streaming(internal);
        }

    };

    struct Device {
        uvc_device_t *internal;

        Device(uvc_device_t *ptr): internal(ptr) {}

        ~Device() {
            if (internal) {
                uvc_unref_device(internal);
            }
        }

        ///This is a managed RAII resource. this object is not copyable
        Device(Device const&) = delete;
        Device& operator=(Device const&) = delete;

        Handle getHandle() {
            uvc_device_handle_t *handle = NULL;

            auto error = uvc_open(internal, &handle);

             if (error < 0) {
                throw std::runtime_error(uvc_strerror(error));
            }

            return Handle(handle);

        }
    };

    struct Context {
        uvc_context_t *internal = NULL;

        Context(libusb_context* libusb_context = NULL) {
            auto error = uvc_init(&internal, libusb_context);

            if (error < 0) {
                uvc_perror(error, "uvc_init");
                throw std::runtime_error("Failed to initialize context.");
            }
        }

        ~Context() {
            if (internal) {
                uvc_exit(internal);
            }
        }

        ///This is a managed RAII resource. this object is not copyable
        Context(Context const&) = delete;
        Context& operator=(Context const&) = delete;

        Device getDevice(int vendorID = 0, int productID = 0, const char* serialNumber = NULL) {
            uvc_device_t *device;

            auto error = uvc_find_device(internal, &device, vendorID, productID, serialNumber);

            if (error < 0) {
                throw std::runtime_error(uvc_strerror(error));
            }

            return Device(device);
        }

    };

};

#endif // _uvc_hpp