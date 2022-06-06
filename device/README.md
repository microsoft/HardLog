# Setting Up a Audit Device for Hardlog

This folder contains scripts to compile/install a device Armbian kernel, the Hardlog device kernel logging module, and the Hardlog library to communicate with the host machine.

## 1. Compile/Install the Linux Kernel

- Go to the kernel folder.

- Run `build-image.sh` to get the Armbian kernel image. 

- Once downloaded, install the Armbian kernel image to an SD card. Some steps are shown in `build-image.sh`

- Next, compile the linux kernel source (needed for building the USB gadget in the next section). There are two scripts that can compile the kernel from source. 

    - `build-kernel.sh` : This script uses the pre-built .deb images of the Armbian kernel to extract linux from source. It should work (as long as the pre-built images are correctly hosted). This should be your first go-to script.

    - `build-kernel-alt.sh` : This script uses the GitHub repository of Armbian and creates the .deb file and linux source from scratch. This should be used as a last resort since it takes a long time, and it works only on Ubuntu systems.

## 2. Compile/Install the Hardlog Kernel Module

Once a Hardlog-compatible kernel is running on the audit device, compile and install the Hardlog kernel module.

- Go to the module folder.

- Run `prep.sh` to copy a patched (Hardlog) version of the USB mass storage gadget.

- Build the module using `build.sh`.

- Afterwards, copy the built modules into the device. For instance, you can use SCP commands. 

    - You will need to install three modules---usb_f_mass_storage.ko, libcomposite.ko, and g_mass_storage.ko. The scripts to load these modules on the device is provided in `/device/module/load`.

## 3. (Optional) Install the Device Library to Retrieve Logs

Copy the `library` folder into the audit device. Compile it using its Makefile, and run the `device-library` binary. The program will automatically wait for commands from the host machine (sent by the library in the `host` folder).