hl-y := hl-main.o hl-bio-fs.o hl-logger.o hl-ioctl.o hl-request.o hl-response.o hl-measure.o
obj-m += hl.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

install:
	sudo insmod hl.ko

remove:
	sudo rmmod hl
