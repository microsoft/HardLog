# Setting Up a Host Machine for Hardlog

This folder contains scripts to compile/install a host Linux kernel (patched with Hardlog),
the Hardlog host kernel logging module, and the Hardlog library to retrieve logs from the
audit device.

## 1. Compile/Install the Linux Kernel

- Go to the kernel folder.

- Run`build.sh` to compile the kernel. 

- Once compiled, install the kernel using `install.sh`.

The scripts contains all steps to download a kernel, patch it with Hardlog's changes, build the kernel, and install it on your machine. 
If you want to understand the steps, please refer to the following link on [kernel source building/installation](https://www.cyberciti.biz/tips/compiling-linux-kernel-26.html).

*Note:* Currently, the script builds a non-preemptible Linux kernel (i.e., with CONFIG_PREEMPT=n). It can be modified to build a pre-emptible Linux kernel by replacing the kernel configuration script from `config` to `config.preempt`.

## 2. Compile/Install the Hardlog Kernel Module

Once a Hardlog-compatible kernel is running, compile and install the Hardlog kernel module.

- Run `install.sh` to install the module

    - The install script assumes you will provide the audit device's `/dev/sdX` entry. For instance, if the
    audit device is installed at `/dev/sdb`, run `./install.sh sdb`.

## 3. (Optional) Install the Host Library to Retrieve Logs

Compile the host library (in the library folder) using its Makefile, and run the `host-library` binary. You can then send commands to the audit device to retrieve logs, assuming the device library (in /device/library/) is also installed and running on the audit device. In particular, you can send the command "ALL" to retrieve all the logs, or command "FILTER XYZ" to filter logs.