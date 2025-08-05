#ifndef LIBDRIVER_MPK_H_
#define LIBDRIVER_MPK_H_

#include <asm-generic/mman-common.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <x86intrin.h>

#ifdef MPK_PROTECTION

#define COMPILER_BARRIER() asm volatile("" ::: "memory")

#define __wrpkru(PKRU_ARG)                                                     \
    do {                                                                       \
        asm volatile("xor %%ecx, %%ecx\n\txor %%edx, %%edx\n\tmov "            \
                     "%0,%%eax\n\t.byte 0x0f,0x01,0xef\n\t"                    \
                     :                                                         \
                     : "n"(PKRU_ARG)                                           \
                     : "eax", "ecx", "edx");                                   \
    } while (0)

#define __wrpkrumem(PKRU_ARG)                                                  \
    do {                                                                       \
        asm volatile("xor %%ecx, %%ecx\n\txor %%edx, %%edx\n\tmov "            \
                     "%0,%%eax\n\t.byte 0x0f,0x01,0xef\n\t"                    \
                     :                                                         \
                     : "m"(PKRU_ARG)                                           \
                     : "eax", "ecx", "edx");                                   \
    } while (0)

#define __rdpkru()                                                             \
    ({                                                                         \
        unsigned int eax, edx;                                                 \
        unsigned int ecx = 0;                                                  \
        unsigned int pkru;                                                     \
        asm volatile(".byte 0x0f,0x01,0xee\n\t"                                \
                     : "=a"(eax), "=d"(edx)                                    \
                     : "c"(ecx));                                              \
        pkru = eax;                                                            \
        pkru;                                                                  \
    })

#else

#define __rdpkru() 0
#endif

int allocate_pkey(void);
int protect_buffer_with_pkey(void *buffer, size_t size, int pkey);
void free_pkey(int pkey);
void enter_protected_region(unsigned int value);
void exit_protected_region(unsigned int value);

#endif // LIBDRIVER_MPK_H_