CXX_FLAGS= -std=c++17 -fpermissive -I/usr/include/opencv4 -I./SSCO/include -g
LD_FLAGS=-luvc -lusb -lopencv_highgui -lopencv_core -lsoundio -lpthread

uvc: main.cpp
	c++ $(LD_FLAGS) $(CXX_FLAGS) -o $@ $<