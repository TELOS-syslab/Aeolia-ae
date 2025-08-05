#ifndef TFS_COMPILER_H_
#define TFS_COMPILER_H_

#define __XCONCAT2(a, b) a##b
#define __XCONCAT(a, b) __XCONCAT2(a, b)

#define __padout__                                                             \
    char __XCONCAT(__padout, __COUNTER__)[0]                                   \
        __attribute__((aligned(64)))
#define __mpalign__ __attribute__((aligned(64)))
#define __noret__ __attribute__((noreturn))

#define tfs_barrier() asm volatile("" ::: "memory")
#define tfs_cpu_relax() asm volatile("pause\n" : : : "memory")

#ifndef __always_inline
#define __always_inline inline __attribute__((always_inline))
#endif

#ifndef likely
#define likely(x) __builtin_expect((x), 1)
#endif

#ifndef unlikely
#define unlikely(x) __builtin_expect((x), 0)
#endif

/* TODO: Port the actual implementation of WRITE_ONCE */
#ifndef WRITE_ONCE
#define WRITE_ONCE(x, val) ((x) = (val))
#endif

#ifndef container_of
#define container_of(ptr, type, member)                                        \
    (type *)((char *)(ptr) - offsetof(type, member))
#endif

#endif /* TFS_COMPILER_H_ */
