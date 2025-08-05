#ifndef SUFS_LIBFS_SUPER_H_
#define SUFS_LIBFS_SUPER_H_

#include "../../include/libfs_config.h"
#include "../../include/nvme-driver.h"

#include "../../../LibStorage-Trusted/include/trusted-api.h"

enum bm_type {
    BM_INODE = 0,
    BM_FREE,
};

struct sufs_libfs_super_block {

    // Just the block offset
    unsigned long start_addr;

    /* Free list of each CPU, used for managing PM space */
    struct sufs_libfs_free_list *free_lists;

    /* Free inode of each CPU, used for allocating and freeing inode space */
    struct sufs_libfs_inode_free_list *inode_free_lists;

    /* the lba of the journal*/
    unsigned long journal_addr;

    // unsigned long journal_mem_va;
    struct sufs_libfs_journal_pointer_page *journal_pointer_mem_page;

    int journal_num_pages;
};

extern struct sufs_libfs_super_block sufs_libfs_sb;

static inline unsigned long
sufs_libfs_offset_to_virt_addr(unsigned long offset) {
    return offset + SUFS_BLK_START;
}

static inline unsigned long sufs_libfs_virt_addr_to_offset(unsigned long addr) {
    return addr - SUFS_BLK_START;
}

void sufs_libfs_init_super_block(struct sufs_libfs_super_block *sb);

#endif /* INCLUDE_SUPER_H_ */
