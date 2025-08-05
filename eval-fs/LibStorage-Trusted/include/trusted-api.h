#ifndef TFS_TRUSTED_API_H_
#define TFS_TRUSTED_API_H_

#include "libfs-headers.h"

struct tfs_blk_io_arg {
    uint64_t lba;
    uint32_t count;
    void (*callback)(void *);
    void *data;
    struct dma_buffer *buf;
};

struct tfs_map_arg {
    int inode;
    int perm;
    unsigned long index_offset;
};

struct tfs_inode_alloc_arg {
    int cpu, inode;
    char *bm_buffer;
    unsigned long inode_bm_lba;
};

struct tfs_sys_info_arg {
    int pmnode_num;
    int sockets;
    int cpus_per_socket;
    void *raddr;
};

struct tfs_block_alloc_arg {
    int cpu;
    unsigned long block;
    char *bm_buffer;
    unsigned long block_bm_lba;
};

struct tfs_dma_buffer_arg {
    struct dma_buffer *buf;
    unsigned long size;
};

struct tfs_read_bm_arg {
    unsigned long lba;
    char *bm;
};

struct tfs_fidx_page_entry {
    int inode;
    unsigned long lba;
    void *page_buffer;
};

struct tfs_dentry_page_entry {
    int inode;
    unsigned long lba;
    void *page_buffer;
};

struct tfs_alloc_inode_arg {
    int inode, pinode;
    int name_len, type, mode, uid, gid;
    char *name;
};

struct tfs_release_inode_arg {
    int inode, pinode;
};

struct tfs_truncate_inode_arg {
    int inode;
};

struct tfs_truncate_file_arg {
    int inode, offset;
    unsigned long index_lba;
};

struct tfs_rename_arg {
    int old_finode, old_dinode;
    int new_finode, new_dinode;
    char *new_name;
};

struct tfs_chmod_arg {
    int inode;
    unsigned int mode;
};

struct tfs_chown_arg {
    int inode, owner, group;
};

struct tfs_append_file_arg {
    int inode;
    int num_block;
    unsigned long *block;
};

int tfs_blk_read(unsigned long arg);
int tfs_blk_write(unsigned long arg);

int tfs_mmap_file(unsigned long arg);
int tfs_unmap_file(unsigned long arg);

int tfs_create_dma_buffer(unsigned long arg);
int tfs_delete_dma_buffer(unsigned long arg);

int tfs_read_fidx_page(unsigned long arg);
int tfs_read_dentry_page(unsigned long arg);
int tfs_alloc_inode_in_directory(unsigned long arg);
int tfs_release_inode(unsigned long arg);
int tfs_chmod(unsigned long arg);
int tfs_chown(unsigned long arg);
int tfs_sync(void);
int tfs_append_file(unsigned long arg);
int tfs_truncate_inode(unsigned long arg);
int tfs_rename(unsigned long arg);
int tfs_truncate_file(unsigned long arg);
int tfs_blk_idle(void);

int tfs_init(void);

#endif // TFS_TRUSTED_API_H_