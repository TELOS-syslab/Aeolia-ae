#ifndef SUFS_LIBFS_MNODE_H_
#define SUFS_LIBFS_MNODE_H_

#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "../../include/common_inode.h"
#include "../../include/libfs_config.h"
#include "amd64.h"
#include "chainhash.h"
#include "cmd.h"
#include "compiler.h"
#include "irwlock.h"
#include "radix_array.h"
#include "random.h"
#include "range_lock.h"
#include "super.h"
#include "types.h"
#include "util.h"

extern struct sufs_libfs_mnode **sufs_libfs_mnode_array;

struct sufs_libfs_radix_array;

/* TODO: cache line alignment */
struct sufs_libfs_dir_tail {
    struct sufs_dir_entry *end_idx;
    pthread_spinlock_t lock;
};

/* TODO: cache line alignment */
struct sufs_libfs_fidx_entry_page {
    int size;
    void *page_buffer;
    unsigned long lba;
    struct sufs_libfs_fidx_entry_page *next;
    struct sufs_libfs_fidx_entry_page *prev;
};

struct sufs_libfs_page_cache_entry {
    int dirty;
    dma_buffer buffer;
    u64 lba;
    struct sufs_libfs_page_cache_entry *next;
    struct sufs_libfs_page_cache_entry *prev;
};

struct sufs_libfs_mnode {
    int ino_num;
    char type;

    int parent_mnum;

    struct sufs_inode *inode;

    // va in dma_buffer
    struct sufs_fidx_entry *index_start;
    // va in dma_buffer
    struct sufs_fidx_entry *index_end;

    struct sufs_libfs_fidx_entry_page *fidx_entry_page_head;
    struct sufs_libfs_fidx_entry_page *fidx_entry_page_tail;

    /*
        atomic_long nlink_ __mpalign__;
        __padout__;
    */

    union {
        struct sufs_libfs_mnode_dir {
            pthread_spinlock_t index_lock;
            struct sufs_libfs_dir_tail *dir_tails;
            struct sufs_libfs_chainhash map_;
            struct sufs_libfs_dentry_page *dentry_page_head;
        } dir_data;

        struct sufs_libfs_mnode_file {
#if SUFS_INODE_RW_LOCK == SUFS_INODE_RW_LOCK_SPIN
            pthread_spinlock_t rw_lock;
#elif SUFS_INODE_RW_LOCK == SUFS_INODE_RW_LOCK_BRAVO
            struct sufs_libfs_bravo_rwlock rw_lock;
#endif

#if SUFS_LIBFS_RANGE_LOCK
            struct sufs_libfs_irange_lock range_lock;
#endif
            struct sufs_libfs_radix_array pages_;
            struct sufs_libfs_radix_array page_cache_;
            struct sufs_libfs_page_cache_entry *lru_page_cache_head;
            struct sufs_libfs_page_cache_entry *lru_page_cache_tail;
            unsigned long size_;
        } file_data;
    } data;
};

static inline void sufs_libfs_inode_init(struct sufs_inode *inode, char type,
                                         unsigned int mode, unsigned int uid,
                                         unsigned int gid,
                                         unsigned long offset) {
    inode->file_type = type;
    inode->mode = mode;
    inode->uid = uid;
    inode->gid = gid;
    inode->size = 0;
    inode->offset = offset;

    /* Make them 0 for now */
    inode->atime = inode->ctime = inode->mtime = 0;
}

static inline char sufs_libfs_mnode_type(struct sufs_libfs_mnode *m) {
    return m->type;
}

static inline bool sufs_libfs_mnode_dir_killed(struct sufs_libfs_mnode *mnode) {
    return sufs_libfs_chainhash_killed(&mnode->data.dir_data.map_);
}

static inline bool sufs_libfs_mnode_dir_replace_from(
    struct sufs_libfs_mnode *dstparent, char *dstname,
    struct sufs_libfs_mnode *mdst, struct sufs_libfs_mnode *srcparent,
    char *srcname, struct sufs_libfs_mnode *msrc,
    struct sufs_libfs_mnode *subdir, struct sufs_libfs_ch_item **item) {
    return sufs_libfs_chainhash_replace_from(
        &dstparent->data.dir_data.map_, dstname, mdst ? 1 : 0,
        &srcparent->data.dir_data.map_, srcname, msrc->ino_num, 0,
        SUFS_NAME_MAX, item);
}

// OK
static inline struct sufs_fidx_entry *
sufs_libfs_mnode_file_get_idx(struct sufs_libfs_mnode *m, u64 pageidx) {
    struct sufs_fidx_entry *idx =
        (struct sufs_fidx_entry *)sufs_libfs_radix_array_find(
            &m->data.file_data.pages_, pageidx, 0, 0);

#if 0
    printf("find idx: %ld, v: %lx\n", pageidx, (unsigned long) idx);
#endif

    return idx;
}

static inline struct sufs_libfs_page_cache_entry *
sufs_libfs_mnode_file_get_page_cache(struct sufs_libfs_mnode *m, u64 pageidx) {
    struct sufs_libfs_page_cache_entry *page_cache_entry =
        (struct sufs_libfs_page_cache_entry *)sufs_libfs_radix_array_find(
            &m->data.file_data.page_cache_, pageidx, 0, 0);

    return page_cache_entry;
}

/*
 * What we have in the radix tree is an pointer to the idx
 * This function returns the actual address of the page
 */
static inline unsigned long
sufs_libfs_mnode_file_get_page(struct sufs_libfs_mnode *m, u64 pageidx) {
    struct sufs_fidx_entry *idx = sufs_libfs_mnode_file_get_idx(m, pageidx);

    if (idx == NULL)
        return 0;
    else
        return idx->offset;
}

static inline struct sufs_libfs_page_cache_entry *
sufs_libfs_mnode_file_alloc_page_cache(u64 lba) {
    struct sufs_libfs_page_cache_entry *page_cache_entry =
        (struct sufs_libfs_page_cache_entry *)malloc(
            sizeof(struct sufs_libfs_page_cache_entry));
    int ret = 0;
    ret = sufs_libfs_cmd_alloc_dma_buffer(SUFS_FILE_BLOCK_SIZE,
                                          &page_cache_entry->buffer);
    if (ret < 0) {
        WARN_FS("alloc page cache buffer failed %d\n", ret);
        free(page_cache_entry);
        return NULL;
    }
    page_cache_entry->lba = lba;
    page_cache_entry->dirty = 0;
    page_cache_entry->next = NULL;
    page_cache_entry->prev = NULL;

    return page_cache_entry;
}

static inline void sufs_libfs_page_cache_entry_free(
    struct sufs_libfs_page_cache_entry *page_cache_entry) {
    if (page_cache_entry->buffer.buf)
        sufs_libfs_cmd_free_dma_buffer(&page_cache_entry->buffer);
    free(page_cache_entry);
}

static inline void sufs_libfs_mnode_file_link_page_cache(
    struct sufs_libfs_mnode *mnode,
    struct sufs_libfs_page_cache_entry *page_cache_entry) {
    page_cache_entry->next = mnode->data.file_data.lru_page_cache_head;
    if (mnode->data.file_data.lru_page_cache_head)
        mnode->data.file_data.lru_page_cache_head->prev = page_cache_entry;
    mnode->data.file_data.lru_page_cache_head = page_cache_entry;
    if (!mnode->data.file_data.lru_page_cache_tail)
        mnode->data.file_data.lru_page_cache_tail = page_cache_entry;
    page_cache_entry->prev = NULL;
}

static inline void sufs_libfs_mnode_file_unlink_page_cache(
    struct sufs_libfs_mnode *mnode,
    struct sufs_libfs_page_cache_entry *page_cache_entry) {
    if (page_cache_entry->prev)
        page_cache_entry->prev->next = page_cache_entry->next;
    if (page_cache_entry->next)
        page_cache_entry->next->prev = page_cache_entry->prev;

    if (mnode->data.file_data.lru_page_cache_head == page_cache_entry)
        mnode->data.file_data.lru_page_cache_head = page_cache_entry->next;

    if (mnode->data.file_data.lru_page_cache_tail == page_cache_entry)
        mnode->data.file_data.lru_page_cache_tail = page_cache_entry->prev;
}

// OK
static inline void sufs_libfs_mnode_file_init(struct sufs_libfs_mnode *mnode) {
    mnode->data.file_data.size_ = 0;

#if SUFS_LIBFS_RANGE_LOCK
    mnode->data.file_data.range_lock.sg_table = NULL;
    mnode->data.file_data.range_lock.sg_size = 0;
#endif
    mnode->data.file_data.lru_page_cache_head = NULL;
    mnode->data.file_data.lru_page_cache_tail = NULL;
    sufs_libfs_init_radix_array(&mnode->data.file_data.page_cache_,
                                sizeof(unsigned long),
                                ULONG_MAX / SUFS_PAGE_SIZE + 1, SUFS_PAGE_SIZE);
    sufs_libfs_init_radix_array(&mnode->data.file_data.pages_,
                                sizeof(unsigned long),
                                ULONG_MAX / SUFS_PAGE_SIZE + 1, SUFS_PAGE_SIZE);
}

// OK
static inline void sufs_libfs_mnode_file_fill_index(struct sufs_libfs_mnode *m,
                                                    u64 pageidx,
                                                    unsigned long v) {
#if 0
    printf("fill idx: %ld, v: %lx\n", pageidx, v);
#endif

    sufs_libfs_radix_array_find(&m->data.file_data.pages_, pageidx, 1, v);
}

static inline void
sufs_libfs_mnode_file_fill_page_cache(struct sufs_libfs_mnode *m, u64 pageidx,
                                      unsigned long v) {
    sufs_libfs_radix_array_find(&m->data.file_data.page_cache_, pageidx, 1, v);
}

static inline unsigned long
sufs_libfs_mnode_file_size(struct sufs_libfs_mnode *m) {
    return m->data.file_data.size_;
}

static inline void
sufs_libfs_radix_array_fini(struct sufs_libfs_radix_array *ra);

static inline void
sufs_libfs_mnode_file_free_page_cache(struct sufs_libfs_mnode *m) {
    int num_io = 0;
    sufs_libfs_radix_array_fini(&m->data.file_data.page_cache_);
    while (m->data.file_data.lru_page_cache_head) {
        struct sufs_libfs_page_cache_entry *page_cache_entry =
            m->data.file_data.lru_page_cache_head;
        // if (page_cache_entry->dirty) {
        //     num_io = 0;
        //     sufs_libfs_cmd_write_blk(page_cache_entry->lba,
        //                              SUFS_FILE_BLOCK_SIZE,
        //                              &page_cache_entry->buffer, &num_io);
        //     while (*((volatile int*)(&num_io)) == 0) {
        //         sufs_libfs_cmd_blk_idle();
        //         // wait for the write to finish
        //     }
        // }
        m->data.file_data.lru_page_cache_head =
            m->data.file_data.lru_page_cache_head->next;
        sufs_libfs_page_cache_entry_free(page_cache_entry);
    }
}

static inline void sufs_libfs_mnode_file_free_page(struct sufs_libfs_mnode *m) {
    sufs_libfs_radix_array_fini(&m->data.file_data.pages_);
    sufs_libfs_mnode_file_free_page_cache(m);
}

static inline void sufs_libfs_mnode_dir_free(struct sufs_libfs_mnode *m) {
    sufs_libfs_chainhash_fini(&m->data.dir_data.map_);
}

static inline void
sufs_libfs_mnode_file_resize_nogrow(struct sufs_libfs_mnode *m, u64 newsize) {
    u64 oldsize = m->data.file_data.size_;
    m->data.file_data.size_ = newsize;

    assert(FILE_BLOCK_ROUND_UP(newsize) <= FILE_BLOCK_ROUND_UP(oldsize));
}

static inline struct sufs_libfs_fidx_entry_page *
sufs_libfs_fidx_entry_page_init(unsigned long lba) {
    struct sufs_libfs_fidx_entry_page *page =
        (struct sufs_libfs_fidx_entry_page *)malloc(
            sizeof(struct sufs_libfs_fidx_entry_page));
    page->size = SUFS_PAGE_SIZE;
    page->page_buffer = (void *)malloc(SUFS_PAGE_SIZE);
    page->lba = lba;
    page->next = NULL;
    page->prev = NULL;

    return page;
}

static inline void
sufs_libfs_link_fidx_entry_page(struct sufs_libfs_mnode *m,
                                struct sufs_libfs_fidx_entry_page *page) {
    page->next = m->fidx_entry_page_head;
    if (m->fidx_entry_page_head)
        m->fidx_entry_page_head->prev = page;
    m->fidx_entry_page_head = page;
    if (!m->fidx_entry_page_tail)
        m->fidx_entry_page_tail = page;
}

static inline void
sufs_libfs_unlink_fidx_entry_page(struct sufs_libfs_mnode *m,
                                  struct sufs_libfs_fidx_entry_page *ipage) {
    if (ipage->prev)
        ipage->prev->next = ipage->next;
    if (ipage->next)
        ipage->next->prev = ipage->prev;

    if (m->fidx_entry_page_head == ipage)
        m->fidx_entry_page_head = ipage->next;

    if (m->fidx_entry_page_tail == ipage)
        m->fidx_entry_page_tail = ipage->prev;
}

static inline void
sufs_libfs_fidx_entry_page_fini(struct sufs_libfs_fidx_entry_page *page) {
    free(page->page_buffer);
    free(page);
}

// TODO: cache line alignment
struct sufs_libfs_dentry_page {
    int size;
    void *page_buffer;
    unsigned long lba;
    struct sufs_libfs_dentry_page *next;
};

static inline struct sufs_libfs_dentry_page *
sufs_libfs_dentry_page_init(unsigned long lba) {

    struct sufs_libfs_dentry_page *dpage =
        (struct sufs_libfs_dentry_page *)malloc(
            sizeof(struct sufs_libfs_dentry_page));
    dpage->size = SUFS_FILE_BLOCK_SIZE;
    dpage->page_buffer = (void *)malloc(SUFS_FILE_BLOCK_SIZE);
    dpage->lba = lba;
    dpage->next = NULL;

    return dpage;
}

static inline void
sufs_libfs_link_dentry_page(struct sufs_libfs_mnode *m,
                            struct sufs_libfs_dentry_page *dpage) {
    dpage->next = m->data.dir_data.dentry_page_head;
    m->data.dir_data.dentry_page_head = dpage;
}

static inline int
sufs_libfs_dentry_page_fini(struct sufs_libfs_dentry_page *dpage) {

    free(dpage->page_buffer);
    return 0;
}

struct sufs_libfs_dentry_page *
sufs_libfs_dentry_to_page(struct sufs_libfs_dentry_page *dpage,
                          struct sufs_dir_entry *idx);
struct sufs_dir_entry *sufs_libfs_get_dentry(struct sufs_libfs_dentry_page *dpg,
                                             int inode);

// OK
static inline void sufs_libfs_mnode_free(struct sufs_libfs_mnode *mnode) {
    if (mnode->type == SUFS_FILE_TYPE_REG) {
        sufs_libfs_inode_rwlock_destroy(mnode);
#if SUFS_LIBFS_RANGE_LOCK
        sufs_libfs_irange_lock_free(&(mnode->data.file_data.range_lock));
#endif
    }

    // free(mnode);
}

void sufs_libfs_mnodes_init(void);

void sufs_libfs_mnodes_fini(void);

struct sufs_libfs_mnode *sufs_libfs_mfs_mnode_init(u8 type, int ino_num,
                                                   int parent_mnum,
                                                   struct sufs_inode *inode);

void sufs_libfs_mnode_dir_init(struct sufs_libfs_mnode *mnode);

int sufs_libfs_mnode_dir_build_index(struct sufs_libfs_mnode *mnode);

struct sufs_libfs_mnode *
sufs_libfs_mnode_dir_lookup(struct sufs_libfs_mnode *mnode, char *name);

bool sufs_libfs_mnode_dir_enumerate(struct sufs_libfs_mnode *mnode, char *prev,
                                    char *name);

__ssize_t sufs_libfs_mnode_dir_getdents(struct sufs_libfs_mnode *mnode,
                                        unsigned long *offset_ptr, void *buffer,
                                        size_t length);

bool sufs_libfs_mnode_dir_entry_insert(struct sufs_libfs_mnode *mnode,
                                       char *name, int name_len,
                                       struct sufs_libfs_mnode *mf,
                                       struct sufs_dir_entry **dirp);

bool sufs_libfs_mnode_dir_insert(struct sufs_libfs_mnode *mnode, char *name,
                                 int name_len, struct sufs_libfs_mnode *mf,
                                 struct sufs_dir_entry **dirp);

bool sufs_libfs_mnode_dir_insert_create(struct sufs_libfs_mnode *mnode, char *name,
                                 int name_len, struct sufs_libfs_ch_item **item);

bool sufs_libfs_mnode_dir_remove(struct sufs_libfs_mnode *mnode, char *name);

bool sufs_libfs_mnode_dir_kill(struct sufs_libfs_mnode *mnode);

struct sufs_libfs_mnode *
sufs_libfs_mnode_dir_exists(struct sufs_libfs_mnode *mnode, char *name);

void sufs_libfs_mnode_file_resize_nogrow(struct sufs_libfs_mnode *m,
                                         u64 newsize);

void sufs_libfs_mnode_file_resize_append(struct sufs_libfs_mnode *m,
                                         u64 newsize, unsigned long ps);

struct sufs_fidx_entry *
sufs_libfs_mnode_file_index_append(struct sufs_libfs_mnode *m,
                                   unsigned long addr);

void sufs_libfs_mnode_file_truncate_zero(struct sufs_libfs_mnode *m);

void sufs_libfs_mnode_file_delete(struct sufs_libfs_mnode *m);

int sufs_libfs_mnode_stat(struct sufs_libfs_mnode *m, struct stat *st);

#endif /* SUFS_MNODE_H_ */
