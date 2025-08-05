#ifndef TFS_SUPER_H_
#define TFS_SUPER_H_

struct sufs_shadow_inode {
    union {
        struct {
            char file_type;
            unsigned int mode;
            unsigned int uid;
            unsigned int gid;
            int pinode;

            unsigned long index_offset;
        };
        char padding[FS_BLOCK_SIZE];
    };
};

struct tfs_super_block {
    unsigned long sinode_start;
    unsigned long inode_bitmap_start;
    unsigned long block_bitmap_start;
    unsigned long data_start;
    unsigned long journal_start;

    struct tfs_free_list *block_free_lists;

    struct tfs_inode_free_list *inode_free_lists;

    struct tfs_journal *journal;
};

void tfs_init_sb(struct tfs_super_block *sb);

void tfs_fini_sb(struct tfs_super_block *sb);

#endif // TFS_SUPER_H_