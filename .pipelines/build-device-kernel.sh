#!/bin/bash -e
set -x

# Armbian build
# KERNEL_DEB=linux-source-legacy-rockchip64_22.05.0-trunk_all.deb 
# KERNEL_CFG=linux-rockchip64-legacy_4.4.213_22.05.0-trunk_config

KERNEL_DEB=linux-source-legacy-rockchip64_22.02.1_all.deb
KERNEL_SRC=linux-source-4.4.213-rockchip64.tar.xz

mkdir -p device/kernel
pushd device
# Armbian kernel build takes too long, so we cannot use Azure Pipeline (default: 60 min timoutout)
#   git clone https://github.com/armbian/build.git
#   pushd build
#     ./compile.sh BOARD=rockpro64 KERNEL_ONLY=yes BRANCH=legacy RELEASE=buster BUILD_KSRC=yes KERNEL_CONFIGURE=no BUILD_DESKTOP=no USE_TORRENT=no
#     sudo chown -R `whoami` output/
#     pushd output/debs
#       dpkg -x ${KERNEL_DEB} .
#       tar xvf usr/src/${KERNEL_SRC} -C ../../../kernel/
#       unxz -c usr/src/${KERNEL_CFG}.xz > ../../../kernel/.config
#     popd
#   popd

  wget -q https://armbian.hosthatch.com/apt/pool/main/l/linux-source-4.4.213-legacy-rockchip64/${KERNEL_DEB}
  dpkg -x ${KERNEL_DEB} .
  cp usr/src/${KERNEL_SRC} .
  tar xf ${KERNEL_SRC} -C kernel

  pushd kernel
    ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- make -j$(nproc)
    ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- make M=../module -j$(nproc)
  popd

  pushd library
    ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- make
  popd
popd
