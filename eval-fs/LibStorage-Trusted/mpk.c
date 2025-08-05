#include "./include/mpk.h"

#ifdef MPK_PROTECTION

int allocate_pkey(void) {
    return syscall(SYS_pkey_alloc, 0, PKEY_DISABLE_ACCESS | PKEY_DISABLE_WRITE);
}

int protect_buffer_with_pkey(void *buffer, size_t size, int pkey) {
    return syscall(SYS_pkey_mprotect, buffer, size, PROT_READ | PROT_WRITE,
                   pkey);
}
void free_pkey(int pkey) { syscall(SYS_pkey_free, pkey); }

// Function to enter a protected region
void enter_protected_region(unsigned int value) {
    // _wrpkru(_rdpkru_u32() & ~(3 << (pkey * 2)));
    __wrpkrumem(value);
}

// Function to exit a protected region
void exit_protected_region(unsigned int value) {
    // _wrpkru(_rdpkru_u32() | (3 << (pkey * 2)));
    __wrpkrumem(value);
}
#else
int allocate_pkey(void) { return 0; }
int protect_buffer_with_pkey(void *buffer, size_t size, int pkey) { return 0; }
void free_pkey(int pkey) { return; }
inline void enter_protected_region(unsigned int value) { return; }
inline void exit_protected_region(unsigned int value) { return; }

#endif