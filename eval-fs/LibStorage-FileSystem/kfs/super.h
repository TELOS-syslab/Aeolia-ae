#ifndef SUFS_KFS_SUPER_H_
#define SUFS_KFS_SUPER_H_

#include <linux/kernel.h>
#include <linux/memory.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/module.h>

#include "../include/kfs_config.h"
#include "lease.h"

#define SUFS_KFS_UNOWNED 0
#define SUFS_KFS_WRITE_OWNED 1
#define SUFS_KFS_READ_OWNED 2

/* Confirm to the order of kstat */
struct sufs_shadow_inode {
    union {
        struct {
            char file_type;
            unsigned int mode;
            unsigned int uid;
            unsigned int gid;
            int pinode;

            /* Where to find the first index page */
            unsigned long index_offset;
        };
        char padding[FS_BLOCK_SIZE];
    };
};

/* Make it in DRAM as of now ... */
struct sufs_super_block {

    // The va means the byte address in nvme device
    unsigned long start_virt_addr;
    unsigned long end_virt_addr;

    unsigned long tot_bytes;

    int cpus;

    /* Free list of each CPU, used for managing PM space */
    struct sufs_free_list *free_lists;

    // we can use the addr for a nvme device (struct sufs_shadow_inode)
    unsigned long sinode_start;
    // struct sufs_shadow_inode *sinode_array;

    unsigned long inode_bitmap_start;
    unsigned long block_bitmap_start;
    unsigned long data_start;

    /* Free inode of each CPU, used for allocating and freeing inode space */
    struct sufs_inode_free_list *inode_free_lists;

    // unsigned long head_reserved_blocks;
};

extern struct sufs_super_block sufs_sb;
extern int sufs_kfs_delegation;

void sufs_init_sb(void);

int sufs_mount(void);

int sufs_umount(unsigned long addr);

long sufs_debug_read(void);

int sufs_fs_init(void);

void sufs_fs_fini(void);

#endif /* KFS_SUPER_H_ */
