#!/bin/bash -e

# Make sure there are no rules for auditctl
sudo auditctl -e 0

# Build the module
make clean && make

# Remove if exists
sudo rmmod hl || true

# Put this particular CPU thread offline because I pin the hardlog
# thread to CPU thread #2 in hl-main.c
sudo echo 0 > /sys/devices/system/cpu/cpu8/online

# Insert the module with the specified usb device
# biof = 1       --> Use Block IO (BIO) instead of VFS (BIO is faster!)
# microlog = 1   --> Use micro-logs instead of full logs (testing purposes only)
# vmallocf = 1   --> Use vmalloc instead of kmalloc for buffer allocations
# flushtimef = 1 --> Measure the flushing time (benchmarking purposes only)
sudo insmod hl.ko device=/dev/$1 biof=1 microlog=0 vmallocf=0 flushtimef=1