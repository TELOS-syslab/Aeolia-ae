#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>

#include "../include/common_inode.h"
#include "../include/libfs_config.h"
#include "./include/amd64.h"
#include "./include/atomic_util.h"
#include "./include/chainhash.h"
#include "./include/cmd.h"
#include "./include/mfs.h"
#include "./include/mnode.h"
#include "./include/random.h"
#include "./include/util.h"

struct sufs_libfs_mnode **sufs_libfs_mnode_array = NULL;

// OK
void sufs_libfs_mnodes_init(void) {
    sufs_libfs_mnode_array = (struct sufs_libfs_mnode **)calloc(
        SUFS_MAX_INODE_NUM, sizeof(struct sufs_libfs_mnode *));

    if (sufs_libfs_mnode_array == NULL) {
        fprintf(stderr, "allocate mnode array failed!\n");
        abort();
    }
}

void sufs_libfs_mnodes_fini(void) {
    if (sufs_libfs_mnode_array)
        free(sufs_libfs_mnode_array);
}

// OK
static struct sufs_libfs_mnode *
sufs_libfs_mfs_do_mnode_init(u8 type, int ino_num, int parent_mnum,
                             struct sufs_inode *inode) {
    struct sufs_libfs_mnode *m = NULL;

    m = malloc(sizeof(struct sufs_libfs_mnode));

    if (!m) {
        fprintf(stderr, "allocate mnode failed in %s", __func__);
        return NULL;
    }

    m->ino_num = ino_num;
    m->type = type;

    m->parent_mnum = parent_mnum;
    m->inode = inode;
    m->index_start = NULL;
    m->index_end = NULL;
    m->fidx_entry_page_head = NULL;
    m->fidx_entry_page_tail = NULL;

    sufs_libfs_inode_rwlock_init(m);

    switch (type) {
    case SUFS_FILE_TYPE_REG:
        sufs_libfs_mnode_file_init(m);
        break;

    case SUFS_FILE_TYPE_DIR:
        sufs_libfs_mnode_dir_init(m);
        break;

    default:
        fprintf(stderr, "unknown type in mnum %d\n", ino_num);
        abort();
    }

    return m;
}

// static void sufs_libfs_mnode_file_unmap(struct sufs_libfs_mnode *m);

// OK
struct sufs_libfs_mnode *sufs_libfs_mfs_mnode_init(u8 type, int ino_num,
                                                   int parent_mnum,
                                                   struct sufs_inode *inode) {

    struct sufs_libfs_mnode *mnode = NULL;

    /* This could happen when another trust group obtains the file */
    if (sufs_libfs_mnode_array[ino_num]) {
        /*
         * BUG: sometime this code is executed even without trust group,
         * comment it for now
         */

        /*
         * sufs_libfs_mnode_file_unmap(sufs_libfs_mnode_array[ino_num]);
         * sufs_libfs_mnode_free(sufs_libfs_mnode_array[ino_num]);
         */
    }

    mnode = sufs_libfs_mfs_do_mnode_init(type, ino_num, parent_mnum, inode);

    sufs_libfs_mnode_array[ino_num] = mnode;

    return mnode;
}

// OK
void sufs_libfs_mnode_dir_init(struct sufs_libfs_mnode *mnode) {
    int i = 0;
    mnode->data.dir_data.dentry_page_head = NULL;
    mnode->data.dir_data.dir_tails =
        malloc(sizeof(struct sufs_libfs_dir_tail) * SUFS_MAX_CPU);

    for (i = 0; i < SUFS_MAX_CPU; i++) {
        pthread_spin_init(&(mnode->data.dir_data.dir_tails[i].lock),
                          PTHREAD_PROCESS_SHARED);
        mnode->data.dir_data.dir_tails[i].end_idx = NULL;
    }

    pthread_spin_init(&(mnode->data.dir_data.index_lock),
                      PTHREAD_PROCESS_SHARED);

    sufs_libfs_chainhash_init(&mnode->data.dir_data.map_,
                              0);
}

struct sufs_dir_entry *sufs_libfs_get_dentry(struct sufs_libfs_dentry_page *dpg,
                                             int inode) {
    int ret;

    memset(dpg->page_buffer, 0, SUFS_FILE_BLOCK_SIZE);
    ret = sufs_libfs_cmd_read_dentry_page(inode, dpg->lba, dpg->page_buffer);
    if (ret < 0) {
        WARN_FS("Read dentry page from tfs failed\n");
        return NULL;
    }
    return (struct sufs_dir_entry *)((unsigned long)dpg->page_buffer);
}

struct sufs_libfs_dentry_page *
sufs_libfs_dentry_to_page(struct sufs_libfs_dentry_page *dpage,
                          struct sufs_dir_entry *idx) {
    unsigned long addr = (unsigned long)idx;
    struct sufs_libfs_dentry_page *dpg = dpage;
    while (dpg != NULL) {
        unsigned long s = (unsigned long)dpg->page_buffer;
        unsigned long e = s + SUFS_FILE_BLOCK_SIZE;
        if (addr >= s && addr < e) {
            return dpg;
        }
        dpg = dpg->next;
    }
    return NULL;
}

// OK
static int
sufs_libfs_mnode_dir_build_index_one_file_block(struct sufs_libfs_mnode *mnode,
                                                unsigned long offset) {
    LOG_FS("building dentry: ino: %d\n", mnode->ino_num);
    struct sufs_libfs_dentry_page *dpage = sufs_libfs_dentry_page_init(offset);

    struct sufs_dir_entry *dir = sufs_libfs_get_dentry(dpage, mnode->ino_num);
    struct sufs_dir_entry *dir_base = dir;

    LOG_FS("dir ino_num is %d, name_len: %d, rec_len: %d, offset: %lx, name: %s\n",  
           dir->ino_num, dir->name_len, dir->rec_len, dir->inode.offset, dir->name);

    int cpu = 0;

    while (dir->name_len != 0) {

        LOG_FS("dir is %lx, ino_num is %d, len: %d\n", (unsigned long)dir,
               dir->ino_num, dir->rec_len);
        LOG_FS("name is %s\n", dir->name);
        // fflush(stdout);

        if (dir->ino_num != SUFS_INODE_TOMBSTONE) {
            if (sufs_libfs_mfs_mnode_init(dir->inode.file_type, dir->ino_num,
                                          mnode->ino_num, &dir->inode) == NULL)
                return -ENOMEM;

            if (sufs_libfs_chainhash_insert(
                    &mnode->data.dir_data.map_, dir->name, SUFS_NAME_MAX,
                    dir->ino_num, (unsigned long)dir, NULL) == false)
                return -ENOMEM;
        }

        dir = (struct sufs_dir_entry *)((unsigned long)dir + dir->rec_len);

        if ((unsigned long)dir >=
            (unsigned long)dir_base + SUFS_FILE_BLOCK_SIZE) 
            break;
    }

    cpu = sufs_libfs_current_cpu();

    mnode->data.dir_data.dir_tails[cpu].end_idx = dir;
    sufs_libfs_link_dentry_page(mnode, dpage);
    return 0;
}

// OK
int sufs_libfs_mnode_dir_build_index(struct sufs_libfs_mnode *mnode) {
    // LCDTODO: DIR change
    LOG_FS("sufs_libfs_mnode_dir_build_index: mnode %d, build index\n", mnode->ino_num);
    struct sufs_fidx_entry *idx =
        sufs_libfs_get_fidx_entry(mnode->fidx_entry_page_head, mnode->ino_num);
    struct sufs_fidx_entry *idx_start = idx;
    mnode->index_start = idx;

#if 0
      printf("index_start is: %lx\n", idx);
#endif

    if (idx == NULL)
        goto out;

    while (idx->offset != 0) {
        if ((unsigned long)idx < (unsigned long)idx_start + SUFS_PAGE_SIZE - sizeof(struct sufs_fidx_entry)) {

            LOG_FS("building index:%p for offset: %lx\n", idx, idx->offset);
            // fflush(stdout);

            sufs_libfs_mnode_dir_build_index_one_file_block(mnode, idx->offset);
            idx++;
        } else {
            struct sufs_libfs_fidx_entry_page *fidx_buffer_next =
                sufs_libfs_fidx_entry_page_init(idx->offset);
            sufs_libfs_link_fidx_entry_page(mnode, fidx_buffer_next);

            idx = sufs_libfs_get_fidx_entry(fidx_buffer_next, mnode->ino_num);
            idx_start = idx;
        }
    }

out:

    mnode->index_end = idx;

    return 0;
}

// OK
struct sufs_libfs_mnode *
sufs_libfs_mnode_dir_lookup(struct sufs_libfs_mnode *mnode, char *name) {
    unsigned long ino = 0;

    if (strcmp(name, ".") == 0)
        return mnode;

    if (strcmp(name, "..") == 0)
        return sufs_libfs_mnode_array[mnode->parent_mnum];

    sufs_libfs_file_enter_cs(mnode);

#if 0
    printf("mnode %d map: %d\n", mnode->ino_num, sufs_libfs_file_is_mapped(mnode));
#endif

    if (sufs_libfs_map_file(mnode, 0) != 0) {
#if 0
        printf("Failed at sufs_libfs_map_file!\n");
#endif
        goto out;
    }

    sufs_libfs_chainhash_lookup(&mnode->data.dir_data.map_, name, SUFS_NAME_MAX,
                                &ino, NULL);

    if(!sufs_libfs_chainhash_lookup(&mnode->data.dir_data.map_, name, SUFS_NAME_MAX,
                                &ino, NULL)&& (mnode->ino_num == 2)){
        // DEBUG("sufs_libfs_mnode_dir_lookup: lookup failed, ino is %d, name is %s, pinode: %d\n", ino, name, mnode->ino_num);
                                }


    // else if(ino == 0) {
    //     DEBUG("sufs_libfs_mnode_dir_lookup: ino is %d, name is %s, pinode: %d\n", ino, name, mnode->ino_num);
    // }

#if 0
        printf("name is %s, ino is %ld!\n", name, ino);
#endif

out:
    sufs_libfs_file_exit_cs(mnode);
    return sufs_libfs_mnode_array[ino];
}

// OK
bool sufs_libfs_mnode_dir_enumerate(struct sufs_libfs_mnode *mnode, char *prev,
                                    char *name) {
    bool ret = false;
    if (strcmp(prev, "") == 0) {
        strcpy(name, ".");
        return true;
    }

    if (strcmp(prev, ".") == 0) {
        strcpy(name, "..");
        return true;
    }

    if (strcmp(prev, "..") == 0)
        prev = NULL;

    sufs_libfs_file_enter_cs(mnode);

    if (sufs_libfs_map_file(mnode, 0) != 0)
        goto out;

    ret = sufs_libfs_chainhash_enumerate(&mnode->data.dir_data.map_,
                                         SUFS_NAME_MAX, prev, name);

out:
    sufs_libfs_file_exit_cs(mnode);
    return ret;
}

// OK
__ssize_t sufs_libfs_mnode_dir_getdents(struct sufs_libfs_mnode *mnode,
                                        unsigned long *offset_ptr, void *buffer,
                                        size_t length) {
    __ssize_t ret = 0;

    sufs_libfs_file_enter_cs(mnode);

    if (sufs_libfs_map_file(mnode, 0) != 0)
        goto out;

    ret = sufs_libfs_chainhash_getdents(
        &mnode->data.dir_data.map_, SUFS_NAME_MAX, offset_ptr, buffer, length);

out:
    sufs_libfs_file_exit_cs(mnode);
    return ret;
}



// OK
bool sufs_libfs_mnode_dir_insert_create(struct sufs_libfs_mnode *mnode, char *name,
                                 int name_len, struct sufs_libfs_ch_item **item) {
    bool ret = false;
    LOG_FS("[libfs] sufs_libfs_mnode_dir_insert_create: name: %s, name_len: %d\n",
           name, name_len);

    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return false;

    sufs_libfs_file_enter_cs(mnode);

    if (sufs_libfs_map_file(mnode, 1) != 0) {

        goto out;
    }

    if (!sufs_libfs_chainhash_insert(&mnode->data.dir_data.map_, name,
                                     SUFS_NAME_MAX, 0, 0, item)) {

        goto out;
    }

    //     if (!sufs_libfs_mnode_dir_entry_insert(mnode, name, name_len, mf,
    //     &dir)) {
    // #if 0
    //         printf("dir insert failed!\n");
    //         fflush(stdout);
    // #endif
    //         goto out;
    //     }
//     dir = (struct sufs_dir_entry *)malloc(sizeof(struct sufs_dir_entry) +
//                                           name_len);
//     strcpy(dir->name, name);
//     dir->ino_num = mf->ino_num;
//     dir->rec_len = sizeof(struct sufs_dir_entry) + name_len;

//     item->val2 = (unsigned long)dir;
//     ret = true;

// #if 0
//     printf("insert complete: name:%s, item:%lx, val:%ld, val2:%ld\n", name, (unsigned long) item, item->val, item->val2);
// #endif

//     if (dirp) {
//         (*dirp) = dir;
//     }

out:
    sufs_libfs_file_exit_cs(mnode);
    return ret;
}



// OK
bool sufs_libfs_mnode_dir_insert(struct sufs_libfs_mnode *mnode, char *name,
                                 int name_len, struct sufs_libfs_mnode *mf,
                                 struct sufs_dir_entry **dirp) {
    bool ret = false;
    struct sufs_dir_entry *dir = NULL;
    struct sufs_libfs_ch_item *item = NULL;
    LOG_FS("[libfs] sufs_libfs_mnode_dir_insert: name: %s, name_len: %d\n",
           name, name_len);

    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return false;

    sufs_libfs_file_enter_cs(mnode);

    if (sufs_libfs_map_file(mnode, 1) != 0) {

        goto out;
    }

    if (!sufs_libfs_chainhash_insert(&mnode->data.dir_data.map_, name,
                                     SUFS_NAME_MAX, mf->ino_num, 0, &item)) {

        goto out;
    }

    //     if (!sufs_libfs_mnode_dir_entry_insert(mnode, name, name_len, mf,
    //     &dir)) {
    // #if 0
    //         printf("dir insert failed!\n");
    //         fflush(stdout);
    // #endif
    //         goto out;
    //     }
    dir = (struct sufs_dir_entry *)malloc(sizeof(struct sufs_dir_entry) +
                                          name_len);
    strcpy(dir->name, name);
    dir->ino_num = mf->ino_num;
    dir->rec_len = sizeof(struct sufs_dir_entry) + name_len;

    item->val2 = (unsigned long)dir;
    ret = true;

#if 0
    printf("insert complete: name:%s, item:%lx, val:%ld, val2:%ld\n", name, (unsigned long) item, item->val, item->val2);
#endif

    if (dirp) {
        (*dirp) = dir;
    }

out:
    sufs_libfs_file_exit_cs(mnode);
    return ret;
}

// OK
bool sufs_libfs_mnode_dir_remove(struct sufs_libfs_mnode *mnode, char *name) {
    bool ret = false;
    struct sufs_dir_entry *dir;

    // if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
    //     return false;

    // sufs_libfs_file_enter_cs(mnode);

    // if (sufs_libfs_map_file(mnode, 1) != 0)
    //     goto out;


    // DEBUG("core: %d, dir infor: dir inode: %d pino_num: %d, child name: %s, map_p: %p\n", get_core_id_userspace(), mnode->ino_num, mnode->parent_mnum,
    //       name, &mnode->data.dir_data.map_);
    if (!sufs_libfs_chainhash_remove(&mnode->data.dir_data.map_, name,
                                     SUFS_NAME_MAX, NULL,
                                     (unsigned long *)&dir)) {
        goto out;
    }
    // DEBUG("dir pointer is %p\n", dir);

    sufs_libfs_cmd_release_inode(dir->ino_num, mnode->ino_num);
    dir->ino_num = SUFS_INODE_TOMBSTONE;
    // struct sufs_libfs_dentry_page *dpage =
    //     sufs_libfs_dentry_to_page(mnode->data.dir_data.dentry_page_head,
    //     dir);
    // sufs_libfs_dentry_page_wb(dpage);
    // sufs_libfs_clwb_buffer(&(dir->ino_num), sizeof(dir->ino_num));
    // sufs_libfs_sfence();

    ret = true;

out:
    // sufs_libfs_file_exit_cs(mnode);
    LOG_FS("sufs_libfs_mnode_dir_remove success!\n");
    return ret;
}

// OK
bool sufs_libfs_mnode_dir_kill(struct sufs_libfs_mnode *mnode) {
    bool ret = false;
    sufs_libfs_file_enter_cs(mnode);

    if (sufs_libfs_map_file(mnode, 0) != 0)
        goto out;

    ret = sufs_libfs_chainhash_remove_and_kill(&mnode->data.dir_data.map_);
out:
    sufs_libfs_file_exit_cs(mnode);
    return ret;
}

struct sufs_libfs_mnode *
sufs_libfs_mnode_dir_exists(struct sufs_libfs_mnode *mnode, char *name) {
    unsigned long ret = 0;

    if (strcmp(name, ".") == 0)
        return mnode;

    if (strcmp(name, "..") == 0)
        return sufs_libfs_mnode_array[mnode->parent_mnum];

    sufs_libfs_file_enter_cs(mnode);

    if (sufs_libfs_map_file(mnode, 0) != 0) {
#if 0
        printf("Failed at sufs_libfs_map_file!\n");
#endif
        goto out;
    }

    sufs_libfs_chainhash_lookup(&mnode->data.dir_data.map_, name, SUFS_NAME_MAX,
                                &ret, NULL);
out:
    sufs_libfs_file_exit_cs(mnode);
    return sufs_libfs_mnode_array[ret];
}

static inline void
sufs_libfs_mnode_dir_force_delete(struct sufs_libfs_mnode *m) {
    sufs_libfs_chainhash_forced_remove_and_kill(&m->data.dir_data.map_);
}

// OK
/* return the inserted index */
struct sufs_fidx_entry *
sufs_libfs_mnode_file_index_append(struct sufs_libfs_mnode *m,
                                   unsigned long lba) {
    struct sufs_fidx_entry *ret = NULL;
    struct sufs_fidx_entry *idx = NULL;
    struct sufs_fidx_entry *idx_start = NULL;
    LOG_FS(
        "[libfs] sufs_libfs_mnode_file_index_append: m->ino: %d, addr: %lx\n",
        m->ino_num, lba);
    if (m->index_end == NULL) {
        if (m->index_start != NULL) {
            WARN_FS("index_end is NULL but index_start is not NULL\n");
            abort();
        }
        struct sufs_libfs_fidx_entry_page *fidx_buffer_next =
            sufs_libfs_fidx_entry_page_init(0);
        memset((void *)fidx_buffer_next->page_buffer, 0, SUFS_PAGE_SIZE);
        // ((struct sufs_fidx_entry *)(fidx_buffer_next->page_buffer))->offset = 0;
        sufs_libfs_link_fidx_entry_page(m, fidx_buffer_next);

        m->index_start = m->index_end =
            (struct sufs_fidx_entry *)m->fidx_entry_page_head->page_buffer;
    }

    idx = m->index_end;

#if 0
    printf("m->index_end is %lx\n", (unsigned long) m->index_end);
#endif

    if ((unsigned long)idx < (unsigned long)idx_start + SUFS_PAGE_SIZE - sizeof(struct sufs_fidx_entry)) {
        ret = idx;

#if 0
    printf("self: %lx, idx is %lx\n", pthread_self(), (unsigned long) idx);
    printf("self: %lx, idx->offset is %lx\n", pthread_self(), (unsigned long) idx->offset);
#endif
        idx->offset = lba;

        idx++;
    } else {
        struct sufs_fidx_entry *old_idx = NULL;

        old_idx = idx;

        struct sufs_libfs_fidx_entry_page *fidx_buffer_next =
            sufs_libfs_fidx_entry_page_init(0);
        memset((void *)fidx_buffer_next->page_buffer, 0, SUFS_PAGE_SIZE);
        // ((struct sufs_fidx_entry *)(fidx_buffer_next->page_buffer))->offset = 0;
        sufs_libfs_link_fidx_entry_page(m, fidx_buffer_next);

        idx = (struct sufs_fidx_entry *)fidx_buffer_next->page_buffer;
        idx->offset = lba;
        ret = idx;
        idx++;

        /*
         * Mark old_idx as the last step to guarantee atomicity
         * No need for barrier since we assume TSO
         */

        old_idx->offset = 1;
    }

    m->index_end = idx;

    return ret;
}

// OK
void sufs_libfs_mnode_file_resize_append(struct sufs_libfs_mnode *m,
                                         u64 newsize, unsigned long addr) {
    u64 oldsize = m->data.file_data.size_;
    struct sufs_fidx_entry *idx = NULL;
    LOG_FS("oldsize: %ld, newsize: %ld\n", oldsize, newsize);
    assert(FILE_BLOCK_ROUND_UP(oldsize) / SUFS_FILE_BLOCK_SIZE + 1 ==
           FILE_BLOCK_ROUND_UP(newsize) / SUFS_FILE_BLOCK_SIZE);

    idx = sufs_libfs_mnode_file_index_append(m, addr);

    sufs_libfs_mnode_file_fill_index(
        m, (FILE_BLOCK_ROUND_UP(newsize) / SUFS_FILE_BLOCK_SIZE - 1),
        (unsigned long)idx);

    m->data.file_data.size_ = newsize;
}

/*
 * if keep_first, do not remove the first offset page
 * otherwise, remove everything
 */
// OK
void sufs_libfs_mnode_file_index_delete(struct sufs_libfs_mnode *m,
                                        struct sufs_fidx_entry *idx,
                                        int keep_first) {
    int first = 1;

    struct sufs_libfs_fidx_entry_page *ipage = m->fidx_entry_page_tail;
    struct sufs_libfs_fidx_entry_page *nxt_ipage = NULL;

    if (idx == NULL)
        return;

    while (ipage != NULL) {
        nxt_ipage = ipage->prev;
        if (keep_first && first) {
            first = 0;
            // memset((void *)ipage->page_buffer, 0, ipage->size);
        } else {
            sufs_libfs_unlink_fidx_entry_page(m, ipage);
            sufs_libfs_fidx_entry_page_fini(ipage);
        }
        ipage = nxt_ipage;
    }

    return;
}

// OK
void sufs_libfs_mnode_file_truncate_zero(struct sufs_libfs_mnode *m) {
    struct sufs_fidx_entry *idx = NULL;

    sufs_libfs_mnode_file_index_delete(m, m->index_start, 1);

    sufs_libfs_cmd_truncate_inode(m->ino_num);
    m->index_end = m->index_start;

    idx = m->index_start;

    if (idx != NULL) {
        idx->offset = 0;
    }

    m->data.file_data.size_ = 0;
    sufs_libfs_mnode_file_free_page(m);
}

// OK
void sufs_libfs_mnode_file_delete(struct sufs_libfs_mnode *m) {

    sufs_libfs_mnode_file_index_delete(m, m->index_start, 0);

    if (m->type == SUFS_FILE_TYPE_REG) {
        sufs_libfs_mnode_file_free_page(m);
    } else {
        sufs_libfs_mnode_dir_free(m);
    }
}

// OK
void sufs_libfs_do_mnode_stat(struct sufs_libfs_mnode *m, struct stat *st) {
    unsigned int stattype = 0;
    int type = m->type;
    switch (type) {
    case SUFS_FILE_TYPE_REG:
        stattype = S_IFREG;
        break;
    case SUFS_FILE_TYPE_DIR:
        stattype = S_IFDIR;
        break;
    default:
        fprintf(stderr, "Unknown type %d\n", type);
    }

    st->st_dev = 0;
    st->st_ino = m->ino_num;

    st->st_mode = stattype | m->inode->mode;

    st->st_nlink = 1;

    st->st_uid = m->inode->uid;
    st->st_gid = m->inode->gid;
    st->st_rdev = 0;

    st->st_size = 0;

    if (sufs_libfs_file_is_mapped(m)) {
        if (type == SUFS_FILE_TYPE_REG)
            st->st_size = sufs_libfs_mnode_file_size(m);
    } else {
        st->st_size = (512 * 1024 * 1024);
        /* st->st_size = m->inode->size; */
    }

    st->st_blksize = st->st_size / SUFS_PAGE_SIZE;
    st->st_blocks = st->st_size / 512;

    /* TODO: get from inode */
    memset(&st->st_atim, 0, sizeof(struct timespec));
    memset(&st->st_mtim, 0, sizeof(struct timespec));
    memset(&st->st_ctim, 0, sizeof(struct timespec));
}

// OK
int sufs_libfs_mnode_stat(struct sufs_libfs_mnode *m, struct stat *st) {
    int ret = -1;

    struct sufs_libfs_mnode *parent = sufs_libfs_mnode_array[m->parent_mnum];

    sufs_libfs_file_enter_cs(parent);

    if (sufs_libfs_map_file(parent, 0) != 0)
        goto out;

    sufs_libfs_do_mnode_stat(m, st);
    ret = 0;

out:
    sufs_libfs_file_exit_cs(parent);

    return ret;
}
