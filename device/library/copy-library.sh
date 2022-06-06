#!/bin/bash -e

make CC=aarch64-linux-gnu-g++

scp ./*.cpp rockpro:~/library/
scp ./*.h rockpro:~/library/
scp ./Makefile rockpro:~/library/
