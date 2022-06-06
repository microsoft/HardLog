#!/bin/bash

# Re-enable core #8
sudo echo 1 > /sys/devices/system/cpu/cpu8/online

# simply remove the Hardlog kernel module
sudo rmmod hl.ko