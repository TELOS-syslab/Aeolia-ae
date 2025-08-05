#include <errno.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>

#include "../include/common_inode.h"
#include "../include/libfs_config.h"
#include "../include/ring_buffer.h"

#include "./include/cmd.h"
#include "./include/mfs.h"
#include "./include/mnode.h"
#include "./include/stat.h"
#include "./include/super.h"
#include "./include/tls.h"
#include "./include/util.h"

struct sufs_libfs_mnode *sufs_libfs_root_dir = NULL;

/*
 * Whether a file is mapped as read-only or read-write
 * set: read-write
 * clear: read-only
 */
atomic_char *sufs_libfs_inode_mapped_attr = NULL;

/*
 * Whether an inode has been mapped before or not
 * set: mapped before
 */
atomic_char *sufs_libfs_inode_has_mapped = NULL;

/* Whether an inode's index has been built or not */
atomic_char *sufs_libfs_inode_has_index = NULL;

pthread_spinlock_t *sufs_libfs_inode_map_lock = NULL;

struct sufs_libfs_mapped_inodes {
    int *inodes;
    int *pinodes;
    pthread_spinlock_t lock;
    int index;
};

static struct sufs_libfs_mapped_inodes sufs_libfs_mapped_inodes;

void sufs_libfs_mfs_init(void) {
    int i = 0;
    sufs_libfs_inode_mapped_attr = calloc(1, SUFS_MAX_INODE_NUM / sizeof(char));

    sufs_libfs_inode_has_mapped = calloc(1, SUFS_MAX_INODE_NUM / sizeof(char));

    sufs_libfs_inode_has_index = calloc(1, SUFS_MAX_INODE_NUM / sizeof(char));

    sufs_libfs_mapped_inodes.inodes = calloc(SUFS_MAX_MAP_FILE, sizeof(int));
    sufs_libfs_mapped_inodes.pinodes = calloc(SUFS_MAX_MAP_FILE, sizeof(int));

    sufs_libfs_inode_map_lock =
        calloc(SUFS_LIBFS_FILE_MAP_LOCK_SIZE, sizeof(pthread_spinlock_t));

    if (sufs_libfs_inode_mapped_attr == NULL ||
        sufs_libfs_inode_has_mapped == NULL ||
        sufs_libfs_inode_has_index == NULL ||
        sufs_libfs_mapped_inodes.inodes == NULL ||
        sufs_libfs_inode_map_lock == NULL) {
        fprintf(stderr, "Cannot allocate sufs_inode_mapped!\n");
        abort();
    }

    sufs_libfs_mapped_inodes.index = 0;
    pthread_spin_init(&(sufs_libfs_mapped_inodes.lock), PTHREAD_PROCESS_SHARED);

    for (i = 0; i < SUFS_LIBFS_FILE_MAP_LOCK_SIZE; i++) {
        pthread_spin_init(&(sufs_libfs_inode_map_lock[i]),
                          PTHREAD_PROCESS_SHARED);
    }
}

void sufs_libfs_mfs_fini(void) {
    if (sufs_libfs_inode_mapped_attr)
        free(sufs_libfs_inode_mapped_attr);

    if (sufs_libfs_inode_has_mapped)
        free(sufs_libfs_inode_has_mapped);

    if (sufs_libfs_inode_has_index)
        free(sufs_libfs_inode_has_index);

    if (sufs_libfs_mapped_inodes.inodes)
        free(sufs_libfs_mapped_inodes.inodes);

    if (sufs_libfs_mapped_inodes.pinodes)
        free(sufs_libfs_mapped_inodes.pinodes);

    if (sufs_libfs_inode_map_lock)
        free((void *)sufs_libfs_inode_map_lock);
}

// OK
void sufs_libfs_mfs_add_mapped_inode(int ino) {
    int index = 0;
    /* TODO: Would be good if we have atomic test and set bit */
    if (sufs_libfs_bm_test_bit(sufs_libfs_inode_has_mapped, ino))
        return;

    /* FIXME: This might be a scalabilty bottleneck */

    /*
     * Also, remove a directory requries us to map the directory to check
     * how many files are still there.
     *
     * This part might overflow with a large number of removed directories
     */
    pthread_spin_lock(&(sufs_libfs_mapped_inodes.lock));

    if (sufs_libfs_bm_test_bit(sufs_libfs_inode_has_mapped, ino))
        goto out;
    else {
        sufs_libfs_bm_set_bit(sufs_libfs_inode_has_mapped, ino);
    }

    if ((index = sufs_libfs_mapped_inodes.index) >= SUFS_MAX_MAP_FILE) {
        fprintf(stderr, "index exceed SUFS_MAX_MAP_FILE!\n");
        goto out;
    }

    sufs_libfs_mapped_inodes.inodes[index] = ino;
    sufs_libfs_mapped_inodes.pinodes[index] =
        sufs_libfs_mnode_array[ino]->parent_mnum;
    sufs_libfs_mapped_inodes.index++;
    LOG_FS("sufs_libfs_mfs_add_mapped_inode: ino: %d, pino: %d\n", ino,
           sufs_libfs_mapped_inodes.pinodes[index]);
out:
    pthread_spin_unlock(&(sufs_libfs_mapped_inodes.lock));
}

// OK
void sufs_libfs_mfs_unmap_mapped_inodes(void) {
    int i = 0;

    pthread_spin_lock(&(sufs_libfs_mapped_inodes.lock));

    for (i = 0; i < sufs_libfs_mapped_inodes.index; i++) {
        int ino = sufs_libfs_mapped_inodes.inodes[i];
        int pino = sufs_libfs_mapped_inodes.pinodes[i];

        if (sufs_libfs_bm_test_bit(sufs_libfs_inode_has_mapped, ino)) {
            /*
             * This is fine, we don't need the complicated
             * sufs_libfs_mnode_file_unmap since:
             * 1. We don't need to recycle memory
             * 2. We don't need to maintain the ownership of block / inode
             * any more
             */

            sufs_libfs_cmd_unmap_file(ino, pino);
        }
    }

    pthread_spin_unlock(&(sufs_libfs_mapped_inodes.lock));
}

// OK
void sufs_libfs_fs_init(void) {
    /* Being lazy here */
    struct sufs_inode *inode = calloc(1, sizeof(struct sufs_inode));
    inode->file_type = SUFS_FILE_TYPE_DIR;
    inode->mode = SUFS_ROOT_PERM;

    inode->uid = inode->gid = 0;
    inode->size = 0;
    /* offset == 0 should be fine; the LibFS really does not use this value */
    inode->offset = 0;

    sufs_libfs_root_dir = sufs_libfs_mfs_mnode_init(
        SUFS_FILE_TYPE_DIR, SUFS_ROOT_INODE, SUFS_ROOT_INODE, inode);
}

void sufs_libfs_fs_fini(void) {
    if (sufs_libfs_root_dir->inode)
        free(sufs_libfs_root_dir->inode);

    // sufs_libfs_mfs_unmap_mapped_inodes();
}

/*
 * Copy the next path element from path into name.
 * Update the pointer to the element following the copied one.
 * The returned path has no leading slashes,
 * so the caller can check *path=='\0' to see if the name is the last one.
 *
 * If copied into name, return 1.
 * If no name to remove, return 0.
 * If the name is longer than DIRSIZ, return -1;
 *
 * Examples:
 *   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
 *   skipelem("///a//bb", name) = "bb", setting name = "a"
 *   skipelem("a", name) = "", setting name = "a"
 *   skipelem("", name) = skipelem("////", name) = 0
 */
// OK
static int skipelem(char **rpath, char *name) {
    char *path = *rpath;
    char *s = NULL;
    int len = 0;

    while (*path == '/')
        path++;

    if (*path == 0)
        return 0;

    s = path;

    while (*path != '/' && *path != 0)
        path++;

    len = path - s;

    if (len > SUFS_NAME_MAX) {
        fprintf(stderr,
                "Error: Path component longer than SUFS_NAME_SIZE"
                " (%d characters)\n",
                SUFS_NAME_MAX);
        return -1;
    } else {
        memmove(name, s, len);
        if (len < SUFS_NAME_MAX) {
            name[len] = 0;
        }
    }

    while (*path == '/')
        path++;

    *rpath = path;
    return 1;
}

/*
 * Look up and return the mnode for a path name.  If nameiparent is true,
 * return the mnode for the parent and copy the final path element into name.
 */
// OK
struct sufs_libfs_mnode *sufs_libfs_namex(struct sufs_libfs_mnode *cwd,
                                          char *path, bool nameiparent,
                                          char *name) {
    struct sufs_libfs_mnode *m;
    int r;

#if 0
    printf("path is %s\n", path);
#endif

    if (*path == '/') {
        m = sufs_libfs_root_dir;
    } else {
        m = cwd;
    }

    while ((r = skipelem(&path, name)) == 1) {
        struct sufs_libfs_mnode *next = NULL;
        LOG_FS("m->ino_num: %d\n", m->ino_num);

        if (sufs_libfs_mnode_type(m) != SUFS_FILE_TYPE_DIR)
            return NULL;

        if (nameiparent && *path == '\0') {
            /* Stop one level early */
            return m;
        }

        next = sufs_libfs_mnode_dir_lookup(m, name);

        if (!next)
            return NULL;

        m = next;
    }

    if (r == -1 || nameiparent)
        return NULL;

    return m;
}

// OK
s64 sufs_libfs_readm(struct sufs_libfs_mnode *m, char *buf, u64 start,
                     u64 nbytes) {
    u64 end = sufs_libfs_mnode_file_size(m);
    u64 off = 0;
    int ret;
    int num_io = 0;

#if SUFS_LIBFS_RANGE_LOCK
    unsigned long start_seg = 0, end_seg = 0;
#endif

    if (start + nbytes < end) {
        end = start + nbytes;
    }
    nbytes = end - start;

    sufs_libfs_inode_read_lock(m);

#if SUFS_LIBFS_RANGE_LOCK
    start_seg = start >> SUFS_LIBFS_SEGMENT_SIZE_BITS;
    end_seg = end >> SUFS_LIBFS_SEGMENT_SIZE_BITS;
    sufs_libfs_irange_lock_read_lock(&m->data.file_data.range_lock, start_seg,
                                     end_seg);
#endif

#if 0
    printf("start: %ld, off: %ld, end: %ld\n", start, off, end);
#endif

    LOG_FS("readm begin: start: %ld, end: %ld\n", start, end);
    while (start + off < end) {
        u64 pos = start + off, pgbase = FILE_BLOCK_ROUND_DOWN(pos), pgoff = 0,
            pgend = 0, len = 0;
        struct sufs_libfs_page_cache_entry *page_cache_entry =
            sufs_libfs_mnode_file_get_page_cache(m,
                                                 pgbase / SUFS_FILE_BLOCK_SIZE);
        if (page_cache_entry == NULL) {
            unsigned long lba = sufs_libfs_mnode_file_get_page(
                m, pgbase / SUFS_FILE_BLOCK_SIZE);

            if (!lba) {
                LOG_FS("readm: addr is NULL\n");
                break;
            }
            LOG_FS("readm: addr: %lx\n", lba);
            page_cache_entry = sufs_libfs_mnode_file_alloc_page_cache(lba);
            if (page_cache_entry == NULL) {
                WARN_FS("readm: page cache entry is NULL\n");
                break;
            }
            sufs_libfs_mnode_file_link_page_cache(m, page_cache_entry);
            sufs_libfs_mnode_file_fill_page_cache(
                m, pgbase / SUFS_FILE_BLOCK_SIZE,
                (unsigned long)page_cache_entry);
            num_io = 0;
            ret = sufs_libfs_cmd_read_blk(lba, SUFS_FILE_BLOCK_SIZE,
                                          &page_cache_entry->buffer, &num_io);
            if (ret < 0) {
                WARN_FS("read blk in sufs_libfs_readm failed %d\n", ret);
                return -1;
            }
            while (*((volatile int*)(&num_io)) == 0) {
                sufs_libfs_cmd_blk_idle(); 
                // wait for the read to finish
            }
        } else {
            // Update LRU
            sufs_libfs_mnode_file_unlink_page_cache(m, page_cache_entry);
            sufs_libfs_mnode_file_link_page_cache(m, page_cache_entry);
        }

        pgoff = pos - pgbase;
        pgend = end - pgbase;

        if (pgend > SUFS_FILE_BLOCK_SIZE)
            pgend = SUFS_FILE_BLOCK_SIZE;

        len = pgend - pgoff;
        LOG_FS("readm iter: pos: %ld, pgbase: %ld, pgoff: %ld, pgend: "
               "%ld, len: %ld\n",
               pos, pgbase, pgoff, pgend, len);
#if 0
            printf("No delegation, len = %ld\n", len);
#endif
        // memcpy(buf + off, (char *)addr + pgoff, len);

        memcpy(buf + off, (char *)page_cache_entry->buffer.buf + pgoff, len);

        off += (pgend - pgoff);
    }

#if SUFS_LIBFS_RANGE_LOCK
    sufs_libfs_irange_lock_read_unlock(&m->data.file_data.range_lock, start_seg,
                                       end_seg);
#endif

    sufs_libfs_inode_read_unlock(m);

    return off;
}

int sufs_libfs_sync_page_cache(struct sufs_libfs_mnode *m) {
    struct sufs_libfs_page_cache_entry *page_cache_entry =
        m->data.file_data.lru_page_cache_head;
    int ret = 0;
    int tot_io = 0;
    int num_io = 0;

    while (page_cache_entry) {
        if (page_cache_entry->dirty) {
            tot_io++;
            page_cache_entry->dirty = 0;
            ret = sufs_libfs_cmd_write_blk(page_cache_entry->lba,
                                           SUFS_FILE_BLOCK_SIZE,
                                           &page_cache_entry->buffer, &num_io);
            if (ret < 0) {
                WARN_FS("write blk in sufs_libfs_sync_page_cache failed %d\n",
                        ret);
                return -1;
            }
            if (tot_io > 64) {
                while (*((volatile int*)(&num_io)) < tot_io) {
                    sufs_libfs_cmd_blk_idle();
                    // wait for the write to finish
                }
                tot_io = 0;
                *((volatile int*)(&num_io)) = 0;
            }
        }
        page_cache_entry = page_cache_entry->next;
    }
    while (*((volatile int*)(&num_io)) < tot_io) {
        sufs_libfs_cmd_blk_idle();
        // wait for the write to finish
    }

    return 0;
}

// OK
s64 sufs_libfs_writem(struct sufs_libfs_mnode *m, char *buf, u64 start,
                      u64 nbytes) {
    u64 end = start + nbytes;
    u64 off = 0;
    u64 size = sufs_libfs_mnode_file_size(m);
    // u64 e1,e2,e3,e4,e5,e6,e7,e8,e9,e10;
    // u64 e[100];
    // int ii = 0;
    
    int whole_lock = 0;
    int ret;
    int num_io;
#if SUFS_LIBFS_RANGE_LOCK
    unsigned long start_seg = 0, end_seg = 0;
#endif
    // e[ii++]= sufs_libfs_rdtsc();
    // printf("inode: %d, writem: start: %ld, end: %ld, size: %ld\n", m->ino_num,
    //        start, end, size); 

    SUFS_LIBFS_DEFINE_TIMING_VAR(writem_time);
    SUFS_LIBFS_DEFINE_TIMING_VAR(index_time);

    SUFS_LIBFS_START_TIMING(SUFS_LIBFS_WRITEM, writem_time);

    /*
     * Here we simplified the code to not consider sparse files. Handling
     * spare file is simple, just iterate to find whether there is an
     * page not mapped
     */

    if (end > size)
        whole_lock = 1;

    if (whole_lock) {
        sufs_libfs_inode_write_lock(m);
    } else {
        sufs_libfs_inode_read_lock(m);

#if SUFS_LIBFS_RANGE_LOCK
        start_seg = start >> SUFS_LIBFS_SEGMENT_SIZE_BITS;
        end_seg = end >> SUFS_LIBFS_SEGMENT_SIZE_BITS;
        sufs_libfs_irange_lock_write_lock(&m->data.file_data.range_lock,
                                          start_seg, end_seg);
#endif
    }
    LOG_FS("writem begin: start: %ld, file_size: %ld, end: %ld\n", start, size,
           end);
           
    // e[ii++]= sufs_libfs_rdtsc();
    while (start + off < end) {
        // e[ii++] = sufs_libfs_rdtsc();
        u64 pos = start + off, pgbase = FILE_BLOCK_ROUND_DOWN(pos),
            pgoff = pos - pgbase, pgend = end - pgbase, len = 0;

        unsigned long addr = 0;
        unsigned long block = 0;

        int need_resize = 0;

        if (pgend > SUFS_FILE_BLOCK_SIZE)
            pgend = SUFS_FILE_BLOCK_SIZE;

        len = pgend - pgoff;
        LOG_FS("writem iter: pos: %ld, pgbase: %ld, pgoff: %ld, pgend: "
               "%ld, len: %ld\n",
               pos, pgbase, pgoff, pgend, len);
        SUFS_LIBFS_START_TIMING(SUFS_LIBFS_INDEX, index_time);

        addr = sufs_libfs_mnode_file_get_page(m, pgbase / SUFS_FILE_BLOCK_SIZE);
        SUFS_LIBFS_END_TIMING(SUFS_LIBFS_INDEX, index_time);
        LOG_FS("writem iter: pos: %ld, addr: %lx\n", pos, addr);
        if (addr) {
            LOG_FS("writem have blk addr\n");
            if (pos + len > sufs_libfs_mnode_file_size(m)) {
                need_resize = 1;
            }

            struct sufs_libfs_page_cache_entry *page_cache_entry =
                sufs_libfs_mnode_file_get_page_cache(
                    m, pgbase / SUFS_FILE_BLOCK_SIZE);
            if (page_cache_entry == NULL) {
                LOG_FS("writem: build page cache. lba %lx\n", addr);
                page_cache_entry = sufs_libfs_mnode_file_alloc_page_cache(addr);
                if (page_cache_entry == NULL) {
                    WARN_FS("writem: page cache entry is NULL\n");
                    break;
                }
                sufs_libfs_mnode_file_link_page_cache(m, page_cache_entry);
                sufs_libfs_mnode_file_fill_page_cache(
                    m, pgbase / SUFS_FILE_BLOCK_SIZE,
                    (unsigned long)page_cache_entry);
                // num_io = 0;
                // ret =
                //     sufs_libfs_cmd_read_blk(addr, SUFS_FILE_BLOCK_SIZE,
                //                             &page_cache_entry->buffer, &num_io);
                // if (ret < 0) {
                //     WARN_FS("build cache in sufs_libfs_writem failed %d\n",
                //             ret);
                //     return -1;
                // }
                // while (*((volatile int*)(&num_io)) == 0) {
                //     sufs_libfs_cmd_blk_idle(); 
                //     // wait for the read to finish
                // }
            } else {
                // Update LRU
                sufs_libfs_mnode_file_unlink_page_cache(m, page_cache_entry);
                sufs_libfs_mnode_file_link_page_cache(m, page_cache_entry);
            }

            /*
             * What happens when writing past the end of the file but within
             * the file's last page?  One worry might be that we're exposing
             * some non-zero bytes left over in the part of the last page that
             * is past the end of the file.  Our plan is to ensure that any
             * file truncate zeroes out any partial pages.  Currently, we only
             * have O_TRUNC, which discards all pages.
             */

#if 0
                printf("No delegation!\n");
#endif
            memcpy((char *)page_cache_entry->buffer.buf + pgoff, buf + off,
                   len);
            page_cache_entry->dirty = 1;
            // memcpy((char *)addr + pgoff, buf + off, len);
            // sufs_libfs_clwb_buffer((char *)addr + pgoff, len);

            if (need_resize) {
                LOG_FS("writem need resize\n");
                sufs_libfs_mnode_file_resize_nogrow(m, pos + len);
            }
        } else {
            LOG_FS("writem no blk addr\n");
            u64 msize = 0;

            unsigned long addr = 0;

            msize = sufs_libfs_mnode_file_size(m);
            /*
             * If this is a write past the end of the file, we may need
             * to first zero out some memory locations, or even fill in
             * a few zero pages.  We do not support sparse files -- the
             * holes are filled in with zeroed pages.
             */
            while (msize < pgbase) {
                if (msize % SUFS_FILE_BLOCK_SIZE) {
                    sufs_libfs_mnode_file_resize_nogrow(
                        m, msize - (msize % SUFS_FILE_BLOCK_SIZE) +
                               SUFS_FILE_BLOCK_SIZE);

                    msize = msize - (msize % SUFS_FILE_BLOCK_SIZE) +
                            SUFS_FILE_BLOCK_SIZE;
                } else {
                    SUFS_LIBFS_START_TIMING(SUFS_LIBFS_INDEX, index_time);

                    sufs_libfs_cmd_append_file(
                        m->ino_num, SUFS_FILE_BLOCK_PAGE_CNT, &block);

                    SUFS_LIBFS_END_TIMING(SUFS_LIBFS_INDEX, index_time);
                    LOG_FS("writem new file data blocks %lx\n", block << SUFS_PAGE_SHIFT);

                    if (!block)
                        break;

                    addr = block << SUFS_PAGE_SHIFT;
                    LOG_FS("writem resize append blocks addr %lx\n", addr);
                    sufs_libfs_mnode_file_resize_append(
                        m, msize + SUFS_FILE_BLOCK_SIZE, addr);

                    msize += SUFS_FILE_BLOCK_SIZE;
                }
            }

            sufs_libfs_cmd_append_file(m->ino_num, SUFS_FILE_BLOCK_PAGE_CNT,
                                       &block);
            LOG_FS("writem new file data blocks %lx\n", block << SUFS_PAGE_SHIFT);

            if (!block)
                break;

            addr = block << SUFS_PAGE_SHIFT;
            LOG_FS("writem resize append blocks addr %lx\n", addr);

            SUFS_LIBFS_START_TIMING(SUFS_LIBFS_INDEX, index_time);

            sufs_libfs_mnode_file_resize_append(m, pos + len, addr);

            SUFS_LIBFS_END_TIMING(SUFS_LIBFS_INDEX, index_time);

#if 0
                printf("No delegation!\n");
#endif
            struct sufs_libfs_page_cache_entry *page_cache_entry =
                sufs_libfs_mnode_file_get_page_cache(
                    m, pgbase / SUFS_FILE_BLOCK_SIZE);
            if (page_cache_entry == NULL) {
                LOG_FS("writem: build page cache. lba %lx\n", addr);
                page_cache_entry = sufs_libfs_mnode_file_alloc_page_cache(addr);
                if (page_cache_entry == NULL) {
                    WARN_FS("writem: page cache entry is NULL\n");
                    break;
                }
                sufs_libfs_mnode_file_link_page_cache(m, page_cache_entry);
                sufs_libfs_mnode_file_fill_page_cache(
                    m, pgbase / SUFS_FILE_BLOCK_SIZE,
                    (unsigned long)page_cache_entry);
                // num_io = 0;
                // ret =
                //     sufs_libfs_cmd_read_blk(addr, SUFS_FILE_BLOCK_SIZE,
                //                             &(page_cache_entry->buffer), &num_io);
                // if (ret < 0) {
                //     WARN_FS("build cache in sufs_libfs_writem failed %d\n",
                //             ret);
                //     return -1;
                // }
                // while (*((volatile int*)(&num_io)) == 0) {
                //     sufs_libfs_cmd_blk_idle(); 
                //     // wait for the read to finish
                // }
            } else {
                WARN_FS("writem: page cache entry will never not NULL\n");
            }
            memcpy((char *)page_cache_entry->buffer.buf + pgoff, buf + off,
                   len);
            page_cache_entry->dirty = 1;
            // memcpy((void *)addr + pgoff, buf + off, len);
            // sufs_libfs_clwb_buffer((char *)addr + pgoff, len);
        }

        off += len;
    }
    if (whole_lock) {
#if SUFS_LIBFS_RANGE_LOCK
        sufs_libfs_irange_lock_resize(&m->data.file_data.range_lock,
                                      start + off);
#endif
        sufs_libfs_inode_write_unlock(m);
    } else {
#if SUFS_LIBFS_RANGE_LOCK
        sufs_libfs_irange_lock_write_unlock(&m->data.file_data.range_lock,
                                            start_seg, end_seg);
#endif

        sufs_libfs_inode_read_unlock(m);
    }
    // e[ii++] = sufs_libfs_rdtsc();
    // for (int i = 0; i < ii; i++) {
    //     printf("e[%d]: %ld | ", i, e[i+1]-e[i]);
    // }
    // printf("\n");
    // sufs_libfs_sfence();

    SUFS_LIBFS_END_TIMING(SUFS_LIBFS_WRITEM, writem_time);

    return off;
}

// OK
int sufs_libfs_truncatem(struct sufs_libfs_mnode *m, off_t length) {
    unsigned long msize = sufs_libfs_mnode_file_size(m);
    unsigned long mbase = FILE_BLOCK_ROUND_UP(msize),
                  lbase = FILE_BLOCK_ROUND_UP(length);

    unsigned long block = 0;

    struct sufs_fidx_entry *idx = NULL;

    int i = 0;

    sufs_libfs_inode_write_lock(m);

    if (mbase == lbase) {
        m->data.file_data.size_ = length;
    } else if (mbase < lbase) {
        // printf("append file, mbase: %lx, lbase: %lx\n", mbase, lbase);
        unsigned long bcount = (lbase - mbase) / SUFS_PAGE_SIZE;

        bcount = (bcount + SUFS_FILE_BLOCK_PAGE_CNT - 1) &
                 ~(SUFS_FILE_BLOCK_PAGE_CNT - 1);

        sufs_libfs_cmd_append_file(m->ino_num, bcount, &block);

        mbase = mbase / SUFS_FILE_BLOCK_SIZE;

        for (i = 0; i < bcount; i += SUFS_FILE_BLOCK_PAGE_CNT) {

            idx = sufs_libfs_mnode_file_index_append(
                m, ((block + i) << SUFS_PAGE_SHIFT));
            
            LOG_FS("file fill index: %d, %lx\n", mbase + (i / SUFS_FILE_BLOCK_PAGE_CNT), idx->offset);
            sufs_libfs_mnode_file_fill_index(
                m, mbase + (i / SUFS_FILE_BLOCK_PAGE_CNT), (unsigned long)idx);
            unsigned long temp = sufs_libfs_mnode_file_get_page(m, mbase + (i / SUFS_FILE_BLOCK_PAGE_CNT));
            assert(temp == idx->offset);
        }

        m->data.file_data.size_ = length;
#if SUFS_LIBFS_RANGE_LOCK
        sufs_libfs_irange_lock_resize(&m->data.file_data.range_lock, length);
#endif
    } else {

        if (lbase == 0) {
            sufs_libfs_mnode_file_truncate_zero(m);
            return 0;
        }

        idx = sufs_libfs_mnode_file_get_idx(m, lbase / SUFS_FILE_BLOCK_SIZE);

        m->index_end = idx;
        m->data.file_data.size_ = length;

        struct sufs_libfs_fidx_entry_page *ipage =
            m->fidx_entry_page_head;
        struct sufs_libfs_fidx_entry_page *nxt_ipage = NULL;

        if (idx->offset != 0) {
            idx->offset = 0;
        }
        while (ipage != NULL) {
            nxt_ipage = ipage->next;
            unsigned long s = (unsigned long)(ipage->page_buffer);
            unsigned long e = s + SUFS_PAGE_SIZE;
            if ((unsigned long)idx >= s && (unsigned long)idx < e) {
                break;
            }
            sufs_libfs_unlink_fidx_entry_page(m, ipage);
            sufs_libfs_fidx_entry_page_fini(ipage);
            ipage = nxt_ipage;
        }
        sufs_libfs_cmd_truncate_file(m->ino_num, ipage->lba, ((unsigned long)idx - 
                    (unsigned long)ipage->page_buffer) / sizeof(struct sufs_fidx_entry));
    }

    sufs_libfs_inode_write_unlock(m);

    return 0;
}

// OK
int sufs_libfs_do_map_file(struct sufs_libfs_mnode *m, int writable) {
    int ret = 0;

    unsigned long index_offset = 0;

    while ((ret = sufs_libfs_cmd_map_file(m->ino_num, writable,
                                          &index_offset)) != 0) {
        if (ret == -EAGAIN)
            continue;
        else {
            fprintf(stderr, "map ion: %d writable: %d failed with %d\n",
                    m->ino_num, writable, ret);

            return ret;
        }
    }

    LOG_FS("inode: %d, index_offset is %lx\n", m->ino_num, index_offset);

    if (index_offset == 0) {
        m->fidx_entry_page_head = NULL;
    } else {
        sufs_libfs_link_fidx_entry_page(
            m, sufs_libfs_fidx_entry_page_init(index_offset));
    }

    if (writable)
        sufs_libfs_file_set_writable(m);

    // sufs_libfs_mfs_add_mapped_inode(m->ino_num);

    return 0;
}

// OK
/* upgrade the mapping from read only to writable */
int sufs_libfs_upgrade_file_map(struct sufs_libfs_mnode *m) {
    sufs_libfs_cmd_unmap_file(m->ino_num, m->parent_mnum);

    return sufs_libfs_do_map_file(m, 1);
}

// OK
int sufs_libfs_map_file(struct sufs_libfs_mnode *m, int writable) {
    int ret = 0;

    /*
     * BUG: Internally marks all the request for write, since our upgrade
     * function does not work.
     *
     * Remove the below line once we have figured out how to work with upgrade
     */
    writable = 1;

    if (sufs_libfs_file_is_mapped(m)) {
        LOG_FS("sufs_libfs_map_file:File %d is already mapped\n", m->ino_num);
        if (!writable)
            return 0;

        if (writable && sufs_libfs_file_mapped_writable(m))
            return 0;

        sufs_libfs_lock_file_mapping(m);

        ret = sufs_libfs_upgrade_file_map(m);

        sufs_libfs_unlock_file_mapping(m);

        return ret;
    } else {
        LOG_FS("sufs_libfs_map_file:File %d is not mapped\n", m->ino_num);
        sufs_libfs_lock_file_mapping(m);

        if (sufs_libfs_file_is_mapped(m)) {
            // WARN_FS("sufs_libfs_map_file:File is already mapped\n");
            // sufs_libfs_unlock_file_mapping(m);
            goto out;
        }

        if ((ret = sufs_libfs_do_map_file(m, writable)) != 0) {
            WARN_FS("sufs_libfs_map_file:Failed to map file\n");
            goto out;
        }

        if (m->type == SUFS_FILE_TYPE_REG) {
            LOG_FS("sufs_libfs_map_file:Building index for file: inode "
                   "%d\n",
                   m->ino_num);
            // fflush(stdout);
            sufs_libfs_file_build_index(m);
        } else {
            LOG_FS("sufs_libfs_map_file:Building index for directory: "
                   "inode %d\n",
                   m->ino_num);
            // fflush(stdout);
            sufs_libfs_mnode_dir_build_index(m);
        }
        
        sufs_libfs_bm_set_bit(sufs_libfs_inode_has_index, m->ino_num);

    out:
        sufs_libfs_unlock_file_mapping(m);

        return ret;
    }
}

// OK
struct sufs_fidx_entry *
sufs_libfs_get_fidx_entry(struct sufs_libfs_fidx_entry_page *pg, int inode) {
    if (pg == NULL) {
        LOG_FS("Empty index page! This will just happen for new files.\n");
        return NULL;
    }
    int ret;
    memset(pg->page_buffer, 0, SUFS_PAGE_SIZE);
    ret = sufs_libfs_cmd_read_fidx_page(inode, pg->lba, pg->page_buffer);

    if (ret < 0) {
        WARN_FS("Read inode from device failed\n");
        return NULL;
    }
    return (struct sufs_fidx_entry *)((unsigned long)pg->page_buffer);
}

// OK
// struct sufs_libfs_fidx_entry_page *
// sufs_libfs_fidx_entry_to_page(struct sufs_libfs_mnode *m,
//                               struct sufs_fidx_entry *idx) {
//     unsigned long addr = (unsigned long)idx;
//     struct sufs_libfs_fidx_entry_page *ipage = m->fidx_entry_page_head;
//     while (ipage != NULL) {
//         unsigned long s = (unsigned long)ipage->page_buffer;
//         unsigned long e = s + SUFS_PAGE_SIZE;
//         if (addr >= s && addr < e) {
//             return ipage;
//         }
//         ipage = ipage->next;
//     }
//     return NULL;
// }

// OK
void sufs_libfs_file_build_index(struct sufs_libfs_mnode *m) {
    struct sufs_fidx_entry *idx =
        sufs_libfs_get_fidx_entry(m->fidx_entry_page_head, m->ino_num);
    struct sufs_fidx_entry *idx_start = idx;
    m->index_start = idx;

    int idx_num = 0;

    if (idx == NULL) {
        goto out;
    }

#if 0
    printf("m->index_start is %lx\n", (unsigned long) m->index_start);
#endif

    while (idx->offset != 0) {
#if 0
    printf("idx is %lx\n", (unsigned long) idx);
#endif

        if ((unsigned long)idx < (unsigned long)idx_start + SUFS_PAGE_SIZE - sizeof(struct sufs_fidx_entry)) {
            sufs_libfs_mnode_file_fill_index(m, idx_num, (unsigned long)idx);

            idx_num++;
            idx++;
            m->data.file_data.size_ += SUFS_FILE_BLOCK_SIZE;
        } else {
            struct sufs_libfs_fidx_entry_page *fidx_buffer_next =
                sufs_libfs_fidx_entry_page_init(idx->offset);
            sufs_libfs_link_fidx_entry_page(m, fidx_buffer_next);

            idx = sufs_libfs_get_fidx_entry(fidx_buffer_next, m->ino_num);
            idx_start = idx;
        }
    }

out:
#if 0
    printf("index_end is %lx\n", (unsigned long) idx);
#endif

    m->index_end = idx;
}
