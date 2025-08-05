#ifndef SUFS_LIBFS_CMD_H_
#define SUFS_LIBFS_CMD_H_

#include "../../include/libfs_config.h"
#include "super.h"

int sufs_libfs_cmd_map_file(int ino, int writable, unsigned long *index_offset);
int sufs_libfs_cmd_unmap_file(int ino, int pino);

int sufs_libfs_cmd_chown(int inode, int uid, int gid);

int sufs_libfs_cmd_chmod(int inode, unsigned int mode);

int sufs_libfs_cmd_read_fidx_page(int inode, unsigned long lba, void *buf);

int sufs_libfs_cmd_read_dentry_page(int inode, unsigned long lba, void *buf);

int sufs_libfs_cmd_alloc_inode_in_directory(int *inode, int pinode,
                                            int name_len, int type, int mode,
                                            int uid, int gid, char *name);

int sufs_libfs_cmd_rename(int old_finode, int old_dinode, int new_finode,
                          int new_dinode, char *new_name);

int sufs_libfs_cmd_append_file(int inode, int num_block, unsigned long *block);

int sufs_libfs_cmd_alloc_dma_buffer(int size, dma_buffer *buf);

int sufs_libfs_cmd_free_dma_buffer(dma_buffer *buf);

int sufs_libfs_cmd_read_blk(unsigned long lba, int size, dma_buffer *buf,
                            void *data);
int sufs_libfs_cmd_write_blk(unsigned long lba, int size, dma_buffer *buf,
                             void *data);
int sufs_libfs_cmd_sync(void);
int sufs_libfs_cmd_truncate_inode(int inode);
int sufs_libfs_cmd_release_inode(int inode, int pinode);
int sufs_libfs_cmd_tfs_init(void);
int sufs_libfs_cmd_truncate_file(int inode, unsigned long index_lba, int offset);
int sufs_libfs_cmd_blk_idle(void);
#endif
