#ifndef SUFS_KFS_UTIL_H_
#define SUFS_KFS_UTIL_H_

#include "../include/kfs_config.h"
#include "dev_nvme.h"
#include <linux/dma-mapping.h>
#include <linux/nvme.h>

static inline unsigned long sufs_kfs_rdtsc(void) {
    unsigned long rax, rdx;
    __asm__ __volatile__("rdtsc\n" : "=a"(rax), "=d"(rdx));
    return (rdx << 32) + rax;
}

#define SUFS_KFS_PAGE_ROUND_UP(a)                                              \
    ((((unsigned long)(a)) + SUFS_PAGE_SIZE - 1) & ~(SUFS_PAGE_SIZE - 1))

#define SUFS_KFS_NEXT_PAGE(a)                                                  \
    ((((unsigned long)(a)) & SUFS_PAGE_MASK) + SUFS_PAGE_SIZE)

#define SUFS_KFS_FILE_BLOCK_OFFSET(a)                                          \
    (((unsigned long)(a)) & ((1 << SUFS_FILE_BLOCK_SHIFT) - 1))

#define SUFS_KFS_CACHE_ROUND_UP(a)                                             \
    ((((unsigned long)(a)) + SUFS_CACHELINE - 1) & ~(SUFS_CACHELINE - 1))

#define SUFS_KFS_CACHE_ROUND_DOWN(a)                                           \
    ((((unsigned long)(a)) & ~(SUFS_CACHELINE - 1)))

static inline void sufs_kfs_memset_nt(void *dest, unsigned int dword,
                                      unsigned long length) {
    unsigned long dummy1, dummy2;
    unsigned long qword = ((unsigned long)dword << 32) | dword;

    asm volatile("movl %%edx,%%ecx\n"
                 "andl $63,%%edx\n"
                 "shrl $6,%%ecx\n"
                 "jz 9f\n"
                 "1:      movnti %%rax,(%%rdi)\n"
                 "2:      movnti %%rax,1*8(%%rdi)\n"
                 "3:      movnti %%rax,2*8(%%rdi)\n"
                 "4:      movnti %%rax,3*8(%%rdi)\n"
                 "5:      movnti %%rax,4*8(%%rdi)\n"
                 "8:      movnti %%rax,5*8(%%rdi)\n"
                 "7:      movnti %%rax,6*8(%%rdi)\n"
                 "8:      movnti %%rax,7*8(%%rdi)\n"
                 "leaq 64(%%rdi),%%rdi\n"
                 "decl %%ecx\n"
                 "jnz 1b\n"
                 "9:     movl %%edx,%%ecx\n"
                 "andl $7,%%edx\n"
                 "shrl $3,%%ecx\n"
                 "jz 11f\n"
                 "10:     movnti %%rax,(%%rdi)\n"
                 "leaq 8(%%rdi),%%rdi\n"
                 "decl %%ecx\n"
                 "jnz 10b\n"
                 "11:     movl %%edx,%%ecx\n"
                 "shrl $2,%%ecx\n"
                 "jz 12f\n"
                 "movnti %%eax,(%%rdi)\n"
                 "12:\n"
                 : "=D"(dummy1), "=d"(dummy2)
                 : "D"(dest), "a"(qword), "d"(length)
                 : "memory", "rcx");
}

static inline void sufs_kfs_mm_clwb(unsigned long addr) {
#if SUFS_CLWB_FLUSH
    asm volatile(".byte 0x66; xsaveopt %0" : "+m"(*(volatile char *)(addr)));
#else
    asm volatile("clflush %0" : "+m"(*(volatile char *)(addr)));
#endif
}

extern struct sufs_dev_arr sufs_dev_arr;

static inline void sufs_kfs_sfence(void) { asm volatile("sfence\n" : :); }

static inline void sufs_kfs_clwb_buffer(void *ptr, unsigned int len) {
    unsigned int i = 0;

    /* align addr to cache line */
    unsigned long addr = SUFS_KFS_CACHE_ROUND_DOWN((unsigned long)ptr);

    /* align len to cache line */
    len = SUFS_KFS_CACHE_ROUND_UP(len);
    for (i = 0; i < len; i += SUFS_CACHELINE) {
        sufs_kfs_mm_clwb(addr + i);
    }
}

extern int nvme_submit_sync_cmd(struct request_queue *q,
                                struct nvme_command *cmd, void *buf,
                                unsigned bufflen);

static inline int __sufs_kfs_send_nvme_rw(unsigned long lba, unsigned long len,
                                          dma_addr_t dma_addr, u8 opcode) {
    int ret;
    int i;
    int numBuf;
    struct nvme_command cmd = {0};
    unsigned long *prpBuf;
    unsigned long __prp1, __prp2;
    int cpu = smp_processor_id();

    numBuf = len / PAGE_SIZE;
    if (len % PAGE_SIZE > 0)
        numBuf++;

    __prp1 = dma_addr;
    if (numBuf == 1) {
        __prp2 = 0;
    } else if (numBuf == 2) {
        __prp2 = dma_addr + PAGE_SIZE;
    } else {

        // sufs_prp_list_acquire();
        prpBuf = (unsigned long *)(sufs_dev_arr.prp_list[cpu]);
        __prp2 = sufs_dev_arr.prp_list_dma_addr[cpu];
        for (i = 1; i < numBuf; i++) {
            prpBuf[i - 1] = dma_addr + i * PAGE_SIZE;
        }
    }

    cmd.rw.opcode = opcode;
    cmd.rw.nsid = sufs_dev_arr.ns->head->ns_id;
    cmd.rw.dptr.prp1 = __prp1;
    cmd.rw.dptr.prp2 = __prp2;
    cmd.rw.slba = lba >> (sufs_dev_arr.ns->head->lba_shift);
    cmd.rw.length = (len >> (sufs_dev_arr.ns->head->lba_shift)) - 1;
    cmd.rw.control = 0;
    cmd.rw.dsmgmt = 0;

    // LCDTODO: check the args.
    ret = nvme_submit_sync_cmd(sufs_dev_arr.ns->queue, &cmd, NULL, 0);
    // if (locked) {
    //     spin_unlock(&sufs_dev_arr.prp_lock[cpu]);
    // }
    return ret;
}

static inline int sufs_kfs_send_nvme_read(unsigned long lba, unsigned long len,
                                          dma_addr_t dma_addr) {
    return __sufs_kfs_send_nvme_rw(lba, len, dma_addr, nvme_cmd_read);
}
static inline int sufs_kfs_send_nvme_write(unsigned long lba, unsigned long len,
                                           dma_addr_t dma_addr) {
    return __sufs_kfs_send_nvme_rw(lba, len, dma_addr, nvme_cmd_write);
}

static inline unsigned long sufs_kfs_dma_buffer_acquire(unsigned long pgs,
                                                        int cpu) {
    unsigned long batchs = sufs_dev_arr.dma_max_bytes / PAGE_SIZE;
    unsigned long dma_base_addr = 0;
    unsigned long i;
    if (cpu < 0 || cpu >= SUFS_MAX_CPU) {
        cpu = smp_processor_id();
    }
    spin_lock(&sufs_dev_arr.dma_buffer_lock[cpu]);
    unsigned long contiguity = 0;
    for (i = 0; i < batchs; i++) {
        if (sufs_dev_arr.local_dma_buffer_bm[cpu] & (1 << i)) {
            contiguity = 0;
            continue;
        }
        contiguity++;
        if (contiguity == pgs) {
            sufs_dev_arr.local_dma_buffer_bm[cpu] |= ((1UL << pgs) - 1UL)
                                                     << (i + 1UL - pgs);
            dma_base_addr = sufs_dev_arr.dma_buffer_start[cpu] +
                            (i + 1UL - pgs) * PAGE_SIZE;
            LOG_FS("dma_buffer_acquire: cpu %d, dma_base_addr_offset %lu, bm "
                   "%lx, pgs %lu\n",
                   cpu, i, sufs_dev_arr.local_dma_buffer_bm[cpu], pgs);
            break;
        }
    }

    spin_unlock(&sufs_dev_arr.dma_buffer_lock[cpu]);
    return dma_base_addr;
}

static inline int dma_buffer_va_to_cpu(unsigned long dma_buffer_va) {
    int cpu = smp_processor_id();
    if (dma_buffer_va >= sufs_dev_arr.dma_buffer_start[cpu] &&
        dma_buffer_va <
            sufs_dev_arr.dma_buffer_start[cpu] + sufs_dev_arr.dma_max_bytes) {
        return cpu;
    } else {
        for (cpu = 0; cpu < SUFS_MAX_CPU; cpu++) {
            if (dma_buffer_va >= sufs_dev_arr.dma_buffer_start[cpu] &&
                dma_buffer_va < sufs_dev_arr.dma_buffer_start[cpu] +
                                    sufs_dev_arr.dma_max_bytes) {
                return cpu;
            }
        }
    }
    return -1;
}

static inline unsigned long dma_buffer_va_to_da(unsigned long vaddr) {
    int cpu = dma_buffer_va_to_cpu(vaddr);
    if (cpu < 0) {
        WARN_FS("dma_buffer_va_to_da: invalid dma_buffer_va\n");
        return 0;
    }
    return (vaddr - sufs_dev_arr.dma_buffer_start[cpu]) +
           sufs_dev_arr.dma_addr[cpu];
}

static inline int sufs_kfs_dma_buffer_release(unsigned long dma_buffer_va,
                                              unsigned long pgs) {
    int cpu = dma_buffer_va_to_cpu(dma_buffer_va);
    spin_lock(&sufs_dev_arr.dma_buffer_lock[cpu]);
    // clear bm
    sufs_dev_arr.local_dma_buffer_bm[cpu] &= ~(
        ((1UL << pgs) - 1UL)
        << ((dma_buffer_va - sufs_dev_arr.dma_buffer_start[cpu]) / PAGE_SIZE));
    spin_unlock(&sufs_dev_arr.dma_buffer_lock[cpu]);
    LOG_FS("dma_buffer_release: cpu %d, pgs %lu, bm %lx\n", cpu, pgs,
           sufs_dev_arr.local_dma_buffer_bm[cpu]);
    return 0;
}
#endif /* SUFS_KFS_UTIL_H_ */
