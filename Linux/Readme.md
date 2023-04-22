# Linux 
Works entirely in userspace with glfw and libusb.

It could theoretically work on other platforms, but the UVC stack on these platforms actually work, so there really isn't a need to go and compile it for these platforms.

There is an annoying bug if the app exits without cleanup though- see [drawbacks](#drawbacks)

## Permission Setup
Getting access to the UVC device requires `sudo`, but libsoundio will not work with sudo. There is a workaround: you need to add USB permissions to your UVC device as shown in https://wiki.ros.org/libuvc_camera.

# Drawbacks
* An issue occurs sometimes if the App exits abnormally, where the UVC device will not be usable until a reboot.