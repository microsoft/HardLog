all: host-library

host-library: main.cpp device.cpp
	g++ $^ -o $@

clean: 
	rm -rf host-library