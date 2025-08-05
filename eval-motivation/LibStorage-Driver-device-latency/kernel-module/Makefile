
obj-m := libstorage_kernel.o
libstorage_kernel-objs := core.o irq.o nvme.o dev_map.o

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
INCLUDES = -I$(CURDIR)/../include/
EXTRA_CFLAGS ?= $(LOGLEVEL)
EXTRA_CFLAGS += $(INCLUDES) -DPROJ_DIR=$(PROJ_DIR) -D__KERNEL__

LIBSCHED ?= 0
ifeq ($(LIBSCHED), 1)
	EXTRA_CFLAGS += -D__LIBSCHED
endif

PWD=$(shell pwd)

$(info Current directory: $(CURDIR) $(EXTRA_CFLAGS) $(PROJ_DIR))
default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) EXTRA_CFLAGS="$(EXTRA_CFLAGS)" modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean