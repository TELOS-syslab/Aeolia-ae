
#include <linux/kernel.h>
#include <linux/module.h>

#include "../include/kfs_config.h"
#include "dev_nvme.h"
#include "super.h"

struct sufs_dev_arr sufs_dev_arr;

// LCDFIXME: now only support nvme0n1 device
int sufs_init_dev(void) {
    int ret, cpus;
    unsigned long byte_size;
    struct pci_dev *pdev = NULL;
    struct nvme_dev *dev = NULL;
    struct nvme_ns *ns;

    ret = request_module("nvme");

    if (ret < 0) {
        return -1;
    }

    while ((pdev = pci_get_class(0x010802, pdev))) {
        dev = pci_get_drvdata(pdev);
        if (dev == NULL) {
            continue;
        }
        if (dev->ctrl.instance != 0) {
            continue;
        }
        sufs_dev_arr.pdev = pdev;
        sufs_dev_arr.dev = dev;
        list_for_each_entry(ns, &dev->ctrl.namespaces, list) {
            byte_size = ns->head->nuse << ns->head->lba_shift;
            sufs_dev_arr.ns = ns;
            break;
        }
    }

    // LCDFIXME: if there are bottlenecks, we can use a huge dma buffer.
    sufs_dev_arr.dma_max_bytes = sufs_dev_arr.dev->ctrl.max_hw_sectors
                                 << sufs_dev_arr.ns->head->lba_shift;
    LOG_FS("dma_max_bytes: %lu, pages: %lu\n", sufs_dev_arr.dma_max_bytes,
           sufs_dev_arr.dma_max_bytes / PAGE_SIZE);
    cpus = num_online_cpus();
    cpus = cpus > SUFS_MAX_CPU ? SUFS_MAX_CPU : cpus;

    for (int i = 0; i < cpus; i++) {
        sufs_dev_arr.dma_buffer_start[i] = (unsigned long)dma_alloc_coherent(
            &(sufs_dev_arr.pdev->dev), sufs_dev_arr.dma_max_bytes,
            &sufs_dev_arr.dma_addr[i], GFP_KERNEL);
        if (!sufs_dev_arr.dma_buffer_start[i]) {
            WARN_FS("dma_buffer_start[%d] is NULL\n", i);
            return -1;
        }

        sufs_dev_arr.prp_list[i] = (unsigned long)dma_alloc_coherent(
            &(sufs_dev_arr.pdev->dev), SUFS_PAGE_SIZE,
            &sufs_dev_arr.prp_list_dma_addr[i], GFP_KERNEL);
        if (!sufs_dev_arr.prp_list[i]) {
            WARN_FS("prp_list[%d] is NULL\n", i);
            return -1;
        }
    }
    memset(sufs_dev_arr.local_dma_buffer_bm, 0, sizeof(unsigned long) * cpus);
    for (int i = 0; i < cpus; i++) {
        spin_lock_init(&sufs_dev_arr.dma_buffer_lock[i]);
    }

    return 0;
}
