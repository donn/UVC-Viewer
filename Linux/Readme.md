# Linux 
Works entirely in userspace with libusb, libuvc and libsoundio.

It could theoretically work on other platforms, but the UVC stack on these platforms actually work, so there really isn't a need to go and compile it for these platforms.

I don't like this one.

## Permission Setup
Getting access to the UVC device requires `sudo`, but libsoundio will not work with sudo. There is a workaround: you need to add USB permissions to your UVC device as shown in https://wiki.ros.org/libuvc_camera.

# Drawbacks
* No options to pick the sound backend
* No options to pick the UVC device being used
* Non-OpenCV Output
    * I kept OpenCV from the tutorial to be expedient, but it's not really the best approach for something so simple.
    * Currently, the only way to exit and keep zombie handles from forming is Ctrl+C