#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include "../../LibStorage-Trusted/include/trusted-api.h"
#include "../include/ioctl.h"
#include "../include/libfs_config.h"
#include "cmd.h"
#include "orig_syscall.h"

int sufs_libfs_cmd_map_file(int ino, int writable,
                            unsigned long *index_offset) {
    int ret = 0;
    struct tfs_map_arg entry;

    entry.inode = ino;
    entry.perm = writable;
#if 0
    printf("ino is %d\n", ino);
#endif

    ret = tfs_mmap_file((unsigned long)&entry);

    if (ret == 0)
        (*index_offset) = entry.index_offset;

    return ret;
}

int sufs_libfs_cmd_unmap_file(int ino, int pino) {
    struct tfs_map_arg entry;

    entry.inode = ino;

    return tfs_unmap_file((unsigned long)&entry);
}

int sufs_libfs_cmd_chown(int inode, int uid, int gid) {
    struct tfs_chown_arg entry;

    entry.inode = inode;
    entry.owner = uid;
    entry.group = gid;

    return tfs_chown((unsigned long)&entry);
}

int sufs_libfs_cmd_chmod(int inode, unsigned int mode) {
    struct tfs_chmod_arg entry;

    entry.inode = inode;
    entry.mode = mode;

    return tfs_chmod((unsigned long)&entry);
}

int sufs_libfs_cmd_read_fidx_page(int inode, unsigned long lba, void *buf) {
    struct tfs_fidx_page_entry entry;
    int ret = 0;

    entry.inode = inode;
    entry.lba = lba;
    entry.page_buffer = buf;
    ret = (int)tfs_read_fidx_page((unsigned long)&entry);
    return ret;
}

int sufs_libfs_cmd_read_dentry_page(int inode, unsigned long lba, void *buf) {
    struct tfs_dentry_page_entry entry;
    int ret = 0;

    entry.inode = inode;
    entry.lba = lba;
    entry.page_buffer = buf;
    ret = (int)tfs_read_dentry_page((unsigned long)&entry);
    return ret;
}

int sufs_libfs_cmd_alloc_inode_in_directory(int *inode, int pinode,
                                            int name_len, int type, int mode,
                                            int uid, int gid, char *name) {
    struct tfs_alloc_inode_arg entry;
    int ret = 0;

    entry.pinode = pinode;
    entry.name_len = name_len;
    entry.type = type;
    entry.mode = mode;
    entry.uid = uid;
    entry.gid = gid;
    entry.name = name;
    entry.inode = 0;

    ret = (int)tfs_alloc_inode_in_directory((unsigned long)&entry);
    (*inode) = entry.inode;
    return ret;
}

int sufs_libfs_cmd_release_inode(int inode, int pinode) {
    struct tfs_release_inode_arg entry;
    int ret = 0;

    entry.inode = inode;
    entry.pinode = pinode;

    ret = (int)tfs_release_inode((unsigned long)&entry);
    return ret;
}

int sufs_libfs_cmd_truncate_inode(int inode) {
    struct tfs_truncate_inode_arg entry;
    int ret = 0;

    entry.inode = inode;

    ret = (int)tfs_truncate_inode((unsigned long)&entry);
    return ret;
}

int sufs_libfs_cmd_truncate_file(int inode, unsigned long index_lba, int offset) {
    struct tfs_truncate_file_arg entry;
    int ret = 0;
    entry.inode = inode;
    entry.index_lba = index_lba;
    entry.offset = offset;
    ret = (int)tfs_truncate_file((unsigned long)&entry);
    return ret;
}

int sufs_libfs_cmd_rename(int old_finode, int old_dinode, int new_finode,
                          int new_dinode, char *new_name) {
    struct tfs_rename_arg entry;
    int ret = 0;
    entry.old_finode = old_finode;
    entry.old_dinode = old_dinode;
    entry.new_finode = new_finode;
    entry.new_dinode = new_dinode;
    entry.new_name = new_name;
    ret = (int)tfs_rename((unsigned long)&entry);

    return ret;
}

int sufs_libfs_cmd_alloc_dma_buffer(int size, dma_buffer *buf) {
    struct tfs_dma_buffer_arg entry;
    int ret = 0;

    entry.buf = buf;
    entry.size = size;

    ret = (int)tfs_create_dma_buffer((unsigned long)&entry);
    return ret;
}

int sufs_libfs_cmd_free_dma_buffer(dma_buffer *buf) {
    struct tfs_dma_buffer_arg entry;
    int ret = 0;

    entry.buf = buf;

    ret = (int)tfs_delete_dma_buffer((unsigned long)&entry);
    return ret;
}

static void cmd_io_callback(void *data) { (*((volatile int *)data))++; }

int sufs_libfs_cmd_read_blk(unsigned long lba, int size, dma_buffer *buf,
                            void *data) {
    struct tfs_blk_io_arg entry;
    int ret = 0;

    entry.lba = lba;
    entry.count = size;
    entry.buf = buf;
    entry.callback = cmd_io_callback;
    entry.data = data;

    ret = (int)tfs_blk_read((unsigned long)&entry);
    return ret;
}

int sufs_libfs_cmd_write_blk(unsigned long lba, int size, dma_buffer *buf,
                             void *data) {
    struct tfs_blk_io_arg entry;
    int ret = 0;

    entry.lba = lba;
    entry.count = size;
    entry.buf = buf;
    entry.callback = cmd_io_callback;
    entry.data = data;

    ret = (int)tfs_blk_write((unsigned long)&entry);
    return ret;
}

int sufs_libfs_cmd_sync(void) { return (int)tfs_sync(); }

int sufs_libfs_cmd_append_file(int inode, int num_block, unsigned long *block) {
    struct tfs_append_file_arg entry;
    int ret = 0;

    entry.inode = inode;
    entry.num_block = num_block;
    entry.block = block;

    ret = (int)tfs_append_file((unsigned long)&entry);
    LOG_FS("append file %d, num_block: %d, block: %lx\n", inode,
                               num_block, *block);
    return ret;
}

int sufs_libfs_cmd_tfs_init(void) {
    int ret = 0;
    ret = (int)tfs_init();
    return ret;
}

int sufs_libfs_cmd_blk_idle(void) {
    int ret = 0;
    ret = (int)tfs_blk_idle();
    return ret;
}