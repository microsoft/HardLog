#!/bin/bash -e

# Get the pre-built image from the armbian site
wget https://armbian.hosthatch.com/archive/rockpro64/archive/Armbian_21.05.1_Rockpro64_buster_legacy_4.4.213.img.xz

# Untar the image
tar -xvf Armbian_21.05.1_Rockpro64_buster_legacy_4.4.213.img.xz

# Install this image into your SD card using the following command
# sudo dd if=Armbian_21.05.1_Rockpro64_buster_legacy_4.4.213.img of=/dev/sdb oflag=direct