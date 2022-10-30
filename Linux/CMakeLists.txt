cmake_policy(SET CMP0110 NEW)
cmake_policy(SET CMP0048 NEW)
cmake_policy(SET CMP0077 NEW)
cmake_minimum_required(VERSION 3.10)

project(uvc-viewer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_subdirectory(submodules/ssco)
add_subdirectory(submodules/libuvc)
add_subdirectory(submodules/scope_guard)

option(GLFW_BUILD_EXAMPLES "Build the GLFW example programs" OFF)
option(GLFW_BUILD_TESTS "Build the GLFW test programs" OFF)
option(GLFW_BUILD_DOCS "Build the GLFW documentation" OFF)
option(GLFW_INSTALL "Generate installation target" OFF)
option(GLFW_VULKAN_STATIC "Assume the Vulkan loader is linked with the application" OFF)
add_subdirectory(submodules/glfw)

option(BUILD_EXAMPLE_PROGRAMS OFF)
option(BUILD_TESTS OFF)
option(ENABLE_JACK "Enable JACK backend" OFF)
option(ENABLE_PULSEAUDIO "Enable PulseAudio backend" ON)
option(ENABLE_ALSA "Enable ALSA backend" OFF)
option(ENABLE_COREAUDIO "Enable CoreAudio backend" OFF)
option(ENABLE_WASAPI "Enable WASAPI backend" OFF)
add_subdirectory(submodules/libsoundio)

find_package(PulseAudio)
find_package(OpenGL)

include_directories(include)

# include_directories(/usr/include/opencv4)
# set (CMAKE_EXE_LINKER_FLAGS "-lopencv_highgui -lopencv_core")

add_executable(uvc-viewer src/main.cpp)
target_link_libraries(uvc-viewer
    ssco
    uvc_static
    ${PULSEAUDIO_LIBRARY}
    libsoundio_static
    glfw
    scope_guard
    OpenGL::GL
    )
