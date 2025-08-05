KFS_DIR = kfs
LIBFS_DIR = libfs
FSUTILS_DIR = fsutils

.PHONY: all kfs clean libfs fsutils
all: kfs libfs install

kfs:
	$(MAKE) -C $(KFS_DIR) LOGFLAG="$(LOGFLAG)"

libfs:
	$(MAKE) -C $(LIBFS_DIR) LOGFLAG="$(LOGFLAG)"
	
fsutils:
	$(MAKE) -C $(FSUTILS_DIR) LOGFLAG="$(LOGFLAG)"

install:
	$(MAKE) -C $(LIBFS_DIR) install
	$(MAKE) -C $(KFS_DIR) install

clean:
	$(MAKE) -C $(KFS_DIR) clean
	$(MAKE) -C $(LIBFS_DIR) clean
	$(MAKE) -C $(FSUTILS_DIR) clean