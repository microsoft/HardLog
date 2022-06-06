#!/bin/bash -e 

#!/bin/bash -e

echo "Building a device kernel compatible with Hardlog!"

# Clean the device folder and make anew
echo "Step-1: Cleaning up the folder for build."
sudo rm -rf device
mkdir device

pushd device

    echo "Step-2: Cloning the Armbian repository."
    git clone https://github.com/armbian/build.git

    # Build a kernel directory
    mkdir kernel

    pushd build
        echo "Step-3: Building the RockPro64 kernel compatible with Hardlog."

        # Build kernel sources natively (only works on Ubuntu systems; tested on Ubuntu 20.04)
        # ./compile.sh BOARD=rockpro64 KERNEL_ONLY=yes BRANCH=legacy RELEASE=buster BUILD_KSRC=yes KERNEL_CONFIGURE=no BUILD_DESKTOP=no USE_TORRENT=no

        # (Alternate) Install using docker. This sometimes does not work, but worked initially.
        ./compile.sh docker BOARD=rockpro64 BRANCH=legacy RELEASE=buster BUILD_DESKTOP=no KERNEL_ONLY=yes KERNEL_CONFIGURE=no BUILD_KSRC=yes USE_TORRENT=no

        # WARNING: This gives permission to all users for a certain drive. Generally not recommended,
        # unless it is a personal (or single-user) machine.
        sudo chown -R `whoami` output/

        pushd output/debs
            echo "Step-4: Uncompressing the kernel sources."

            # Uncompress the Linux source kernel
            ar x linux-source-legacy-rockchip64_22.05.0-trunk_all.deb

            # Uncompress the data folder
            tar -xvf data.tar.xz

            # Uncompress the actual kernel
            tar -xvf usr/src/linux-source-4.4.213-rockchip64.tar.xz -C ../../../kernel/

            # Uncompress the configuration file
            unxz usr/src/linux-rockchip64-legacy_4.4.213_22.05.0-trunk_config.xz

            # Copy the configuration file
            cp usr/src/linux-rockchip64-legacy_4.4.213_22.05.0-trunk_config ../../../kernel/.config
        popd

    popd

    # Go into the kernel folder and build it
    pushd kernel
        echo "Step-5: Building the RockPro64 kernel."

        # Note: make sure that you have installed the correct ARM cross-compiler for
        # the audit device kernel. Most likely if you did not use the docker version 
        # of the armbian installation, it should already be there.
        make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j`nproc`
    popd

popd

echo "Success: The audit device kernel is now built! You can now build the USB Mass Storage Gadget (MSG)."