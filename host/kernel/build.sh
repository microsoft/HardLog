#!/bin/bash -e

echo "Building a host kernel compatible with Hardlog"

rm -rf linux-5.4.92.tar.gz*

echo "Step 1: Downloading the kernel"

wget https://cdn.kernel.org/pub/linux/kernel/v5.x/linux-5.4.92.tar.gz

echo "Step 2: Decompressing the downloaded kernel"

tar -xvf linux-5.4.92.tar.gz

echo "Step 3: Patching the native kernel"

cp linux-5.4.92-host.diff linux
cp config linux-5.4.92/.config

# Patch assumes a certain name of the folder
mv -f linux-5.4.92 desktop-hardlog
patch -s -p0 < linux-5.4.92-host.diff

echo "Step 4: Building the kernel"

pushd desktop-hardlog
    make -j`nproc`
popd

echo "[*] Successfully built the host kernel"