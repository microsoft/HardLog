#!/bin/bash -e

# Go into the deviice kernel folder and build our USB device driver using its Makefile
pushd ../kernel/linux/
  make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- M=../../module/usb-gadget -j$(nproc)
popd

# Once built, you must copy the module into the audit device. One way to do it is using
# SCP (if SSH is enabled on the audit device).
