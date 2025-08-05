#ifndef SUFS_KFS_DEV_NVME_H_
#define SUFS_KFS_DEV_NVME_H_

#include "../include/kfs_config.h"
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/nvme.h>
#include <linux/pci.h>

struct sufs_dev_arr {
    struct pci_dev *pdev;
    struct nvme_dev *dev;
    struct nvme_ns *ns;

    unsigned long dma_buffer_start[SUFS_MAX_CPU];
    spinlock_t dma_buffer_lock[SUFS_MAX_CPU];
    unsigned long local_dma_buffer_bm[SUFS_MAX_CPU];

    dma_addr_t dma_addr[SUFS_MAX_CPU];
    unsigned long dma_max_bytes;
    unsigned long prp_list[SUFS_MAX_CPU];
    dma_addr_t prp_list_dma_addr[SUFS_MAX_CPU];
};

int sufs_init_dev(void);

#endif
