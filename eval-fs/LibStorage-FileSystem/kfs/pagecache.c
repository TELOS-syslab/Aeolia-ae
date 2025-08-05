
#include <linux/kernel.h>
#include <linux/memory.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "pagecache.h"

void **sufs_read_cache_chunk_list = NULL;
static int sufs_read_cache_chunk_list_idx = 0;

int sufs_kfs_expand_read_cache(void) {
    if (sufs_read_cache_chunk_list_idx >= LSFS_READ_PAGE_CACHE_CHUNK_LIST_MAX) {
        WARN_FS(KERN_ERR "sufs_kfs_expand_read_cache: read cache is full\n");
        return -ENOMEM;
    }
    sufs_read_cache_chunk_list[sufs_read_cache_chunk_list_idx] =
        kvzalloc(LSFS_READ_PAGE_CACHE_CHUNK_SIZE, GFP_KERNEL);
    if (!sufs_read_cache_chunk_list[sufs_read_cache_chunk_list_idx]) {
        WARN_FS(KERN_ERR
                "sufs_kfs_expand_read_cache: failed to allocate memory\n");
        return -ENOMEM;
    }
    sufs_read_cache_chunk_list_idx++;
    return sufs_kfs_mmap_read_cache();
}

int sufs_kfs_mmap_read_cache(void) {
    int ret = 0;
    struct vm_area_struct *vma = NULL;
    unsigned long i_addr, i;
    unsigned long pfn;
    pgprot_t prop;
    vm_fault_t rc;
    int last_idx = sufs_read_cache_chunk_list_idx - 1;
    unsigned long size = LSFS_READ_PAGE_CACHE_CHUNK_SIZE;
    unsigned long addr =
        LSFS_READ_PAGE_CACHE_ADDR + last_idx * LSFS_READ_PAGE_CACHE_CHUNK_SIZE;

    ret = vm_mmap(NULL, addr, size, PROT_READ | PROT_WRITE,
                  MAP_SHARED | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, 0);
    if (ret < 0)
        return ret;

    prop = vm_get_page_prot(VM_READ | VM_WRITE | VM_SHARED);
    vma = find_vma(current->mm, addr);
    vm_flags_set(vma, VM_PFNMAP);

    for (i_addr = (unsigned long)sufs_read_cache_chunk_list[last_idx], i = 0;
         i_addr < (unsigned long)sufs_read_cache_chunk_list[last_idx] + size;
         i_addr += PAGE_SIZE, i++) {
        pfn = vmalloc_to_pfn((void *)i_addr);

        if ((rc = vmf_insert_pfn_prot(vma, addr + i * PAGE_SIZE, pfn, prop)) !=
            VM_FAULT_NOPAGE) {
            WARN_FS("insert pfn root failed with rc: %x\n", rc);
            return -ENOENT;
        }
    }
    return ret;
}

int sufs_kfs_munmap_read_cache(void) { return 0; }

int sufs_kfs_read_cache_init(void) {
    sufs_read_cache_chunk_list = kvzalloc(
        LSFS_READ_PAGE_CACHE_CHUNK_LIST_MAX * sizeof(void *), GFP_KERNEL);
    if (!sufs_read_cache_chunk_list) {
        WARN_FS(KERN_ERR
                "sufs_kfs_read_cache_init: failed to allocate memory\n");
        return -ENOMEM;
    }
    sufs_read_cache_chunk_list_idx = 0;
    sufs_read_cache_chunk_list[sufs_read_cache_chunk_list_idx] =
        kvzalloc(LSFS_READ_PAGE_CACHE_CHUNK_SIZE, GFP_KERNEL);
    if (!sufs_read_cache_chunk_list[sufs_read_cache_chunk_list_idx]) {
        WARN_FS(KERN_ERR
                "sufs_kfs_read_cache_init: failed to allocate memory\n");
        return -ENOMEM;
    }
    sufs_read_cache_chunk_list_idx++;
    return 0;
}

int sufs_kfs_read_cache_fini(void) {
    int i;
    for (i = 0; i < LSFS_READ_PAGE_CACHE_CHUNK_LIST_MAX; i++) {
        if (sufs_read_cache_chunk_list[i]) {
            kvfree(sufs_read_cache_chunk_list[i]);
        }
    }
    kvfree(sufs_read_cache_chunk_list);
    return 0;
}