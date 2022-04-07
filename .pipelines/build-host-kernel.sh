#!/bin/bash -e
# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

BASE_KERNEL=linux-5.4.92

mkdir -p host
pushd host
  wget -q https://cdn.kernel.org/pub/linux/kernel/v5.x/${BASE_KERNEL}.tar.xz
  tar xf ${BASE_KERNEL}.tar.xz
  mv ${BASE_KERNEL} kernel

  pushd kernel
    patch -s -p1 < ../hardlog-kernel.patch
    make defconfig
    make -j$(nproc)
    make M=../module -j$(nproc)
  popd

  pushd library
    make
  popd
popd
