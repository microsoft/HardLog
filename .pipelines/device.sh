#!/bin/bash -e
# Copyright (c) 2022 Microsoft Corporation.
# Licensed under the MIT License.

set -x
set -e

KERNEL_DEB=linux-source-legacy-rockchip64_22.02.1_all.deb
KERNEL_SRC=linux-source-4.4.213-rockchip64.tar.xz

pushd device

  # Kernel build testing
  pushd kernel
    wget -q https://armbian.hosthatch.com/apt/pool/main/l/linux-source-4.4.213-legacy-rockchip64/${KERNEL_DEB}
    dpkg -x ${KERNEL_DEB} .
    tar xf usr/src/${KERNEL_SRC} -C linux

    pushd linux
      patch -s -p1 < ../../module/linux-4.4.213-usb-gadget.diff
      ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- make prepare
      ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- make scripts
      ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- make M=../../module -j$(nproc)
    popd
  popd

  # Device library testing
  pushd library
    ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- make
  popd

  # Module testing requires installing the kernel and copying the USB MSG, so let's ignore it for the pipeline
popd
