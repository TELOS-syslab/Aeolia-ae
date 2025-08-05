KDIR ?= /lib/modules/$(shell uname -r)/build
obj-m += sufs.o

sufs-y := balloc.o device.o inode.o lease.o \
	mmap.o rbtree.o spinlock.o super.o dev_nvme.o\
             
ccflags-y := -std=gnu99 -Wno-declaration-after-statement -O3 -mtune=native 

.PHONY: all
all:
	make -C $(KDIR) M=`pwd` EXTRA_CFLAGS="$(LOGFLAG)" 
	objcopy --remove-section .BTF sufs.ko

.PHONY: clean
clean:
	make -C $(KDIR) M=`pwd` clean
	- sudo rmmod sufs
	
.PHONY: install
install:
	- sudo rmmod sufs
	sudo insmod sufs.ko
