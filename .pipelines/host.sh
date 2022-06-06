#!/bin/bash -e
# Copyright (c) 2022 Microsoft Corporation.
# Licensed under the MIT License.

BASE_KERNEL=linux-5.4.92

pushd host

  pushd kernel 
    wget -q https://cdn.kernel.org/pub/linux/kernel/v5.x/${BASE_KERNEL}.tar.xz
    tar xf ${BASE_KERNEL}.tar.xz

    # patch the kernel
    mv ${BASE_KERNEL} desktop-hardlog
    patch -s -p1 < linux-5.4.92-host.diff

    pushd desktop-hardlog
      # build using default config
      make defconfig
      make prepare
      make scripts

      # build HardLog files only
      make kernel/audit.o
      make kernel/auditsc.o
      make kernel/hardlog.o
      make M=../module -j$(nproc)
    popd
  popd

  pushd library
    make
  popd
popd
