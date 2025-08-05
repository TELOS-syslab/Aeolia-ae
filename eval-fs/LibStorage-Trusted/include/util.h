#ifndef _TFS_UTIL_H_
#define _TFS_UTIL_H_

#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

static inline void set_bit(unsigned long nr, unsigned long *addr) {
    unsigned long mask = 1UL << (nr % (sizeof(unsigned long) * 8));
    unsigned long *p = addr + nr / (sizeof(unsigned long) * 8);
    *p |= mask;
}

static inline void clear_bit(unsigned long nr, unsigned long *addr) {
    unsigned long mask = 1UL << (nr % (sizeof(unsigned long) * 8));
    unsigned long *p = addr + nr / (sizeof(unsigned long) * 8);
    *p &= ~mask;
}

static inline unsigned long test_bit(unsigned long nr, unsigned long *addr) {
    unsigned long mask = 1UL << (nr % (sizeof(unsigned long) * 8));
    unsigned long *p = addr + nr / (sizeof(unsigned long) * 8);
    return *p & mask;
}

#define KERNEL_PAGE_SIZE 4096
#define KERNEL_PAGE_ALIGN(addr)                                                \
    (((addr) + KERNEL_PAGE_SIZE - 1) & ~(KERNEL_PAGE_SIZE - 1))

static inline unsigned get_core_id_userspace() {
    unsigned a, d, c;
    asm volatile("rdtscp" : "=a"(a), "=d"(d), "=c"(c)::"memory");
    return c & 0xFFF;
}

static inline long long tfs_rdtsc(void) {
    unsigned long hi, lo;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return (lo | (hi << 32));
}

static inline unsigned long tfs_rdtscp(void) {
    unsigned long rax, rdx;
    __asm__ __volatile__("rdtscp\n" : "=a"(rax), "=d"(rdx) : : "%ecx");
    return (rdx << 32) + rax;
}

static inline int tfs_gettid(void) {
    /* return sched_getcpu(); */
    return syscall(SYS_gettid);
}

#endif // _TFS_UTIL_H_