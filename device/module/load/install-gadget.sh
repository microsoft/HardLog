#/bin/bash -e

# remove the existing kernel modules, if any
sudo rmmod g_mass_storage || true
sudo rmmod usb_f_mass_storage || true
sudo rmmod libcomposite || true

# clear files; this assumes that the device contains an NVMe drive which is 
# mounted to the /mnt/nvme folder. It will create three files to get logs
# from host machine (host-data.bin), server commands (server-commands.bin), 
# and send back server response (server-data.bin).
sudo dd if=/dev/zero of=/mnt/nvme/host-data.bin bs=500M count=100 oflag=direct
sudo dd if=/dev/zero of=/mnt/nvme/server-commands.bin bs=500M count=1 oflag=direct
sudo dd if=/dev/zero of=/mnt/nvme/server-data.bin bs=500M count=10 oflag=direct

# Install the new (patched) kernel modules. Please make sure that you have SCP-ed
# the correct kernel modules to this folder on the device.
sudo insmod -f libcomposite.ko
sudo insmod -f usb_f_mass_storage.ko
sudo insmod -f g_mass_storage.ko luns=3 file=/mnt/nvme/host-data.bin,/mnt/nvme/server-commands.bin,/mnt/nvme/server-data.bin stall=0 nofua=1