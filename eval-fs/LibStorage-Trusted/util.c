#include "./include/util.h"

inline unsigned get_core_id_userspace() {
    unsigned a, d, c;
    asm volatile("rdtscp" : "=a"(a), "=d"(d), "=c"(c)::"memory");
    return c & 0xFFF;
}