#!/bin/bash -e

# Copy the native USB gadget folder here for patching
cp -r ../kernel/linux/drivers/usb/gadget usb-gadget

# Apply the patch on the USB gadget folder
patch -s -p0 < linux-4.4.213-usb-gadget.diff