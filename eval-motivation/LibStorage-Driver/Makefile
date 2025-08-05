
KERNEL_MODULE_DIR := kernel-module
USER_LIB_DIR := libstorage
MODULE_NAME := libstorage_kernel
TEST_DIR := apps/test
FIO_DIR := apps/fio-plugin

DEBUG ?= 0
OPTIMIZE = -O2
ifeq ($(DEBUG), 1)
    LOGLEVEL += -DDEBUG -DCURRENT_LOG_LEVEL=LOG_LEVEL_DEBUG
else
	LOGLEVEL += -DDEBUG -DCURRENT_LOG_LEVEL=LOG_LEVEL_INFO
endif

LIBSCHED ?= 0
ifeq ($(LIBSCHED), 1)
	EXTRA_CFLAGS += -D__LIBSCHED
endif

.PHONY: default
default: all

.PHONY: all
all: clean kernel-module user-lib install test fio

.PHONY: clean
clean:
	$(MAKE) -C $(KERNEL_MODULE_DIR) clean
	$(MAKE) -C $(USER_LIB_DIR) clean
	$(MAKE) -C $(TEST_DIR) clean
	$(MAKE) -C $(FIO_DIR) clean
	$(MAKE) unload

.PHONY : install
install:
	$(MAKE) load
	bash scripts/setup.sh
	$(MAKE) -C $(USER_LIB_DIR) install


.PHONY: load
load: kernel-module
	@if sudo lsmod | grep -q $(MODULE_NAME); then \
		echo "$(MODULE_NAME) is already loaded."; \
	else \
		echo "Loading $(MODULE_NAME)..."; \
		objcopy --remove-section .BTF $(KERNEL_MODULE_DIR)/$(MODULE_NAME).ko; \
		sudo insmod $(KERNEL_MODULE_DIR)/$(MODULE_NAME).ko; \
	fi


.PHONY: unload
unload:
	@if sudo lsmod | grep -q $(MODULE_NAME); then \
		echo "Unloading $(MODULE_NAME)..."; \
		sudo rmmod $(MODULE_NAME); \
	else \
		echo "$(MODULE_NAME) is not loaded."; \
	fi

.PHONY: kernel-module
kernel-module:
	LIBSCHED=$(LIBSCHED) $(MAKE) -C $(KERNEL_MODULE_DIR) LOGLEVEL="$(LOGLEVEL)" PROJ_DIR="$(CURDIR)/"

.PHONY: user-lib
user-lib:
	$(MAKE) -C $(USER_LIB_DIR) LOGLEVEL="$(LOGLEVEL)" OPTIMIZE="$(OPTIMIZE)" PROJ_DIR="$(CURDIR)/"

.PHONY: test
test:
	$(MAKE) -C $(TEST_DIR) LOGLEVEL="$(LOGLEVEL)" PROJ_DIR="$(CURDIR)/"

.PHONY: fio
fio:
	$(MAKE) -C $(FIO_DIR) LOGLEVEL="$(LOGLEVEL)" OPTIMIZE="$(OPTIMIZE)"
