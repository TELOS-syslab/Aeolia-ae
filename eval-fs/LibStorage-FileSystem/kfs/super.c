#include <linux/dax.h>
#include <linux/kernel.h>
#include <linux/memory.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/module.h>

#include "../include/common_inode.h"
#include "../include/kfs_config.h"
#include "balloc.h"
#include "inode.h"
#include "mmap.h"
#include "pagecache.h"
#include "super.h"
#include "util.h"

struct sufs_super_block sufs_sb;
int sufs_kfs_delegation = 0;

void sufs_init_sb(void) {
    // int cpus = num_online_cpus();
    int cpus = SUFS_MAX_CPU;
    memset(&sufs_sb, 0, sizeof(struct sufs_super_block));

    sufs_sb.cpus = cpus;
}

/* init file system related fields */

static void sufs_sb_sinode_clear(void) {
    unsigned long io_size;
    unsigned long sinode_size = 0;
    unsigned long tot_io_size = 0;
    unsigned long start_lba = sufs_sb.sinode_start;
    sinode_size = SUFS_MAX_INODE_NUM * sizeof(struct sufs_shadow_inode);
    unsigned long dma_buffer_va;
    dma_buffer_va =
        sufs_kfs_dma_buffer_acquire(sufs_dev_arr.dma_max_bytes / PAGE_SIZE, -1);
    LOG_FS("dma_buffer_va: %lx\n", dma_buffer_va);
    memset((void *)dma_buffer_va, 0, sufs_dev_arr.dma_max_bytes);
    while (sinode_size > 0) {
        io_size = sinode_size > sufs_dev_arr.dma_max_bytes
                      ? sufs_dev_arr.dma_max_bytes
                      : sinode_size;
        sufs_kfs_send_nvme_write(start_lba, io_size,
                                 dma_buffer_va_to_da(dma_buffer_va));
        sinode_size -= io_size;
        start_lba += io_size;
    }

    start_lba = sufs_sb.inode_bitmap_start;
    tot_io_size = sufs_sb.sinode_start - start_lba;

    while (tot_io_size > 0) {
        io_size = tot_io_size > sufs_dev_arr.dma_max_bytes
                      ? sufs_dev_arr.dma_max_bytes
                      : tot_io_size;
        sufs_kfs_send_nvme_write(start_lba, io_size,
                                 dma_buffer_va_to_da(dma_buffer_va));
        tot_io_size -= io_size;
        start_lba += io_size;
    }

    sufs_kfs_dma_buffer_release(dma_buffer_va,
                                sufs_dev_arr.dma_max_bytes / PAGE_SIZE);
}

static void sufs_init_root_inode(void) {

    sufs_kfs_set_inode(SUFS_ROOT_INODE, SUFS_FILE_TYPE_DIR, SUFS_ROOT_PERM, 0,
                       0, 0);
}

/*
 * One page superblock
 * Multiple pages for shadow inode
 * One extra page for root inode
 */
static void sufs_sb_fs_init(void) {
    // unsigned long head_block_bytes = 0;

    sufs_sb.inode_bitmap_start = sufs_sb.start_virt_addr + SUFS_SUPER_PAGE_SIZE;
    sufs_sb.block_bitmap_start = sufs_sb.inode_bitmap_start +
                                 FS_INODE_BITMAP_LOGICAL_BLOCKS * FS_BLOCK_SIZE;
    sufs_sb.sinode_start = sufs_sb.block_bitmap_start +
                           FS_FREE_BITMAP_LOGICAL_BLOCKS * FS_BLOCK_SIZE;

    sufs_sb.data_start = FS_HEAD_BYTES;

    LOG_FS("sinode_start: %lx\n", sufs_sb.sinode_start);
    // LCDFIXME: do we need to clear all blocks in device?
    sufs_sb_sinode_clear();
    LOG_FS("sufs_sb_sinode_clear done!\n");

    sufs_init_inode_free_list(&sufs_sb);
    LOG_FS("sufs_init_inode_free_list done!\n");

    sufs_init_block_free_list(&sufs_sb, 0);
    LOG_FS("sufs_init_block_free_list done!\n");
}

// LCD: we can open device in usermode directly.
int sufs_mount(void) { return 0; }

int sufs_umount(unsigned long addr) {
    int ret = 0;

    return ret;
}

/* TODO: add a macro to enable/disable debugging code */
long sufs_debug_read() { return 0; }

/* Format the file system */
int sufs_fs_init(void) {
    int ret = 0;

    // if ((ret = sufs_kfs_lease_init()) != 0)
    //     goto fail;

    // if ((ret = sufs_kfs_read_cache_init()) != 0)
    //     goto fail_read_cache;

    if ((ret = sufs_init_rangenode_cache()) != 0)
        goto fail_rangenode;

    if ((ret = sufs_alloc_inode_free_list(&sufs_sb)) != 0)
        goto fail_inode_free_list;

    if ((ret = sufs_alloc_block_free_lists(&sufs_sb)) != 0)
        goto fail_block_free_lists;

    LOG_FS("sufs_fs_init prep done!\n");

    sufs_sb_fs_init();
    LOG_FS("sufs_fs_init fs done!\n");

    sufs_init_root_inode();
    LOG_FS("sufs_fs_init root done!\n");

    return 0;

fail_block_free_lists:
    sufs_free_inode_free_list(&sufs_sb);
fail_inode_free_list:
    sufs_free_rangenode_cache();

fail_rangenode:
    // sufs_kfs_lease_fini();
fail:
    return ret;
}

void sufs_fs_fini(void) {

    sufs_delete_block_free_lists(&sufs_sb);

    sufs_free_inode_free_list(&sufs_sb);

    sufs_free_rangenode_cache();

    return;
}
