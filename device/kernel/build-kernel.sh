#!/bin/bash -e

KERNEL_DEB=linux-source-legacy-rockchip64_22.05.1_all.deb
KERNEL_SRC=linux-source-4.4.213-rockchip64.tar.xz
KERNEL_CONFIG_XZ=linux-rockchip64-legacy_4.4.213_22.05.1_config.xz
KERNEL_CONFIG=linux-rockchip64-legacy_4.4.213_22.05.1_config

echo "Building a device kernel compatible with Hardlog!"

echo "Step-1: Downloading the device kernel."
wget https://armbian.hosthatch.com/apt/pool/main/l/linux-source-4.4.213-legacy-rockchip64/${KERNEL_DEB}
dpkg -x ${KERNEL_DEB} .

echo "Step-2: Unzipping the Linux kernel."
mkdir linux
tar xf usr/src/${KERNEL_SRC} -C linux

echo "Step-3: Copying the correct kernel configuration."
unxz usr/src/${KERNEL_CONFIG_XZ}
cp usr/src/${KERNEL_CONFIG} linux/.config

echo "Step-4: Building the device kernel."
pushd linux
    make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j`nproc`
popd

echo "Success: The audit device kernel is now built! You can now build the USB Mass Storage Gadget (MSG)."