all: device-library

device-library: main.cpp decode.cpp response.cpp device.cpp
	g++ $^ -o $@

clean: 
	rm -rf device-library