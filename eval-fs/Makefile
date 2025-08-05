DRIVER_DIR = LibStorage-Driver
FS_DIR = LibStorage-FileSystem
TRUSTED_DIR = LibStorage-Trusted

DEBUG ?= 0
ifeq ($(DEBUG), 1)
	LOGFLAG += -DDEBUG
endif

.PHONY: default
default: all

.PHONY: all
all: driver trusted fs install 

.PHONY: driver
driver:
	# $(MAKE) -C $(DRIVER_DIR) DEBUG="$(DEBUG)"
	$(MAKE) -C $(DRIVER_DIR) 

.PHONY: fs
fs:
	$(MAKE) -C $(FS_DIR) LOGFLAG="$(LOGFLAG)"

.PHONY: trusted
trusted:
	$(MAKE) -C $(TRUSTED_DIR) DEBUG="$(DEBUG)"

.PHONY: clean
clean:
	$(MAKE) -C $(DRIVER_DIR) clean
	$(MAKE) -C $(FS_DIR) clean
	$(MAKE) -C $(TRUSTED_DIR) clean
	rm -rf ./build
	# - sudo rm /usr/lib/libstorage.so
	- sudo rm /usr/lib/libtrusted.so
	- sudo rm /usr/lib/libsufs.so

.PHONY: install
install:
	# $(MAKE) -C $(DRIVER_DIR) install
	# $(MAKE) -C $(FS_DIR) install
	# $(MAKE) -C $(TRUSTED_DIR) install
	$(MAKE) -C $(FS_DIR) fsutils LOGFLAG="$(LOGFLAG)"
	sudo ./$(FS_DIR)/fsutils/init