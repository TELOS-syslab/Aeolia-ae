#ifndef SUFS_KFS_PAGECACHE_H_
#define SUFS_KFS_PAGECACHE_H_

#include <linux/sched.h>

#include "../include/kfs_config.h"

int sufs_kfs_read_cache_init(void);

int sufs_kfs_read_cache_fini(void);

int sufs_kfs_mmap_read_cache(void);

int sufs_kfs_munmap_read_cache(void);

int sufs_kfs_expand_read_cache(void);

#endif /* SUFS_KFS_PAGECACHE_H_ */