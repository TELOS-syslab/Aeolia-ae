#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "./include/cmd.h"
#include "./include/fsop.h"
#include "./include/ialloc.h"
#include "./include/journal.h"
#include "./include/radix_array.h"

struct tfs_inode_cache **tfs_inode_cache_array = NULL;

void tfs_inodec_init(void) {
    tfs_inode_cache_array = (struct tfs_inode_cache **)calloc(
        SUFS_MAX_INODE_NUM, sizeof(struct tfs_inode_cache *));
}

void tfs_inodec_fini(void) {
    if (tfs_inode_cache_array != NULL) {
        free(tfs_inode_cache_array);
    }
}

static int tfs_perm_check(struct tfs_shadow_inode *sinode, int write) {
    u32 uid = tfs_tls_state[tls_my_thread].uid;
    u32 gid = tfs_tls_state[tls_my_thread].gid;
    int ret = 0;
    /* owner */
    if (sinode->uid == uid) {
        if (sinode->mode & S_IRUSR) {
            ret |= TFS_READ;
            if (write && (sinode->mode & S_IWUSR))
                ret |= TFS_WRITE;
        }
    }
    /* group */
    else if (sinode->gid == gid) {
        if (sinode->mode & S_IRGRP) {
            ret |= TFS_READ;
            if (write && (sinode->mode & S_IWGRP))
                ret |= TFS_WRITE;
        }
    }
    /* others */
    else {
        if (sinode->mode & S_IROTH) {
            ret |= TFS_READ;
            if (write && (sinode->mode & S_IWOTH))
                ret |= TFS_WRITE;
        }
    }

    return ret;
}

static int tfs_build_fidx_and_authorize(struct tfs_inode_cache *ic,
                                        unsigned long perm) {
    int ret = 0;
    unsigned long index_lba =
        ((struct tfs_shadow_inode *)ic->sinode_buffer->buf->buf)->index_offset;
    int last = SUFS_PAGE_SIZE / sizeof(struct tfs_fidx_entry) - 1;

    ic->fidx_array =
        (struct tfs_radix_array *)malloc(sizeof(struct tfs_radix_array));
    tfs_init_radix_array(ic->fidx_array, sizeof(unsigned long),
                         ULONG_MAX / SUFS_PAGE_SIZE + 1, SUFS_PAGE_SIZE);

    if (index_lba == 0)
        return 0;
    LOG_INFO("tfs_build_fidx_and_authorize: index_lba %lx\n", index_lba);
    // build fidx
    if (ic->fidx_buffer_list_head == NULL) {
        LOG_INFO("fidx buffer is NULL\n");
        while (index_lba != 0) {
            LOG_INFO("tfs_build_fidx_and_authorize: build index_lba %lx\n", index_lba);
            struct tfs_fidx_buffer_list *fidx_buffer =
                tfs_fidx_buffer_init(index_lba, 1);
            if (fidx_buffer == NULL) {
                LOG_ERROR("failed to allocate fidx buffer\n");
                return -1;
            }
            tfs_fidx_link_buffer_list(ic, fidx_buffer);
            tfs_radix_array_find(ic->fidx_array, index_lba >> SUFS_PAGE_SHIFT,
                                 1, (unsigned long)fidx_buffer);
            index_lba = ((struct tfs_fidx_entry *)(fidx_buffer->fidx_buffer.buf
                                                       ->buf))[last]
                            .offset;
        }
    }

    // authorize
    struct tfs_fidx_buffer_list *fidx_buffer_list = ic->fidx_buffer_list_head;

    while (fidx_buffer_list != NULL) {
        TFS_SET_R((fidx_buffer_list->fidx_buffer.lba >> SUFS_PAGE_SHIFT),
                  tfs_state);

        struct tfs_fidx_entry *fidx_entry =
            (struct tfs_fidx_entry *)(fidx_buffer_list->fidx_buffer.buf->buf);

        for (int i = 0; i < SUFS_PAGE_SIZE / sizeof(struct tfs_fidx_entry) - 1;
             i++) {
            if (fidx_entry[i].offset == 0)
                break;
            LOG_INFO("tfs_build_fidx_and_authorize: authorize index %lx\n",
                        fidx_entry[i].offset);
            if (perm & TFS_READ) {
                TFS_SET_R((fidx_entry[i].offset >> SUFS_PAGE_SHIFT), tfs_state);
                TFS_SET_R((fidx_entry[i].offset >> SUFS_PAGE_SHIFT)+1, tfs_state);
                TFS_SET_R((fidx_entry[i].offset >> SUFS_PAGE_SHIFT)+2, tfs_state);
                TFS_SET_R((fidx_entry[i].offset >> SUFS_PAGE_SHIFT)+3, tfs_state);
                TFS_SET_R((fidx_entry[i].offset >> SUFS_PAGE_SHIFT)+4, tfs_state);
                TFS_SET_R((fidx_entry[i].offset >> SUFS_PAGE_SHIFT)+5, tfs_state);
                TFS_SET_R((fidx_entry[i].offset >> SUFS_PAGE_SHIFT)+6, tfs_state);
                TFS_SET_R((fidx_entry[i].offset >> SUFS_PAGE_SHIFT)+7, tfs_state);
            }
            if (perm & TFS_WRITE) {
                TFS_SET_W((fidx_entry[i].offset >> SUFS_PAGE_SHIFT), tfs_state);
                TFS_SET_W((fidx_entry[i].offset >> SUFS_PAGE_SHIFT)+1, tfs_state);
                TFS_SET_W((fidx_entry[i].offset >> SUFS_PAGE_SHIFT)+2, tfs_state);
                TFS_SET_W((fidx_entry[i].offset >> SUFS_PAGE_SHIFT)+3, tfs_state);
                TFS_SET_W((fidx_entry[i].offset >> SUFS_PAGE_SHIFT)+4, tfs_state);
                TFS_SET_W((fidx_entry[i].offset >> SUFS_PAGE_SHIFT)+5, tfs_state);
                TFS_SET_W((fidx_entry[i].offset >> SUFS_PAGE_SHIFT)+6, tfs_state);
                TFS_SET_W((fidx_entry[i].offset >> SUFS_PAGE_SHIFT)+7, tfs_state);
            }
        }

        fidx_buffer_list = fidx_buffer_list->next;
    }
    return ret;
}

static int tfs_unauthorize_inode(struct tfs_inode_cache *ic) {
    int ret = 0;
    struct tfs_fidx_buffer_list *fidx_buffer_list = ic->fidx_buffer_list_head;

    while (fidx_buffer_list != NULL) {
        TFS_CLR_R((fidx_buffer_list->fidx_buffer.lba >> SUFS_PAGE_SHIFT),
                  tfs_state);

        struct tfs_fidx_entry *fidx_entry =
            (struct tfs_fidx_entry *)(fidx_buffer_list->fidx_buffer.buf->buf);

        for (int i = 0; i < SUFS_PAGE_SIZE / sizeof(struct tfs_fidx_entry) - 1;
             i++) {
            LOG_INFO("tfs_unauthorize_inode: clear index %lx\n",
                        fidx_entry[i].offset);
            if (fidx_entry[i].offset == 0)
                break;
            TFS_CLR_R((fidx_entry[i].offset >> SUFS_PAGE_SHIFT), tfs_state);
            TFS_CLR_W((fidx_entry[i].offset >> SUFS_PAGE_SHIFT), tfs_state);
        }

        fidx_buffer_list = fidx_buffer_list->next;
    }
    return ret;
}

static int tfs_init_dir_cache(struct tfs_inode_cache *inodec) {
    int ret = 0;
    int cpu, i;
    cpu = SUFS_MAX_CPU;
    struct tfs_dir_cache *dirc =
        (struct tfs_dir_cache *)malloc(sizeof(struct tfs_dir_cache));
    dirc->dentry_buffer_list_head = NULL;
    dirc->dentry_buffer_list_tail = NULL;

    dirc->dir_tails =
        (struct tfs_dir_tail *)malloc(sizeof(struct tfs_dir_tail) * cpu);
    for (i = 0; i < cpu; i++) {
        dirc->dir_tails[i].end_idx = NULL;
        dirc->dir_tails[i].dentry_buffer = NULL;
        pthread_spin_init(&(dirc->dir_tails[i].lock), PTHREAD_PROCESS_SHARED);
    }

    pthread_spin_init(&(dirc->index_lock), PTHREAD_PROCESS_SHARED);

    dirc->map_ =
        (struct tfs_chainhash *)malloc(sizeof(struct tfs_chainhash));
    tfs_chainhash_init(dirc->map_, 0);


    dirc->dcache_array =
        (struct tfs_chainhash *)malloc(sizeof(struct tfs_chainhash));
    tfs_chainhash_init(dirc->dcache_array, 0);

    inodec->dir_cache = dirc;
    return ret;
}

static int tfs_build_dir_cache(struct tfs_inode_cache *inodec) {
    int ret = 0;
    int i;
    struct tfs_dir_cache *dirc = inodec->dir_cache;
    struct tfs_fidx_buffer_list *fidx_buffer_list =
        inodec->fidx_buffer_list_head;
    struct tfs_fidx_entry *fidx_entry;
    int tot_idx = SUFS_PAGE_SIZE / sizeof(struct tfs_fidx_entry);
    while (fidx_buffer_list != NULL) {
        fidx_entry =
            (struct tfs_fidx_entry *)(fidx_buffer_list->fidx_buffer.buf->buf);
        for (i = 0; i < tot_idx - 1; i++) {
            if (fidx_entry[i].offset == 0)
                break;
            LOG_INFO("tfs_build_dir_cache: build dentry %lx\n",
                    fidx_entry[i].offset);
            struct tfs_dentry_buffer_list *dentry_buffer =
                tfs_dentry_buffer_init(fidx_entry[i].offset, 1);
            if (dentry_buffer == NULL) {
                LOG_ERROR("failed to allocate dentry buffer\n");
                return -1;
            }
            tfs_dentry_link_buffer_list(dirc, dentry_buffer);
            tfs_chainhash_insert(dirc->dcache_array, fidx_entry[i].offset,
                (unsigned long)dentry_buffer, 0, NULL);
        }
        fidx_buffer_list = fidx_buffer_list->next;
    }
    struct tfs_dentry_buffer_list *dentry_buffer =
        dirc->dentry_buffer_list_head;
    int first = 1;
    while (dentry_buffer != NULL) {
        struct tfs_dir_entry *dentry_entry =
            (struct tfs_dir_entry *)(dentry_buffer->dentry_buffer.buf->buf);
        while (dentry_entry->name_len != 0) {
            if (dentry_entry->ino_num != SUFS_INODE_TOMBSTONE) {
                struct tfs_dir_map_item *dir_map_item =
                    (struct tfs_dir_map_item *)malloc(
                        sizeof(struct tfs_dir_map_item));
                dir_map_item->dentry_buffer = dentry_buffer;
                dir_map_item->dentry_entry = dentry_entry;
                tfs_chainhash_insert(dirc->map_, dentry_entry->ino_num, 
                    (unsigned long)dir_map_item, 0, NULL);

                LOG_INFO("tfs_build_dir_cache: dentry %d %s\n",
                        dentry_entry->ino_num, dentry_entry->name);
            }
            dentry_entry =
                (struct tfs_dir_entry *)((unsigned long)dentry_entry +
                                         dentry_entry->rec_len);
        }
        if (first) {
            first = 0;
            int cpu = get_core_id_userspace();
            dirc->dir_tails[cpu].end_idx = dentry_entry;
            dirc->dir_tails[cpu].dentry_buffer = dentry_buffer;
        }
        dentry_buffer = dentry_buffer->next;
    }
    return ret;
}

int tfs_do_mmap_file(int ino, int writable, unsigned long *index_offset) {
    int ret = 0;
    unsigned long perm;
    struct tfs_shadow_inode *sinode;
    ret = tfs_cmd_map_file(ino, writable);
    if (ret < 0) {
        LOG_WARN("map file failed %d\n", ret);
        return ret;
    } else if (ret == 1) {
        // TODO: use shared cache
        LOG_INFO("should use shared cache %d\n", ret);
    }
    LOG_INFO("do mmap file: ino is %d\n", ino);

    if (tfs_inode_cache_array[ino] == NULL) {
        struct tfs_inode_cache *inode_cache =
            (struct tfs_inode_cache *)malloc(sizeof(struct tfs_inode_cache));
        tfs_build_inode_cache(inode_cache, ino, 1);
        tfs_inode_cache_array[ino] = inode_cache;
    }

    sinode = (struct tfs_shadow_inode *)tfs_inode_cache_array[ino]
                 ->sinode_buffer->buf->buf;

    perm = tfs_perm_check(sinode, writable);
    if (perm == 0) {
        LOG_WARN("permission denied %d\n", ino);
        return -1;
    }

    ret = tfs_build_fidx_and_authorize(tfs_inode_cache_array[ino], perm);
    if (ret < 0) {
        LOG_WARN("build fidx failed %d\n", ret);
        return ret;
    }

    if (sinode->file_type == SUFS_FILE_TYPE_DIR) {
        ret = tfs_init_dir_cache(tfs_inode_cache_array[ino]);
        if (ret < 0) {
            LOG_WARN("init dir cache failed %d\n", ret);
            return ret;
        }
        ret = tfs_build_dir_cache(tfs_inode_cache_array[ino]);
        if (ret < 0) {
            LOG_WARN("build dir cache failed %d\n", ret);
            return ret;
        }
    }

    *index_offset = ((struct tfs_shadow_inode *)tfs_inode_cache_array[ino]
                         ->sinode_buffer->buf->buf)
                        ->index_offset;

    return ret;
}

int tfs_do_unmap_file(int ino) {
    int ret = 0;
    if (tfs_inode_cache_array[ino] == NULL) {
        LOG_INFO("inode cache is NULL %d\n", ino);
        return 0;
    }

    ret = tfs_unauthorize_inode(tfs_inode_cache_array[ino]);
    return ret;
}

int tfs_do_chown(int ino, int uid, int gid) {
    int ret = 0;

    u32 cuid = tfs_tls_uid();
    if (cuid != 0) {
        LOG_WARN("permission denied %d\n", ino);
        return -1;
    }

    struct tfs_inode_cache *inode_cache = tfs_inode_cache_array[ino];
    if (inode_cache == NULL) {
        LOG_WARN("inode cache is NULL %d\n", ino);
        return -1;
    }

    struct tfs_shadow_inode *sinode =
        (struct tfs_shadow_inode *)inode_cache->sinode_buffer->buf->buf;
    tfs_begin_journal(tfs_state->super_block->journal);
    tfs_run_journal(
        tfs_state->super_block->journal, inode_cache->sinode_buffer->lba,
        inode_cache->sinode_buffer->size, inode_cache->sinode_buffer->buf);
    if (uid > 0) {
        sinode->uid = uid;
    }
    if (gid > 0) {
        sinode->gid = gid;
    }

    tfs_exit_journal(tfs_state->super_block->journal);

    return ret;
}

int tfs_do_chmod(int ino, unsigned int mode) {
    int ret = 0;
    int uid = tfs_tls_uid();
    struct tfs_inode_cache *inode_cache = tfs_inode_cache_array[ino];
    if (inode_cache == NULL) {
        LOG_WARN("inode cache is NULL %d\n", ino);
        return -1;
    }
    struct tfs_shadow_inode *sinode =
        (struct tfs_shadow_inode *)inode_cache->sinode_buffer->buf->buf;

    if (uid != 0 && uid != sinode->uid) {
        LOG_WARN("permission denied %d\n", ino);
        return -1;
    }
    tfs_begin_journal(tfs_state->super_block->journal);
    tfs_run_journal(
        tfs_state->super_block->journal, inode_cache->sinode_buffer->lba,
        inode_cache->sinode_buffer->size, inode_cache->sinode_buffer->buf);

    sinode->mode = mode;

    tfs_exit_journal(tfs_state->super_block->journal);
    return ret;
}

int tfs_do_read_fidx_page(int inode, unsigned long lba, void *buf) {
    int ret = 0;
    struct tfs_inode_cache *inode_cache = tfs_inode_cache_array[inode];
    if (inode_cache == NULL) {
        LOG_WARN("inode cache is NULL %d\n", inode);
        return -1;
    }
    struct tfs_fidx_buffer_list *fidx =
        (struct tfs_fidx_buffer_list *)tfs_radix_array_find(
            inode_cache->fidx_array, lba >> SUFS_PAGE_SHIFT, 0, 0);
    if (fidx == NULL) {
        LOG_WARN("fidx buffer is NULL %d, lba %lx\n", inode, lba);
        abort();
    }
    memcpy(buf, fidx->fidx_buffer.buf->buf, SUFS_PAGE_SIZE);
    return ret;
}

int tfs_do_read_dentry_page(int inode, unsigned long lba, void *buf) {
    int ret = 0;
    struct tfs_inode_cache *inode_cache = tfs_inode_cache_array[inode];
    if (inode_cache == NULL) {
        LOG_WARN("inode cache is NULL %d\n", inode);
        return -1;
    }
    unsigned long val1;
    struct tfs_dentry_buffer_list *dentry;
    if (!tfs_chainhash_lookup(
            inode_cache->dir_cache->dcache_array, lba, &val1, 0)) {
        LOG_WARN("dentry buffer is NULL %d\n", inode);
        return -1;
    }
    dentry = (struct tfs_dentry_buffer_list *)val1;
    memcpy(buf, dentry->dentry_buffer.buf->buf, SUFS_FILE_BLOCK_SIZE);
    return ret;
}

static int tfs_file_index_append(struct tfs_inode_cache *pinode_cache,
                                 unsigned long lba) {
    struct tfs_fidx_buffer_list *old_fidx_buffer =
        pinode_cache->fidx_buffer_list_head;
    struct tfs_fidx_entry *old_fidx_entry;
    if (old_fidx_buffer == NULL) {
        LOG_INFO("fidx buffer is NULL\n");
        unsigned long block = 0;
        tfs_new_blocks(tfs_state->super_block, 1, &block);
        if (block == 0) {
            LOG_ERROR("failed to allocate block %ld\n", block);
            return -1;
        }
        old_fidx_buffer =
            tfs_fidx_buffer_init(block * SUFS_PAGE_SIZE, 0);
        if (old_fidx_buffer == NULL) {
            LOG_ERROR("failed to allocate fidx buffer\n");
            return -1;
        }
        tfs_fidx_link_buffer_list(pinode_cache, old_fidx_buffer);
        tfs_radix_array_find(pinode_cache->fidx_array, block, 1,
                             (unsigned long)old_fidx_buffer);
        pinode_cache->old_fidx_entry =
            (struct tfs_fidx_entry *)old_fidx_buffer->fidx_buffer.buf->buf;
        struct tfs_sinode_buffer *sinode_buffer = pinode_cache->sinode_buffer;
        struct tfs_shadow_inode *sinode =
            (struct tfs_shadow_inode *)sinode_buffer->buf->buf;
        tfs_run_journal(tfs_state->super_block->journal, sinode_buffer->lba,
                        sinode_buffer->size, sinode_buffer->buf);
        LOG_INFO("tfs_file_index_append: index_offset %lx\n",
                block * SUFS_PAGE_SIZE);
        sinode->index_offset = block * SUFS_PAGE_SIZE;
    } else {
        if (pinode_cache->old_fidx_entry == NULL) {
            pinode_cache->old_fidx_entry =
                (struct tfs_fidx_entry *)old_fidx_buffer->fidx_buffer.buf->buf;
            while (pinode_cache->old_fidx_entry->offset != 0) {
                pinode_cache->old_fidx_entry++;
            }
        }
    }

    old_fidx_entry = pinode_cache->old_fidx_entry;
    if ((unsigned long)old_fidx_entry <
        (unsigned long)(old_fidx_buffer->fidx_buffer.buf->buf) +
            SUFS_PAGE_SIZE - sizeof(struct tfs_fidx_entry)) {
        tfs_run_journal(tfs_state->super_block->journal,
                        old_fidx_buffer->fidx_buffer.lba, SUFS_PAGE_SIZE,
                        old_fidx_buffer->fidx_buffer.buf);
        LOG_INFO("tfs_file_index_append: new offset lba %lx\n", lba);
        old_fidx_entry->offset = lba;
        old_fidx_entry++;
    } else {
        LOG_INFO("need to allocate new idx block\n");
        unsigned long block = 0;
        tfs_new_blocks(tfs_state->super_block, 1, &block);
        if (block == 0) {
            LOG_ERROR("failed to allocate block %ld\n", block);
            return -1;
        }
        LOG_INFO("new idx block %lx\n", block * SUFS_PAGE_SIZE);
        struct tfs_fidx_buffer_list *fidx_buffer =
            tfs_fidx_buffer_init(block * SUFS_PAGE_SIZE, 0);
        if (fidx_buffer == NULL) {
            LOG_ERROR("failed to allocate fidx buffer\n");
            return -1;
        }
        tfs_fidx_link_buffer_list(pinode_cache, fidx_buffer);
        tfs_radix_array_find(pinode_cache->fidx_array, block, 1,
                             (unsigned long)fidx_buffer);
        struct tfs_fidx_entry *fidx_entry =
            (struct tfs_fidx_entry *)fidx_buffer->fidx_buffer.buf->buf;

        tfs_run_journal(tfs_state->super_block->journal,
                        fidx_buffer->fidx_buffer.lba, SUFS_PAGE_SIZE,
                        fidx_buffer->fidx_buffer.buf);
        fidx_entry->offset = lba;

        tfs_run_journal(tfs_state->super_block->journal,
                        old_fidx_buffer->fidx_buffer.lba, SUFS_PAGE_SIZE,
                        old_fidx_buffer->fidx_buffer.buf);
        LOG_INFO("tfs_file_index_append: old offset offset %ld\n",
                (unsigned long)old_fidx_entry -
                    (unsigned long)(old_fidx_buffer->fidx_buffer.buf->buf));
        old_fidx_entry->offset = block * SUFS_PAGE_SIZE;
        old_fidx_entry = fidx_entry;
        old_fidx_entry++;
    }
    pinode_cache->old_fidx_entry = old_fidx_entry;
    LOG_INFO("Exit tfs_file_index_append\n");
    return 0;
}

static int tfs_dir_entry_insert(struct tfs_inode_cache *pinode_cache,
                                char *name, int name_len, int type, int mode,
                                int uid, int gid, int inode) {
    struct tfs_dir_cache *dir_cache = pinode_cache->dir_cache;
    struct tfs_dir_entry *dir;
    struct tfs_dentry_buffer_list *dentry_buffer;
    int cpu = get_core_id_userspace();
    int record_len = sizeof(struct tfs_dir_entry) + name_len;
    pthread_spin_lock(&(dir_cache->dir_tails[cpu].lock));
    dir = dir_cache->dir_tails[cpu].end_idx;
    dentry_buffer = dir_cache->dir_tails[cpu].dentry_buffer;
    LOG_INFO("tfs_dir_entry_insert: name %s, name_len %d, type %d, mode %d, uid %d, gid %d, inode %d\n",
             name, name_len, type, mode, uid, gid, inode);
    if ((dir == NULL) ||
        ((unsigned long)dir + record_len >
         (unsigned long)(dentry_buffer->dentry_buffer.buf->buf) +
             SUFS_FILE_BLOCK_SIZE)) {
        LOG_INFO("need to allocate new dentry block\n");
        unsigned long block_nr = 0;
        tfs_new_blocks(tfs_state->super_block, SUFS_FILE_BLOCK_PAGE_CNT,
                       &block_nr);
        if (block_nr == 0) {
            LOG_ERROR("failed to allocate block %ld\n", block_nr);
            pthread_spin_unlock(&(dir_cache->dir_tails[cpu].lock));
            return -1;
        }
        LOG_INFO("alloc block %ld\n", block_nr);

       dentry_buffer =
            tfs_dentry_buffer_init(block_nr * SUFS_PAGE_SIZE, 0);
        if (dentry_buffer == NULL) {
            LOG_ERROR("failed to allocate dentry buffer\n");
            return -1;
        }
        tfs_dentry_link_buffer_list(dir_cache, dentry_buffer);

        tfs_chainhash_insert(dir_cache->dcache_array, block_nr* SUFS_PAGE_SIZE,
            (unsigned long)dentry_buffer, 0, NULL);

        dir = (struct tfs_dir_entry *)dentry_buffer->dentry_buffer.buf->buf;

        pthread_spin_lock(&(dir_cache->index_lock));
        LOG_INFO("tfs_dir_entry_insert: dir inode ??, block_nr %lx\n", 
            block_nr * SUFS_PAGE_SIZE);
        tfs_file_index_append(pinode_cache, block_nr * SUFS_PAGE_SIZE);
        pthread_spin_unlock(&(dir_cache->index_lock));
    }

    dir_cache->dir_tails[cpu].end_idx =
        (struct tfs_dir_entry *)((unsigned long)dir + record_len);
    dir_cache->dir_tails[cpu].dentry_buffer = dentry_buffer;
    pthread_spin_unlock(&(dir_cache->dir_tails[cpu].lock));


    LOG_INFO("tfs_dir_entry_insert: dir %p, dentry_buffer %p\n", dir, dentry_buffer);

    struct tfs_dir_map_item *dir_map_item =
                (struct tfs_dir_map_item *)malloc(sizeof(struct tfs_dir_map_item));
    dir_map_item->dentry_buffer = dentry_buffer;
    dir_map_item->dentry_entry = dir;

    tfs_chainhash_insert(dir_cache->map_, inode, (unsigned long)dir_map_item,
                    0, NULL);

    tfs_run_journal(tfs_state->super_block->journal,
                    dentry_buffer->dentry_buffer.lba, SUFS_FILE_BLOCK_SIZE,
                    dentry_buffer->dentry_buffer.buf);
    LOG_INFO("build dir entry\n");
    strcpy(dir->name, name);
    dir->name_len = name_len;
    dir->ino_num = inode;
    dir->rec_len = record_len;
    dir->inode.file_type = type;
    dir->inode.mode = mode;
    dir->inode.uid = uid;
    dir->inode.gid = gid;
    dir->inode.size = 0;
    dir->inode.offset = 0;
    dir->inode.atime = 0;
    dir->inode.ctime = 0;
    dir->inode.mtime = 0;
    LOG_INFO("build end dir entry %p\n", dir);
    return 0;
}

int tfs_do_alloc_inode_in_directory(int *inode, int pinode, int name_len,
                                    int type, int mode, int uid, int gid,
                                    char *name) {
    int ret = 0;
    int ninode = 0;

    struct tfs_inode_cache *pinode_cache = tfs_inode_cache_array[pinode];
    if (pinode_cache == NULL) {
        LOG_WARN("pinode cache is NULL %d\n", pinode);
        return -1;
    }
    struct tfs_shadow_inode *pinode_sinode =
        (struct tfs_shadow_inode *)pinode_cache->sinode_buffer->buf->buf;
    if (pinode_sinode->file_type != SUFS_FILE_TYPE_DIR ||
        (tfs_perm_check(pinode_sinode, 1) & TFS_WRITE == 0)) {
        LOG_WARN("permission denied %d\n", pinode);
        return -1;
    }
    tfs_begin_journal(tfs_state->super_block->journal);
    ninode = tfs_new_inode(tfs_state->super_block, get_core_id_userspace());
    if (ninode < 0) {
        LOG_ERROR("failed to allocate inode %d\n", ninode);
        return -1;
    }
    LOG_INFO("tfs_do_alloc_inode_in_directory:alloc inode %d\n", ninode);
    tfs_dir_entry_insert(pinode_cache, name, name_len, type, mode, uid, gid,
                         ninode);
    struct tfs_inode_cache *inode_cache = tfs_inode_cache_array[ninode];
    if (inode_cache == NULL) {
        inode_cache =
            (struct tfs_inode_cache *)malloc(sizeof(struct tfs_inode_cache));
        tfs_build_inode_cache(inode_cache, ninode, 0);
        tfs_inode_cache_array[ninode] = inode_cache;
    }
    tfs_run_journal(
        tfs_state->super_block->journal, inode_cache->sinode_buffer->lba,
        inode_cache->sinode_buffer->size, inode_cache->sinode_buffer->buf);
    struct tfs_shadow_inode *sinode =
        (struct tfs_shadow_inode *)inode_cache->sinode_buffer->buf->buf;
    sinode->file_type = type;
    sinode->mode = mode;
    sinode->uid = uid;
    sinode->gid = gid;
    sinode->pinode = pinode;
    sinode->index_offset = 0;
    tfs_exit_journal(tfs_state->super_block->journal);
    *inode = ninode;
    
    return ret;
}

int tfs_do_release_inode(int ino, int pinode, int need_journal) {
    int ret = 0;
    struct tfs_inode_cache *inode_cache = tfs_inode_cache_array[ino];
    // LOG_WARN("tfs_do_release_inode: release inode %d, pinode %d\n", ino, pinode);
    if(need_journal)tfs_begin_journal(tfs_state->super_block->journal);

    // TODO: be lazy here

    if (inode_cache != NULL) {
        // TODO: free after journal
        // delete_dma_buffer(tfs_tls_ls_nvme_dev(), inode_cache->sinode_buffer->buf);
        // free(inode_cache->sinode_buffer->buf);
        free(inode_cache->sinode_buffer);
        inode_cache->sinode_buffer = NULL;

        tfs_radix_array_fini(inode_cache->fidx_array);
        free(inode_cache->fidx_array);
        inode_cache->fidx_array = NULL;

        struct tfs_fidx_buffer_list *fidx_buffer_list =
            inode_cache->fidx_buffer_list_head;

        while (fidx_buffer_list != NULL) {
            struct tfs_fidx_buffer_list *next = fidx_buffer_list->next;
            struct tfs_fidx_entry *fidx_entry =
                (struct tfs_fidx_entry *)(fidx_buffer_list->fidx_buffer.buf->buf);
            while (fidx_entry->offset != 0 &&
                ((unsigned long)fidx_entry <
                    (unsigned long)(fidx_buffer_list->fidx_buffer.buf->buf) +
                        SUFS_PAGE_SIZE - sizeof(struct tfs_fidx_entry))) {
                tfs_free_blocks(tfs_state->super_block,
                                fidx_entry->offset >> SUFS_PAGE_SHIFT,
                                SUFS_FILE_BLOCK_PAGE_CNT);
                fidx_entry++;
            }
            tfs_free_blocks(tfs_state->super_block,
                            fidx_buffer_list->fidx_buffer.lba >> SUFS_PAGE_SHIFT,
                            1);
            tfs_fidx_unlink_buffer_list(inode_cache, fidx_buffer_list);
            tfs_fidx_buffer_fini(fidx_buffer_list);
            fidx_buffer_list = next;
        }
        inode_cache->fidx_buffer_list_head = NULL;
        inode_cache->fidx_buffer_list_tail = NULL;
        inode_cache->old_fidx_entry = NULL;
        free(inode_cache);
        tfs_inode_cache_array[ino] = NULL;
    }


    struct tfs_inode_cache *pinode_cache = tfs_inode_cache_array[pinode];
    if (pinode_cache == NULL) {
        LOG_WARN("pinode cache is NULL %d\n", pinode);
        return -1;
    }
    struct tfs_dir_cache *dir_cache = pinode_cache->dir_cache;

    unsigned long val1;
    struct tfs_dir_map_item *dir_map_item;
    if (!tfs_chainhash_lookup(dir_cache->map_, ino, &val1, NULL)) {
        LOG_WARN("dir map item is NULL %d\n", ino);
        return -1;
    }

    dir_map_item = (struct tfs_dir_map_item *)val1;

    tfs_run_journal(tfs_state->super_block->journal,
                    dir_map_item->dentry_buffer->dentry_buffer.lba,
                    SUFS_FILE_BLOCK_SIZE,
                    dir_map_item->dentry_buffer->dentry_buffer.buf);
    dir_map_item->dentry_entry->ino_num = SUFS_INODE_TOMBSTONE;
    tfs_free_inode(tfs_state->super_block, ino);
    if(need_journal)tfs_exit_journal(tfs_state->super_block->journal);
    return ret;
}

int tfs_do_truncate_inode(int inode) {
    int ret = 0;
    struct tfs_inode_cache *inode_cache = tfs_inode_cache_array[inode];
    if (inode_cache == NULL) {
        LOG_WARN("inode cache is NULL %d\n", inode);
        return -1;
    }

    struct tfs_fidx_buffer_list *fidx_buffer_list =
        inode_cache->fidx_buffer_list_tail;

    if (fidx_buffer_list == NULL) {
        LOG_INFO("fidx buffer list is NULL\n");
        return 0;  
    }

    struct tfs_fidx_entry *fidx_entry =
        (struct tfs_fidx_entry *)(fidx_buffer_list->fidx_buffer.buf->buf);
    tfs_begin_journal(tfs_state->super_block->journal);
    inode_cache->old_fidx_entry = fidx_entry;
    while (((unsigned long)fidx_entry <
            (unsigned long)(fidx_buffer_list->fidx_buffer.buf->buf) +
                SUFS_PAGE_SIZE - sizeof(struct tfs_fidx_entry)) && fidx_entry->offset != 0) {
        LOG_INFO("tfs_do_truncate_inode: clear index %lx\n",
                    fidx_entry->offset);
        tfs_free_blocks(tfs_state->super_block,
                        fidx_entry->offset >> SUFS_PAGE_SHIFT,
                        SUFS_FILE_BLOCK_PAGE_CNT);
        fidx_entry++;
    }
    
    LOG_INFO("tfs_do_truncate_inode: free block end\n");

    tfs_run_journal(tfs_state->super_block->journal,
                    fidx_buffer_list->fidx_buffer.lba, SUFS_PAGE_SIZE,
                    fidx_buffer_list->fidx_buffer.buf);
    memset((void *)(fidx_buffer_list->fidx_buffer.buf->buf), 0, SUFS_PAGE_SIZE);

    fidx_buffer_list = fidx_buffer_list->prev;
    struct tfs_fidx_buffer_list * fidx_buffer_list_prev;
    while (fidx_buffer_list != NULL) {
        struct tfs_fidx_entry *fidx_entry =
            (struct tfs_fidx_entry *)(fidx_buffer_list->fidx_buffer.buf->buf);
        while (((unsigned long)fidx_entry <
                (unsigned long)(fidx_buffer_list->fidx_buffer.buf->buf) +
                    SUFS_PAGE_SIZE - sizeof(struct tfs_fidx_entry)) && fidx_entry->offset != 0) {
            tfs_free_blocks(tfs_state->super_block,
                            fidx_entry->offset >> SUFS_PAGE_SHIFT,
                            SUFS_FILE_BLOCK_PAGE_CNT);
            fidx_entry++;
        }
        tfs_free_blocks(tfs_state->super_block,
                        fidx_buffer_list->fidx_buffer.lba >> SUFS_PAGE_SHIFT,
                        1);
        fidx_buffer_list_prev = fidx_buffer_list->prev;
        tfs_fidx_unlink_buffer_list(inode_cache, fidx_buffer_list);
        tfs_fidx_buffer_fini(fidx_buffer_list);
        fidx_buffer_list = fidx_buffer_list_prev;
    }
    if (inode_cache->fidx_buffer_list_head !=
        inode_cache->fidx_buffer_list_tail) {
        LOG_ERROR("fidx buffer list head and tail are not equal\n");
        return -1;
    }
    tfs_exit_journal(tfs_state->super_block->journal);
    return ret;
}

int tfs_do_truncate_file(int inode, unsigned long index_lba, int offset){
    int ret = 0;
    struct tfs_inode_cache *inode_cache = tfs_inode_cache_array[inode];
    if (inode_cache == NULL) {
        LOG_WARN("inode cache is NULL %d\n", inode);
        return -1;
    }
    struct tfs_fidx_buffer_list *fidx =
        (struct tfs_fidx_buffer_list *)tfs_radix_array_find(
            inode_cache->fidx_array, index_lba >> SUFS_PAGE_SHIFT, 0, 0);

    if (fidx == NULL) {
        LOG_WARN("fidx buffer is NULL %d\n", inode);
        return -1;
    }
    struct tfs_fidx_entry *fidx_entry =
        &(((struct tfs_fidx_entry *)(fidx->fidx_buffer.buf->buf))[offset]);
    struct tfs_fidx_entry *first_entry = fidx_entry;
    struct tfs_fidx_entry *st_fidx_entry = fidx_entry;
    tfs_begin_journal(tfs_state->super_block->journal);
    inode_cache->old_fidx_entry = fidx_entry;
    while (fidx_entry->offset != 0 &&
           ((unsigned long)fidx_entry <
            (unsigned long)(fidx->fidx_buffer.buf->buf) +
                SUFS_PAGE_SIZE - sizeof(struct tfs_fidx_entry))) {
        LOG_INFO("tfs_do_truncate_file: clear index %lx\n",
                 fidx_entry->offset);
        tfs_free_blocks(tfs_state->super_block,
                        fidx_entry->offset >> SUFS_PAGE_SHIFT,
                        SUFS_FILE_BLOCK_PAGE_CNT);
        fidx_entry++;
    }
    LOG_INFO("tfs_do_truncate_file: free block end\n");
    tfs_run_journal(tfs_state->super_block->journal,
                    fidx->fidx_buffer.lba, SUFS_PAGE_SIZE,
                    fidx->fidx_buffer.buf);
    memset((void *)(st_fidx_entry), 0 , SUFS_PAGE_SIZE - offset * sizeof(struct tfs_fidx_entry));

    fidx = fidx->prev;
    struct tfs_fidx_buffer_list * fidx_prev;
    while (fidx != NULL) {
        struct tfs_fidx_entry *fidx_entry =
            (struct tfs_fidx_entry *)(fidx->fidx_buffer.buf->buf);
        while (fidx_entry->offset != 0 &&
               ((unsigned long)fidx_entry <
                (unsigned long)(fidx->fidx_buffer.buf->buf) +
                    SUFS_PAGE_SIZE - sizeof(struct tfs_fidx_entry))) {
            tfs_free_blocks(tfs_state->super_block,
                            fidx_entry->offset >> SUFS_PAGE_SHIFT,
                            SUFS_FILE_BLOCK_PAGE_CNT);
            fidx_entry++;
        }
        tfs_free_blocks(tfs_state->super_block,
                        fidx->fidx_buffer.lba >> SUFS_PAGE_SHIFT, 1);
        fidx_prev = fidx->prev;
        tfs_fidx_unlink_buffer_list(inode_cache, fidx);
        tfs_fidx_buffer_fini(fidx);
        fidx = fidx_prev;
    }
    if (inode_cache->fidx_buffer_list_head !=
        inode_cache->fidx_buffer_list_tail) {
        LOG_ERROR("fidx buffer list head and tail are not equal\n");
        return -1;
    }
    tfs_exit_journal(tfs_state->super_block->journal);
    return ret;
}

int tfs_do_sync(void) {
    tfs_commit_and_checkpoint_journal(tfs_state->super_block->journal);
    return 0;
}

int tfs_do_append_file(int inode, int num_block, unsigned long *block) {
    int ret = 0;
    int i;
    LOG_INFO("tfs_do_append_file: inode %d, num_block %d, blockp %p\n", inode,
             num_block, block);
    struct tfs_inode_cache *inode_cache = tfs_inode_cache_array[inode];
    if (inode_cache == NULL) {
        LOG_WARN("inode cache is NULL %d\n", inode);
        return -1;
    }
    struct tfs_shadow_inode *sinode =
        (struct tfs_shadow_inode *)inode_cache->sinode_buffer->buf->buf;

    if (sinode->file_type != SUFS_FILE_TYPE_REG ||
        (tfs_perm_check(sinode, 1) & TFS_WRITE == 0)) {
        LOG_WARN("permission denied %d\n", inode);
        return -1;
    }
    tfs_begin_journal(tfs_state->super_block->journal);
    tfs_new_blocks(tfs_state->super_block, num_block, block);
    LOG_INFO("tfs_do_append_file: after new block blockp %p\n", block);


    for (i = 0; i < num_block; i += SUFS_FILE_BLOCK_PAGE_CNT) {
        LOG_INFO("append file %d %lx\n", inode, (*block + (unsigned long)i) * SUFS_PAGE_SIZE);
        ret = tfs_file_index_append(inode_cache, (*block + (unsigned long)i) * SUFS_PAGE_SIZE);
        
        if (ret < 0) {
            LOG_WARN("append file failed %d\n", ret);
            return ret;
        }
    }
    LOG_INFO("tfs_do_append_file: append file %d %lx\n", inode,
             (*block));
    for(i = 0; i < num_block; i++) {
        TFS_SET_R((*block + (unsigned long)i) , tfs_state);
        TFS_SET_W((*block + (unsigned long)i) , tfs_state);
    }
    tfs_exit_journal(tfs_state->super_block->journal);
    return ret;
}

static int tfs_rename_lock(int old_dinode, int new_dinode){
    int pinode_array[100];
    struct tfs_inode_cache *pinode_cache = tfs_inode_cache_array[old_dinode];
    if(pinode_cache == NULL){
        LOG_WARN("pinode cache is NULL %d\n", old_dinode);
        return -1;
    }
    struct tfs_shadow_inode *sinode = (struct tfs_shadow_inode *)pinode_cache
                 ->sinode_buffer->buf->buf;
    int pinode = sinode->pinode;
    int i = 0;

    pinode_array[i++] = old_dinode;
    pinode_array[i++] = new_dinode;
    while(pinode != -1){
        if(i >= 100){
            LOG_WARN("pinode array is full %d\n", old_dinode);
            return -1;
        }
        pinode_array[i++] = pinode;
        struct tfs_inode_cache *pinode_cache = tfs_inode_cache_array[pinode];
        if(pinode_cache == NULL){
            LOG_WARN("pinode cache is NULL %d\n", pinode);
            return -1;
        }
        sinode = (struct tfs_shadow_inode *)pinode_cache->sinode_buffer->buf->buf;
        pinode = sinode->pinode;
    }

    pinode_cache = tfs_inode_cache_array[new_dinode];
    if(pinode_cache == NULL){
        LOG_WARN("pinode cache is NULL %d\n", new_dinode);
        return -1;
    }
    sinode = (struct tfs_shadow_inode *)pinode_cache->sinode_buffer->buf->buf;
    pinode = sinode->pinode;
    while(pinode != -1){
        if(i >= 100){
            LOG_WARN("pinode array is full %d\n", new_dinode);
            return -1;
        }
        pinode_array[i++] = pinode;
        struct tfs_inode_cache *pinode_cache = tfs_inode_cache_array[pinode];
        if(pinode_cache == NULL){
            LOG_WARN("pinode cache is NULL %d\n", pinode);
            return -1;
        }
        sinode = (struct tfs_shadow_inode *)pinode_cache->sinode_buffer->buf->buf;
        pinode = sinode->pinode;
    }

    //sort the pinode_array
    for(int j = 0; j < i - 1; j++){
        for(int k = j + 1; k < i; k++){
            if(pinode_array[j] > pinode_array[k]){
                int tmp = pinode_array[j];
                pinode_array[j] = pinode_array[k];
                pinode_array[k] = tmp;
            }
        }
    }

    //lock the pinode_array
    for(int j = 0; j < i; j++){
        if(j > 0 && pinode_array[j] == pinode_array[j-1]){
            continue; // skip duplicate pinodes
        }
        struct tfs_inode_cache *pinode_cache = tfs_inode_cache_array[pinode_array[j]];
        if(pinode_cache == NULL){
            LOG_WARN("pinode cache is NULL %d\n", pinode_array[j]);
            return -1;
        }
        if(pinode_array[j] == old_dinode || pinode_array[j] == new_dinode){
            // write lock
            pthread_rwlock_wrlock(&pinode_cache->rwlock);
        } else {
            // read lock
            pthread_rwlock_rdlock(&pinode_cache->rwlock);
        }
    }

    return 0;
}

static int tfs_rename_unlock(int old_dinode, int new_dinode){
        int pinode_array[100];
    struct tfs_inode_cache *pinode_cache = tfs_inode_cache_array[old_dinode];
    if(pinode_cache == NULL){
        LOG_WARN("pinode cache is NULL %d\n", old_dinode);
        return -1;
    }
    struct tfs_shadow_inode *sinode = (struct tfs_shadow_inode *)pinode_cache
                 ->sinode_buffer->buf->buf;
    int pinode = sinode->pinode;
    int i = 0;

    pinode_array[i++] = old_dinode;
    pinode_array[i++] = new_dinode;
    while(pinode != -1){
        if(i >= 100){
            LOG_WARN("pinode array is full %d\n", old_dinode);
            return -1;
        }
        pinode_array[i++] = pinode;
        struct tfs_inode_cache *pinode_cache = tfs_inode_cache_array[pinode];
        if(pinode_cache == NULL){
            LOG_WARN("pinode cache is NULL %d\n", pinode);
            return -1;
        }
        sinode = (struct tfs_shadow_inode *)pinode_cache->sinode_buffer->buf->buf;
        pinode = sinode->pinode;
    }

    pinode_cache = tfs_inode_cache_array[new_dinode];
    if(pinode_cache == NULL){
        LOG_WARN("pinode cache is NULL %d\n", new_dinode);
        return -1;
    }
    sinode = (struct tfs_shadow_inode *)pinode_cache->sinode_buffer->buf->buf;
    pinode = sinode->pinode;
    while(pinode != -1){
        if(i >= 100){
            LOG_WARN("pinode array is full %d\n", new_dinode);
            return -1;
        }
        pinode_array[i++] = pinode;
        struct tfs_inode_cache *pinode_cache = tfs_inode_cache_array[pinode];
        if(pinode_cache == NULL){
            LOG_WARN("pinode cache is NULL %d\n", pinode);
            return -1;
        }
        sinode = (struct tfs_shadow_inode *)pinode_cache->sinode_buffer->buf->buf;
        pinode = sinode->pinode;
    }

    //sort the pinode_array
    for(int j = 0; j < i - 1; j++){
        for(int k = j + 1; k < i; k++){
            if(pinode_array[j] > pinode_array[k]){
                int tmp = pinode_array[j];
                pinode_array[j] = pinode_array[k];
                pinode_array[k] = tmp;
            }
        }
    }

    //lock the pinode_array
    for(int j = 0; j < i; j++){
        if(j > 0 && pinode_array[j] == pinode_array[j-1]){
            continue; // skip duplicate pinodes
        }
        struct tfs_inode_cache *pinode_cache = tfs_inode_cache_array[pinode_array[j]];
        if(pinode_cache == NULL){
            LOG_WARN("pinode cache is NULL %d\n", pinode_array[j]);
            return -1;
        }
        pthread_rwlock_unlock(&pinode_cache->rwlock);
    }

    return 0;
}

static int tfs_rename_check(int old_finode, int new_dinode){
    struct tfs_shadow_inode * sinode = (struct tfs_shadow_inode *)tfs_inode_cache_array[new_dinode]
                 ->sinode_buffer->buf->buf;
    int pinode = sinode->pinode;

    while(pinode != -1){
        if(pinode == old_finode){
            LOG_WARN("rename check failed %d %d\n", old_finode, new_dinode);
            return -1;
        }

        struct tfs_inode_cache *pinode_cache = tfs_inode_cache_array[pinode];
        if(pinode_cache == NULL){
            LOG_WARN("pinode cache is NULL %d\n", pinode);
            return -1;
        }
        sinode = (struct tfs_shadow_inode *)pinode_cache->sinode_buffer->buf->buf;
        pinode = sinode->pinode;
    }

    return 0;
}


int tfs_do_rename(int old_finode, int old_dinode, int new_finode,
                  int new_dinode, char *new_name) {
    int ret = 0;

    struct tfs_inode_cache *old_dir_inode_cache =
        tfs_inode_cache_array[old_dinode];
    struct tfs_inode_cache *new_dir_inode_cache =
        tfs_inode_cache_array[new_dinode];
    if (old_dir_inode_cache == NULL ||
        new_dir_inode_cache == NULL) {
        LOG_WARN("inode cache is NULL %d %d %d\n", old_finode, old_dinode,
                 new_dinode);
        return -1;
    }
    // permission check
    if (tfs_perm_check((struct tfs_shadow_inode *)
                           old_dir_inode_cache->sinode_buffer->buf->buf,
                       1) &
        TFS_WRITE == 0) {
        LOG_WARN("permission denied %d\n", old_finode);
        return -1;
    }
    if (tfs_perm_check((struct tfs_shadow_inode *)
                           new_dir_inode_cache->sinode_buffer->buf->buf,
                       1) &
        TFS_WRITE == 0) {
        LOG_WARN("permission denied %d\n", new_dinode);
        return -1;
    }
    LOG_INFO("tfs_do_rename: old file inode %d, old dir inode %d, new dir inode %d\n",
             old_finode, old_dinode, new_dinode);

    struct tfs_dir_cache *dir_cache = old_dir_inode_cache->dir_cache;

    unsigned long val1;
    struct tfs_dir_map_item *dir_map_item;

    if (!tfs_chainhash_lookup(dir_cache->map_, old_finode, &val1, NULL)) {
        LOG_ERROR("dir map item is NULL %d\n", old_finode);
        return -1;
    }
    dir_map_item = (struct tfs_dir_map_item *)val1;

    if (old_finode == new_finode && dir_map_item->dentry_entry->name == new_name)
        return 0;
    struct tfs_inode_cache *old_finode_cache = tfs_inode_cache_array[old_finode];
    struct tfs_shadow_inode *sinode = (struct tfs_shadow_inode *)old_finode_cache
                 ->sinode_buffer->buf->buf;
    if(sinode->file_type == SUFS_FILE_TYPE_DIR){
        if (tfs_rename_check(old_finode, new_dinode) < 0) {
            LOG_WARN("rename check failed %d %d\n", old_finode, new_dinode);
            return -1;
        }
        if (tfs_rename_lock(old_finode, new_dinode) < 0) {
            LOG_WARN("rename lock failed %d %d\n", old_finode, new_dinode);
            return -1;
        }
    }
    
    tfs_begin_journal(tfs_state->super_block->journal);
    if (new_finode) {
        ret = tfs_do_release_inode(new_finode, new_dinode, 0);
        if (ret < 0) {
            LOG_WARN("release inode failed %d\n", ret);
            return ret;
        }
    }

    tfs_run_journal(tfs_state->super_block->journal,
                    dir_map_item->dentry_buffer->dentry_buffer.lba,
                    SUFS_FILE_BLOCK_SIZE,
                    dir_map_item->dentry_buffer->dentry_buffer.buf);
    dir_map_item->dentry_entry->ino_num = SUFS_INODE_TOMBSTONE;

    int name_len = strlen(new_name) + 1;

    LOG_INFO("tfs_do_rename: old file inode %d, old dir inode %d, new dir inode %d\n",
             old_finode, old_dinode, new_dinode);
    tfs_dir_entry_insert(new_dir_inode_cache, new_name, name_len,
            dir_map_item->dentry_entry->inode.file_type, dir_map_item->dentry_entry->inode.mode, 
            dir_map_item->dentry_entry->inode.uid, dir_map_item->dentry_entry->inode.gid, old_finode);
    tfs_exit_journal(tfs_state->super_block->journal);

    if(sinode->file_type == SUFS_FILE_TYPE_DIR){
        tfs_rename_unlock(old_finode, new_dinode);
    }
}