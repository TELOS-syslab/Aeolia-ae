#include <assert.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>

#include "./include/bravo.h"
#include "./include/cmd.h"
#include "./include/fsop.h"
#include "./include/libfs-headers.h"
#include "./include/logger.h"
#include "./include/super.h"
#include "./include/tls.h"
#include "./include/trampoline.h"
#include "./include/trusted-api.h"
#include "./include/trusted-states.h"
#include "./include/util.h"
#include "./include/journal.h"

int (*tfs_orig_open)(const char *, int, ...) = NULL;
int (*tfs_close)(int) = NULL;

struct tfs_state *tfs_state = NULL;

static void tfs_init_states(void) {
    unsigned int authority_table_list_cnt = 0;
    int i;

    tfs_state = (struct tfs_state *)malloc(sizeof(struct tfs_state));

    // init mpk
    tfs_state->pkey = allocate_pkey();
    if (tfs_state->pkey < 0) {
        ERR_DEBUG("Cannot allocate pkey\n");
        abort();
    }
    tfs_state->out_trust = __rdpkru();
    tfs_state->in_trust = tfs_state->out_trust & ~(3 << (tfs_state->pkey * 2));
    enter_protected_region(tfs_state->in_trust);
    // init authority table,  16MB
    tfs_state->authority_table_bytes =
        KERNEL_PAGE_ALIGN(FS_TOTAL_BYTES / (4096 * 2));
    tfs_state->authority_table_per_list_bytes = 4 * 1024 * 1024;
    authority_table_list_cnt = (tfs_state->authority_table_bytes +
                                tfs_state->authority_table_per_list_bytes - 1) /
                               tfs_state->authority_table_per_list_bytes;
    for (i = 0; i < authority_table_list_cnt; i++) {
        tfs_state->authority_table_list[i] =
            (unsigned long *)malloc(tfs_state->authority_table_per_list_bytes);
        memset(tfs_state->authority_table_list[i], 0,
               tfs_state->authority_table_per_list_bytes);

        protect_buffer_with_pkey(tfs_state->authority_table_list[i],
                                 tfs_state->authority_table_per_list_bytes,
                                 tfs_state->pkey);
    }
    for (; i < 8; i++) {
        tfs_state->authority_table_list[i] = NULL;
    }
    pthread_spin_init(&tfs_state->authority_table_lock, PTHREAD_PROCESS_SHARED);

    // init tls
    tfs_tls_state = (struct tfs_tls_state *)malloc(
        sizeof(struct tfs_tls_state) * SUFS_MAX_THREADS);

    tfs_state->super_block =
        (struct tfs_super_block *)malloc(sizeof(struct tfs_super_block));
    
    for (i=0; i<64; i++){
        tfs_init_tls(i);
    }
}

static void tfs_fini_states(void) {
    int i;

    for (i = 0; i < 8; i++) {
        if (tfs_state->authority_table_list[i]) {
            free(tfs_state->authority_table_list[i]);
            tfs_state->authority_table_list[i] = NULL;
        }
    }
    pthread_spin_destroy(&tfs_state->authority_table_lock);
    if (tfs_state->pkey >= 0) {
        free_pkey(tfs_state->pkey);
        tfs_state->pkey = -1;
    }

    if (tfs_tls_state) {
        free(tfs_tls_state);
        tfs_tls_state = NULL;
    }

    if (tfs_state->super_block) {
        free(tfs_state->super_block);
        tfs_state->super_block = NULL;
    }

    if (tfs_state) {
        free(tfs_state);
        tfs_state = NULL;
    }
}

int __tfs_init(void) {
    int ret = 0;

    // tfs_orig_open = dlsym(RTLD_NEXT, "open");
    // LOG_INFO("TFS open %p\n", tfs_orig_open);
    // tfs_close = dlsym(RTLD_NEXT, "close");
    // LOG_INFO("TFS close %p\n", tfs_close);

    // abort();

    LOG_INFO("TFS init\n");
    tfs_cmd_init();
    LOG_INFO("TFS cmd init\n");
    tfs_init_states();
    LOG_INFO("TFS state init\n");
    tfs_init_sb(tfs_state->super_block);
    LOG_INFO("TFS super block init\n");
    tfs_inodec_init();
    LOG_INFO("TFS inode cache init\n");
    tfs_init_global_rglock_bravo();
    exit_protected_region(tfs_state->out_trust);

    return ret;
}

__attribute__((destructor)) int __tfs_fini(void) {

    tfs_commit_and_checkpoint_journal(tfs_state->super_block->journal);

    tfs_free_global_rglock_bravo();

    tfs_inodec_fini();

    tfs_fini_sb(tfs_state->super_block);

    tfs_fini_states();

    tfs_cmd_fini();

    return 0;
}

static int __tfs_mmap_file(unsigned long arg) {
    struct tfs_map_arg *map_arg = (struct tfs_map_arg *)arg;
    int ret = 0;
    LOG_INFO("mmap file , inode: %d, offset: %lx\n",
             map_arg->inode, map_arg->index_offset);
    ret =
        tfs_do_mmap_file(map_arg->inode, map_arg->perm, &map_arg->index_offset);
    return ret;
}
static int __tfs_unmap_file(unsigned long arg) {
    struct tfs_map_arg *map_arg = (struct tfs_map_arg *)arg;
    int ret = 0;
    LOG_INFO("unmap file , inode: %d, offset: %lx\n", 
             map_arg->inode, map_arg->index_offset);
    ret = tfs_do_unmap_file(map_arg->inode);
    return ret;
}

static int __tfs_chown(unsigned long arg) {
    struct tfs_chown_arg *chown_arg = (struct tfs_chown_arg *)arg;
    int ret = 0;
    LOG_INFO("chown file , inode: %d, owner: %d, group: %d\n",
            chown_arg->inode, chown_arg->owner,
             chown_arg->group);
    ret = tfs_do_chown(chown_arg->inode, chown_arg->owner, chown_arg->group);
    return ret;
}
static int __tfs_chmod(unsigned long arg) {
    struct tfs_chmod_arg *chmod_arg = (struct tfs_chmod_arg *)arg;
    int ret = 0;
    LOG_INFO("chmod file , inode: %d, mode: %o\n", 
             chmod_arg->inode, chmod_arg->mode);
    ret = tfs_do_chmod(chmod_arg->inode, chmod_arg->mode);
    return ret;
}

static int __tfs_blk_read(unsigned long arg) {
    struct tfs_blk_io_arg *blk_io_arg = (struct tfs_blk_io_arg *)arg;
    int ret = 0;
    int i, tot_bfn;
    LOG_INFO("blk read %lx, count: %d\n", blk_io_arg->lba,
             blk_io_arg->count);
    tot_bfn = (blk_io_arg->count + SUFS_PAGE_SIZE - 1) / SUFS_PAGE_SIZE;
    for (i = 0; i < tot_bfn; i++) {
        if (!TFS_TEST_R((blk_io_arg->lba >> SUFS_PAGE_SHIFT) + i, tfs_state)) {
            LOG_ERROR("Read block %lx failed\n", (blk_io_arg->lba >> SUFS_PAGE_SHIFT) + i);
            return -1;
        }
    }
    LOG_INFO("start read block %lx\n", blk_io_arg->lba);
    ret =
        read_blk_async(tfs_tls_ls_nvme_qp(), blk_io_arg->lba, blk_io_arg->count,
                       blk_io_arg->buf, blk_io_arg->callback, blk_io_arg->data);
    return ret;
}

static int __tfs_blk_write(unsigned long arg) {
    struct tfs_blk_io_arg *blk_io_arg = (struct tfs_blk_io_arg *)arg;
    int ret = 0;
    int i, tot_bfn;
    LOG_INFO("blk write %lx, count: %d\n", blk_io_arg->lba,
             blk_io_arg->count);
    tot_bfn = (blk_io_arg->count + SUFS_PAGE_SIZE - 1) / SUFS_PAGE_SIZE;
    for (i = 0; i < tot_bfn; i++) {
        if (!TFS_TEST_W((blk_io_arg->lba >> SUFS_PAGE_SHIFT) + i, tfs_state)) {
            LOG_ERROR("Write block %lu failed\n", (blk_io_arg->lba >> SUFS_PAGE_SHIFT) + i);
            return -1;
        }
    }
    ret = write_blk_async(tfs_tls_ls_nvme_qp(), blk_io_arg->lba,
                          blk_io_arg->count, blk_io_arg->buf,
                          blk_io_arg->callback, blk_io_arg->data);
    return ret;
}

static int __tfs_blk_idle(unsigned long arg){
    block_idle(tfs_tls_ls_nvme_qp());
    return 0;
}

static int __tfs_create_dma_buffer(unsigned long arg) {
    struct tfs_dma_buffer_arg *dma_buffer_arg =
        (struct tfs_dma_buffer_arg *)arg;
    int ret = 0;
    LOG_INFO("create dma buffer %p, size: %lu\n", dma_buffer_arg->buf,
             dma_buffer_arg->size);
    ret = create_dma_buffer(tfs_tls_ls_nvme_dev(), dma_buffer_arg->buf,
                            dma_buffer_arg->size);
    return ret;
}

static int __tfs_delete_dma_buffer(unsigned long arg) {
    struct tfs_dma_buffer_arg *dma_buffer_arg =
        (struct tfs_dma_buffer_arg *)arg;
    int ret = 0;
    LOG_INFO("delete dma buffer %p\n", dma_buffer_arg->buf);
    ret = delete_dma_buffer(tfs_tls_ls_nvme_dev(), dma_buffer_arg->buf);
    return ret;
}

static int __tfs_read_fidx_page(unsigned long arg) {
    struct tfs_fidx_page_entry *fidx_page_entry =
        (struct tfs_fidx_page_entry *)arg;
    int ret = 0;
    LOG_INFO("read fidx page %d, lba: %lx\n", fidx_page_entry->inode,
             fidx_page_entry->lba);
    ret = tfs_do_read_fidx_page(fidx_page_entry->inode, fidx_page_entry->lba,
                                fidx_page_entry->page_buffer);
    return ret;
}

static int __tfs_read_dentry_page(unsigned long arg) {
    struct tfs_dentry_page_entry *dentry_page_entry =
        (struct tfs_dentry_page_entry *)arg;
    int ret = 0;
    LOG_INFO("read dentry page %d, lba: %lx\n", dentry_page_entry->inode,
             dentry_page_entry->lba);
    ret = tfs_do_read_dentry_page(dentry_page_entry->inode,
                                  dentry_page_entry->lba,
                                  dentry_page_entry->page_buffer);
    return ret;
}

static int __tfs_alloc_inode_in_directory(unsigned long arg) {
    struct tfs_alloc_inode_arg *alloc_inode_arg =
        (struct tfs_alloc_inode_arg *)arg;
    int ret = 0;
    LOG_INFO("alloc inode in directory %s, dir_inode: %d\n", alloc_inode_arg->name, alloc_inode_arg->pinode);
    ret = tfs_do_alloc_inode_in_directory(
        &(alloc_inode_arg->inode), alloc_inode_arg->pinode,
        alloc_inode_arg->name_len, alloc_inode_arg->type, alloc_inode_arg->mode,
        alloc_inode_arg->uid, alloc_inode_arg->gid, alloc_inode_arg->name);
    return ret;
}

static int __tfs_release_inode(unsigned long arg) {
    struct tfs_release_inode_arg *release_inode_arg =
        (struct tfs_release_inode_arg *)arg;
    int ret = 0;
    LOG_INFO("release inode %d, pinode: %d\n", release_inode_arg->inode,
             release_inode_arg->pinode);
    ret = tfs_do_release_inode(release_inode_arg->inode,
                               release_inode_arg->pinode, 1);
    return ret;
}

static int __tfs_sync(unsigned long arg) {
    int ret = 0;
    LOG_INFO("sync\n");
    ret = tfs_do_sync();
    return ret;
}

static int __tfs_append_file(unsigned long arg) {
    struct tfs_append_file_arg *append_file_arg =
        (struct tfs_append_file_arg *)arg;
    int ret = 0;
    LOG_INFO("append file %d, num_block: %d\n", append_file_arg->inode,
             append_file_arg->num_block);
    ret = tfs_do_append_file(append_file_arg->inode, append_file_arg->num_block,
                             append_file_arg->block);
    return ret;
}

static int __tfs_rename(unsigned long arg) {
    struct tfs_rename_arg *rename_arg = (struct tfs_rename_arg *)arg;
    int ret = 0;
    // LOG_INFO("rename file, inode: %d, new_inode: %d\n",
    //          rename_arg->old_dinode, rename_arg->new_dinode);
    ret = tfs_do_rename(rename_arg->old_finode, rename_arg->old_dinode,
                        rename_arg->new_finode, rename_arg->new_dinode,
                        rename_arg->new_name);
    return ret;
}

static int __tfs_truncate_inode(unsigned long arg) {
    struct tfs_truncate_inode_arg *truncate_inode_arg =
        (struct tfs_truncate_inode_arg *)arg;
    int ret = 0;
    LOG_INFO("truncate inode %d\n", truncate_inode_arg->inode);
    ret = tfs_do_truncate_inode(truncate_inode_arg->inode);
    return ret;
}

static int __tfs_truncate_file(unsigned long arg) {
    struct tfs_truncate_file_arg *truncate_file_arg =
        (struct tfs_truncate_file_arg *)arg;
    int ret = 0;
    LOG_INFO("truncate file %d, lba: %lx, offset: %d\n",
             truncate_file_arg->inode, truncate_file_arg->index_lba,
             truncate_file_arg->offset);
    ret = tfs_do_truncate_file(truncate_file_arg->inode,
                               truncate_file_arg->index_lba,
                               truncate_file_arg->offset);
    return ret;
}

int tfs_mmap_file(unsigned long arg) {
    return trampoline_call(__tfs_mmap_file, arg);
}
int tfs_unmap_file(unsigned long arg) {
    return trampoline_call(__tfs_unmap_file, arg);
}

int tfs_chown(unsigned long arg) { return trampoline_call(__tfs_chown, arg); }
int tfs_chmod(unsigned long arg) { return trampoline_call(__tfs_chmod, arg); }

int tfs_blk_read(unsigned long arg) {
    return trampoline_call(__tfs_blk_read, arg);
}
int tfs_blk_write(unsigned long arg) {
    return trampoline_call(__tfs_blk_write, arg);
}

int tfs_create_dma_buffer(unsigned long arg) {
    return trampoline_call(__tfs_create_dma_buffer, arg);
}

int tfs_delete_dma_buffer(unsigned long arg) {
    return trampoline_call(__tfs_delete_dma_buffer, arg);
}

int tfs_read_fidx_page(unsigned long arg) {
    return trampoline_call(__tfs_read_fidx_page, arg);
}

int tfs_read_dentry_page(unsigned long arg) {
    return trampoline_call(__tfs_read_dentry_page, arg);
}

int tfs_alloc_inode_in_directory(unsigned long arg) {
    return trampoline_call(__tfs_alloc_inode_in_directory, arg);
}

int tfs_release_inode(unsigned long arg) {
    return trampoline_call(__tfs_release_inode, arg);
}

int tfs_sync(void) { return trampoline_call(__tfs_sync, 0); }

int tfs_append_file(unsigned long arg) {
    return trampoline_call(__tfs_append_file, arg);
}
int tfs_truncate_inode(unsigned long arg) {
    return trampoline_call(__tfs_truncate_inode, arg);
}
int tfs_rename(unsigned long arg) { return trampoline_call(__tfs_rename, arg); }

int tfs_truncate_file(unsigned long arg) {
    return trampoline_call(__tfs_truncate_file, arg);
}

int tfs_init(void) {
    int ret = 0;

    ret = __tfs_init();
    if (ret < 0) {
        ERR_DEBUG("TFS init failed\n");
        return ret;
    }

    return ret;
}

int tfs_blk_idle() {
    return trampoline_call(__tfs_blk_idle, 0);
}