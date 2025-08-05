#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include "./include/cmd.h"
#include "./include/libfs-headers.h"
#include "./include/logger.h"

static int dev_fd = 0;

void tfs_cmd_init(void) {
    int fd = 0;

    fd = open(SUFS_DEV_PATH, O_RDWR);

    if (fd == -1) {
        LOG_ERROR("Cannot open %s\n", SUFS_DEV_PATH);
        abort();
    } else {
        dev_fd = fd;
    }
}

void tfs_cmd_fini(void) {
    if (dev_fd > 0) {
        close(dev_fd);
        dev_fd = 0;
    }
}

int tfs_cmd_map_file(int ino, int writable) {
    int ret = 0;
    struct sufs_ioctl_map_entry entry;

    entry.inode = ino;
    entry.perm = writable;

    ret = (int)syscall(SYS_ioctl, dev_fd, SUFS_CMD_MAP, &entry);

    return ret;
}

int tfs_cmd_unmap_file(int ino) {
    struct sufs_ioctl_map_entry entry;

    entry.inode = ino;

    return (int)syscall(SYS_ioctl, dev_fd, SUFS_CMD_UNMAP, &entry);
}

int tfs_cmd_alloc_inodes(unsigned long *ibm_lba, int *inode, int *num,
                         int cpu) {
    int ret = 0;
    struct sufs_ioctl_inode_alloc_entry entry;

    entry.num = (*num);
    entry.cpu = cpu;

    ret = (int)syscall(SYS_ioctl, dev_fd, SUFS_CMD_ALLOC_INODE, &entry);

    (*ibm_lba) = entry.inode_bm_lba;
    (*num) = entry.num;
    (*inode) = entry.inode;

    return ret;
}

int tfs_cmd_free_inodes(int ino, int num) {
    struct sufs_ioctl_inode_alloc_entry entry;

    entry.inode = ino;
    entry.num = num;

    return syscall(SYS_ioctl, dev_fd, SUFS_CMD_FREE_INODE, &entry);
}

int tfs_cmd_alloc_blocks(unsigned long *bbm_lba, unsigned long *st_block,
                         int *num, int cpu) {
    int ret = 0;
    struct sufs_ioctl_block_alloc_entry entry;

    entry.num = (*num);
    entry.cpu = cpu;

    ret = (int)syscall(SYS_ioctl, dev_fd, SUFS_CMD_ALLOC_BLOCK, &entry);

    (*bbm_lba) = entry.bbm_lba;
    (*st_block) = entry.st_block;
    (*num) = entry.num;

    return ret;
}

int tfs_cmd_free_blocks(unsigned long bbm_lba, int num) {
    struct sufs_ioctl_block_alloc_entry entry;

    entry.bbm_lba = bbm_lba;
    entry.num = num;

    return syscall(SYS_ioctl, dev_fd, SUFS_CMD_FREE_BLOCK, &entry);
}