#!/bin/bash -e

# Clean the folder
pushd ../kernel/linux/
  make clean ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- M=../../module/usb-gadget -j12
popd