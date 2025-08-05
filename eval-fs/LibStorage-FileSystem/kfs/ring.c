#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/memory.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "../include/kfs_config.h"
#include "ring.h"
#include "super.h"

/* Total ring size */
#define SUFS_RING_SIZE (SUFS_LEASE_RING_SIZE + SUFS_MAPPED_RING_SIZE)

int sufs_kfs_allocate_pages(unsigned long size, int node, unsigned long **kaddr,
                            struct page **kpage) {
    int order = 0;
    unsigned long base_pfn = 0;
    struct page *pg = NULL;

    order = get_order(size);

    pg = alloc_pages_node(node, GFP_KERNEL | __GFP_ZERO, order);

    if (!pg) {
        WARN_FS("alloc failed with size: %ld\n", size);
        return -ENOMEM;
    }

    base_pfn = page_to_pfn(pg);

    if (kaddr) {
        (*kaddr) = page_to_virt(pg);
    }

    if (kpage) {
        (*kpage) = pg;
    }

    return 0;
}

int sufs_kfs_mmap_pages_to_user(unsigned long addr, unsigned long size,
                                struct vm_area_struct *vma, int user_writable,
                                struct page *pg) {
    unsigned long i = 0, base_pfn = 0;
    pgprot_t prop;
    vm_fault_t rc;

    if (user_writable) {
        prop = vm_get_page_prot(VM_READ | VM_WRITE | VM_SHARED);
    } else {
        prop = vm_get_page_prot(VM_READ | VM_SHARED);
    }

    base_pfn = page_to_pfn(pg);

    for (i = 0; i < size / PAGE_SIZE; i++) {
        if ((rc = vmf_insert_pfn_prot(vma, addr + i * PAGE_SIZE, base_pfn + i,
                                      prop)) != VM_FAULT_NOPAGE) {
            WARN_FS("insert pfn root failed with rc: %x\n", rc);
            return -ENOENT;
        }
    }

    return 0;
}

static int sufs_kfs_mmap_ring(unsigned long addr, unsigned long size,
                              struct vm_area_struct *vma, int user_writable,
                              unsigned long **kaddr, struct page **kpage,
                              int node) {
    int ret = 0;

    struct page *page = NULL;

    if ((ret = sufs_kfs_allocate_pages(size, node, kaddr, &page)) != 0)
        return ret;

    if (kpage) {
        (*kpage) = page;
    }

    if ((ret = sufs_kfs_mmap_pages_to_user(addr, size, vma, user_writable,
                                           page)) != 0)
        return ret;

    return 0;
}

/*
 * Create ring buffers for the trust group at the specified address with the
 * specified size
 */
int sufs_kfs_create_ring(struct sufs_tgroup *tgroup) {
    int ret = 0;
    struct vm_area_struct *vma = NULL;

    ret = vm_mmap(NULL, SUFS_RING_ADDR, SUFS_RING_SIZE, PROT_READ | PROT_WRITE,
                  MAP_SHARED | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, 0);

    if (ret < 0)
        return ret;

    vma = find_vma(current->mm, SUFS_RING_ADDR);

    /*  Makes vmf_insert_pfn_prot happy */
    /*  TODO: validate whether VM_PFNMAP has any side effect or not */
    vm_flags_set(vma, VM_PFNMAP);

    ret = sufs_kfs_mmap_ring(SUFS_LEASE_RING_ADDR, SUFS_LEASE_RING_SIZE, vma, 1,
                             &(tgroup->lease_ring_kaddr),
                             &(tgroup->lease_ring_pg), NUMA_NO_NODE);

    if (ret < 0)
        goto err;

    ret = sufs_kfs_mmap_ring(SUFS_MAPPED_RING_ADDR, SUFS_MAPPED_RING_SIZE, vma,
                             1, &(tgroup->map_ring_kaddr),
                             &(tgroup->map_ring_pg), NUMA_NO_NODE);

    if (ret < 0)
        goto err;

    return ret;

err:
    WARN_FS("vm_mmap failed!\n");
    vm_munmap(SUFS_RING_ADDR, SUFS_RING_SIZE);
    return -ENOMEM;
}

void sufs_kfs_delete_ring(struct sufs_tgroup *tgroup) {
    __free_pages(tgroup->lease_ring_pg, get_order(SUFS_LEASE_RING_SIZE));
    tgroup->lease_ring_kaddr = 0;
    tgroup->lease_ring_pg = NULL;

    __free_pages(tgroup->map_ring_pg, get_order(SUFS_MAPPED_RING_SIZE));
    tgroup->map_ring_kaddr = 0;
    tgroup->map_ring_pg = NULL;

    vm_munmap(SUFS_RING_ADDR, SUFS_RING_SIZE);
}
