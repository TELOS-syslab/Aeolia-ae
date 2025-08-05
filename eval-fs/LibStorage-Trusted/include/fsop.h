#ifndef _TFS_FSOP_H_
#define _TFS_FSOP_H_

#include <pthread.h>

#include "libfs-headers.h"
#include "logger.h"
#include "tls.h"
#include "trusted-states.h"
#include "rwlock_bravo.h"

#define TFS_READ 1
#define TFS_WRITE 2

/* Confirm to the order of kstat */
struct tfs_shadow_inode {
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

struct tfs_inode {
    char file_type;
    unsigned int mode;
    unsigned int uid;
    unsigned int gid;
    unsigned long size;
    unsigned long offset;
    long atime;
    long ctime;
    long mtime;
};

struct tfs_dir_entry {
    char name_len;
    int ino_num;
    short rec_len;
    struct tfs_inode inode;
    char name[];
};

struct tfs_fidx_entry {
    unsigned long offset;
};

struct tfs_sinode_buffer {
    int size;
    unsigned long lba;
    dma_buffer *buf;
};

struct tfs_fidx_buffer {
    unsigned long lba;
    dma_buffer *buf;
};

struct tfs_dentry_buffer {
    unsigned long lba;
    dma_buffer *buf;
};

struct tfs_fidx_buffer_list {
    struct tfs_fidx_buffer fidx_buffer;
    struct tfs_fidx_buffer_list *next;
    struct tfs_fidx_buffer_list *prev;
};

struct tfs_dentry_buffer_list {
    struct tfs_dentry_buffer dentry_buffer;
    struct tfs_dentry_buffer_list *next;
    struct tfs_dentry_buffer_list *prev;
};

struct tfs_dir_tail {
    struct tfs_dir_entry *end_idx;
    struct tfs_dentry_buffer_list *dentry_buffer;
    pthread_spinlock_t lock;
};

struct tfs_dir_cache {
    pthread_spinlock_t index_lock;

    struct tfs_dir_tail *dir_tails;
    struct tfs_chainhash *map_;
    struct tfs_chainhash *dcache_array;


    struct tfs_dentry_buffer_list *dentry_buffer_list_head;
    struct tfs_dentry_buffer_list *dentry_buffer_list_tail;
};

struct tfs_dir_map_item {
    struct tfs_dentry_buffer_list *dentry_buffer;
    struct tfs_dir_entry *dentry_entry;
};

struct tfs_inode_cache {
    int dead;
    struct tfs_sinode_buffer *sinode_buffer;

    struct tfs_radix_array *fidx_array;
    struct tfs_fidx_buffer_list *fidx_buffer_list_head;
    struct tfs_fidx_buffer_list *fidx_buffer_list_tail;
    struct tfs_fidx_entry *old_fidx_entry;

    struct tfs_dir_cache *dir_cache;

    pthread_rwlock_t rwlock;
};

static void fsop_io_callback(void *data) { (*((volatile int *)data))++; }

static inline int tfs_build_inode_cache(struct tfs_inode_cache *inodec,
                                        int inode, int need_read) {
    unsigned long lba;
    int ios = 0;
    ls_nvme_qp* qp = tfs_tls_ls_nvme_qp();
    lba = tfs_inode_to_sinode_lba(inode);
    inodec->sinode_buffer =
        (struct tfs_sinode_buffer *)malloc(sizeof(struct tfs_sinode_buffer));
    inodec->sinode_buffer->lba = lba;
    LOG_INFO("inode %d lba %lx\n", inode, lba);
    inodec->sinode_buffer->buf = (dma_buffer *)malloc(sizeof(dma_buffer));
    create_dma_buffer(tfs_tls_ls_nvme_dev(), inodec->sinode_buffer->buf,
                      SUFS_PAGE_SIZE);
    memset(inodec->sinode_buffer->buf->buf, 0, SUFS_PAGE_SIZE);
    inodec->sinode_buffer->size = FS_BLOCK_SIZE;
    if (need_read) {
        int ret =
            read_blk_async(qp, inodec->sinode_buffer->lba,
                           inodec->sinode_buffer->size,
                           inodec->sinode_buffer->buf, fsop_io_callback, &ios);
        if (ret < 0) {
            LOG_ERROR("read sinode failed %d\n", ret);
            return ret;
        }
        while (*((volatile int*)(&ios)) == 0) {
            block_idle(qp);
            // wait for the read to finish
        }
    }
    inodec->fidx_array = NULL;
    inodec->fidx_buffer_list_head = NULL;
    inodec->fidx_buffer_list_tail = NULL;
    inodec->old_fidx_entry = NULL;
    pthread_rwlock_init(&inodec->rwlock, NULL);
}

static inline struct tfs_fidx_buffer_list *
tfs_fidx_buffer_init(unsigned long lba, int need_read) {
    struct tfs_fidx_buffer_list *fidx_buffer =
        (struct tfs_fidx_buffer_list *)malloc(
            sizeof(struct tfs_fidx_buffer_list));
    int ret = 0;
    int num_io = 0;
    ls_nvme_qp* qp = tfs_tls_ls_nvme_qp();
    fidx_buffer->fidx_buffer.lba = lba;
    fidx_buffer->fidx_buffer.buf = (dma_buffer *)malloc(sizeof(dma_buffer));
    create_dma_buffer(tfs_tls_ls_nvme_dev(), fidx_buffer->fidx_buffer.buf,
                      SUFS_PAGE_SIZE);
    memset(fidx_buffer->fidx_buffer.buf->buf, 0, SUFS_PAGE_SIZE);
    if (need_read) {
        ret = read_blk_async(qp, lba, SUFS_PAGE_SIZE,
                             fidx_buffer->fidx_buffer.buf, fsop_io_callback,
                             &num_io);
        if (ret < 0) {
            LOG_ERROR("read fidx buffer failed %d\n", ret);
            return NULL;
        }
        while (*((volatile int*)(&num_io)) == 0) {
            block_idle(qp);
            // wait for the read to finish
        }
    }
    fidx_buffer->next = NULL;
    fidx_buffer->prev = NULL;
    return fidx_buffer;
}

static inline int
tfs_fidx_buffer_fini(struct tfs_fidx_buffer_list *fidx_buffer_list) {
    if (fidx_buffer_list->fidx_buffer.buf) {
        // TODO: free after journal
        // delete_dma_buffer(tfs_tls_ls_nvme_dev(),
        //                   fidx_buffer_list->fidx_buffer.buf);
        // free(fidx_buffer_list->fidx_buffer.buf);
    }
    free(fidx_buffer_list);
    return 0;
}

static inline void
tfs_fidx_link_buffer_list(struct tfs_inode_cache *inodec,
                          struct tfs_fidx_buffer_list *item) {
    item->next = inodec->fidx_buffer_list_head;
    if (inodec->fidx_buffer_list_head)
        inodec->fidx_buffer_list_head->prev = item;
    inodec->fidx_buffer_list_head = item;
    if (!inodec->fidx_buffer_list_tail)
        inodec->fidx_buffer_list_tail = item;
    item->prev = NULL;
}

static inline void
tfs_fidx_unlink_buffer_list(struct tfs_inode_cache *inodec,
                            struct tfs_fidx_buffer_list *item) {
    if (item->prev)
        item->prev->next = item->next;
    if (item->next)
        item->next->prev = item->prev;

    if (inodec->fidx_buffer_list_head == item)
        inodec->fidx_buffer_list_head = item->next;

    if (inodec->fidx_buffer_list_tail == item)
        inodec->fidx_buffer_list_tail = item->prev;
}

static inline struct tfs_dentry_buffer_list *
tfs_dentry_buffer_init(unsigned long lba, int need_read) {
    struct tfs_dentry_buffer_list *dentry_buffer =
        (struct tfs_dentry_buffer_list *)malloc(
            sizeof(struct tfs_dentry_buffer_list));
    int num_io = 0;
    int ret = 0;
    ls_nvme_qp* qp = tfs_tls_ls_nvme_qp();
    dentry_buffer->dentry_buffer.lba = lba;
    dentry_buffer->dentry_buffer.buf = (dma_buffer *)malloc(sizeof(dma_buffer));
    ret = create_dma_buffer(tfs_tls_ls_nvme_dev(),
                            dentry_buffer->dentry_buffer.buf,
                            SUFS_FILE_BLOCK_SIZE);
    if (ret < 0) {
        LOG_ERROR("create dentry buffer failed %d\n", ret);
        return NULL;
    }
    memset(dentry_buffer->dentry_buffer.buf->buf, 0, SUFS_FILE_BLOCK_SIZE);

    if (need_read) {
        ret = read_blk_async(qp, lba, SUFS_FILE_BLOCK_SIZE,
                             dentry_buffer->dentry_buffer.buf, fsop_io_callback,
                             &num_io);
        if (ret < 0) {
            LOG_ERROR("read dentry buffer failed %d\n", ret);
            return NULL;
        }
        while (*((volatile int*)(&num_io)) == 0) {
            block_idle(qp);
            // wait for the read to finish
        }
    }
    dentry_buffer->next = NULL;
    dentry_buffer->prev = NULL;
    return dentry_buffer;
}

static inline int
tfs_dentry_buffer_fini(struct tfs_dentry_buffer_list *dentry_buffer_list) {
    if (dentry_buffer_list->dentry_buffer.buf) {
        // TODO: free after journal
        // delete_dma_buffer(tfs_tls_ls_nvme_dev(),
        //                   dentry_buffer_list->dentry_buffer.buf);
        // free(dentry_buffer_list->dentry_buffer.buf);
    }
    free(dentry_buffer_list);
    return 0;
}

static inline void
tfs_dentry_link_buffer_list(struct tfs_dir_cache *dirc,
                            struct tfs_dentry_buffer_list *item) {
    item->next = dirc->dentry_buffer_list_head;
    if (dirc->dentry_buffer_list_head)
        dirc->dentry_buffer_list_head->prev = item;
    dirc->dentry_buffer_list_head = item;
    if (!dirc->dentry_buffer_list_tail)
        dirc->dentry_buffer_list_tail = item;
    item->prev = NULL;
}

static inline void
tfs_dentry_unlink_buffer_list(struct tfs_dir_cache *dirc,
                              struct tfs_dentry_buffer_list *item) {
    if (item->prev)
        item->prev->next = item->next;
    if (item->next)
        item->next->prev = item->prev;

    if (dirc->dentry_buffer_list_head == item)
        dirc->dentry_buffer_list_head = item->next;

    if (dirc->dentry_buffer_list_tail == item)
        dirc->dentry_buffer_list_tail = item->prev;
}

extern struct tfs_inode_cache **tfs_inode_cache_array;

int tfs_do_mmap_file(int ino, int writable, unsigned long *index_offset);

int tfs_do_unmap_file(int ino);

int tfs_do_chown(int ino, int uid, int gid);

int tfs_do_chmod(int ino, unsigned int mode);

void tfs_inodec_init(void);

void tfs_inodec_fini(void);

int tfs_do_read_fidx_page(int inode, unsigned long lba, void *buf);
int tfs_do_read_dentry_page(int inode, unsigned long lba, void *buf);

int tfs_do_alloc_inode_in_directory(int *inode, int pinode, int name_len,
                                    int type, int mode, int uid, int gid,
                                    char *name);

int tfs_do_release_inode(int inode, int pinode, int need_journal);

int tfs_do_truncate_inode(int inode);

int tfs_do_sync(void);
int tfs_do_append_file(int inode, int num_block, unsigned long *block);
int tfs_do_rename(int old_finode, int old_dinode, int new_finode,
                  int new_dinode, char *new_name);

int tfs_do_truncate_file(int inode, unsigned long index_lba, int offset);
                  
#endif