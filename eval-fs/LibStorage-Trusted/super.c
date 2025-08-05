#include <stdlib.h>

#include "./include/balloc.h"
#include "./include/ialloc.h"
#include "./include/journal.h"
#include "./include/libfs-headers.h"
#include "./include/super.h"
#include "./include/logger.h"

void tfs_init_sb(struct tfs_super_block *sb) {

    // init bitmap
    tfs_alloc_inode_free_lists(sb);
    tfs_init_inode_free_lists(sb);

    tfs_alloc_block_free_lists(sb);
    tfs_init_block_free_list(sb, 0);

    sb->inode_bitmap_start = SUFS_SUPER_PAGE_SIZE;
    sb->block_bitmap_start =
        sb->inode_bitmap_start + FS_INODE_BITMAP_LOGICAL_BLOCKS * FS_BLOCK_SIZE;
    sb->sinode_start =
        sb->block_bitmap_start + FS_FREE_BITMAP_LOGICAL_BLOCKS * FS_BLOCK_SIZE;

    // sb->data_start = sb->sinode_start +
    //                  SUFS_MAX_INODE_NUM * sizeof(struct sufs_shadow_inode);
    sb->data_start = FS_HEAD_BYTES;

    sb->journal_start = FS_TOTAL_BYTES;
    
    LOG_INFO("sb->inode_bitmap_start %lu, sb->block_bitmap_start %lu, "
             "sb->sinode_start %lu, sb->data_start %lu\n",
             sb->inode_bitmap_start, sb->block_bitmap_start,
             sb->sinode_start, sb->data_start);

    // init journal
    sb->journal = (struct tfs_journal *)malloc(sizeof(struct tfs_journal));
    tfs_init_journal(sb->journal);
}

void tfs_fini_sb(struct tfs_super_block *sb) {

    tfs_fini_journal(sb->journal);

    tfs_free_inode_free_lists(sb);
    tfs_delete_block_free_lists(sb);

    free(sb->journal);
}