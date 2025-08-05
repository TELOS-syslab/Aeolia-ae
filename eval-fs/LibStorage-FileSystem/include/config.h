#ifndef SUFS_GLOBAL_CONFIG_H_
#define SUFS_GLOBAL_CONFIG_H_

#include <linux/types.h>

// This excludes the inode bitmap, block bitmap and sinode.
#define FS_DATA_BYTES 256ul * 1024 * 1024 * 1024
#define FS_HEAD_BYTES 12ul * 1024 * 1024 * 1024
#define FS_TOTAL_BYTES (FS_DATA_BYTES + FS_HEAD_BYTES)
#define FS_JOURNAL_BYTES 32ul * 1024 * 1024 * 1024

#define FS_BLOCK_SIZE 512
#define FS_BLOCK_SHIFT 9

#define FS_FREE_PAGES (FS_DATA_BYTES / 4096)                       // 33,554,432
#define FS_FREE_BITMAP_BYTES (FS_FREE_PAGES / 8)                   // 4,194,304
#define FS_FREE_BITMAP_LOGICAL_BLOCKS (FS_FREE_BITMAP_BYTES / 512) // 8,192

#define FS_INODE_BITMAP_BYTES (SUFS_MAX_INODE_NUM / (8)) // 2,097,152
#define FS_INODE_BITMAP_LOGICAL_BLOCKS (FS_INODE_BITMAP_BYTES / 512) // 4,096

#define FS_PAGECACHE_LISTS 128
#define FS_PAGECACHE_PER_LIST_SIZE 1024 * 1024 * 1024 // 1GB

/* cache line size */
#define SUFS_CACHELINE 64

#define SUFS_PAGE_SIZE 4096

/* log2(SUFS_PAGE_SIZE) */
#define SUFS_PAGE_SHIFT 12

#define SUFS_PAGE_MASK (~(SUFS_PAGE_SIZE - 1))

/* maximum number of CPUs */
#define SUFS_MAX_CPU 128
#define SUFS_MAX_THREADS 128

#define SUFS_MAJOR 269
#define SUFS_DEV_NAME "supremefs"

#define SUFS_DEV_PATH "/dev/" SUFS_DEV_NAME

/* Maximum number of PM device */
#define SUFS_PM_MAX_INS 1

#define LSFS_GLOBAL_LEASE_ADDR 0x300000000000

#define LSFS_READ_PAGE_CACHE_ADDR 0x3f0000000000

#define LSFS_READ_PAGE_CACHE_CHUNK_SIZE 0x100000000 // 4GB

#define LSFS_READ_PAGE_CACHE_CHUNK_LIST_MAX 64
/*
 * Not sure whether we actually need this or not, have it for now to simplify
 * debugging
 */
#define SUFS_MOUNT_ADDR 0x400000000000

#define SUFS_BLK_START 0

#define SUFS_ROOT_INODE 2

/* The maximal number of thread groups that can acquire the lease */
#define SUFS_KFS_LEASE_MAX_OWNER 4
/* What is the length of one lease, in the unit of number of ticks */
#define SUFS_KFS_LEASE_PERIOD 5000

/* maximum number of bytes in the FS */
#define SUFS_KFS_MAX_SIZE (1024ul * 1024 * 1024 * 1024)

/* First inode number that can be accessed by UFS */
#define SUFS_FIRST_UFS_INODE_NUM (3)

/* Maximal number of inode */
// LCDTODO: how to calculate this number?
// #define SUFS_MAX_INODE_NUM 16777216
#define SUFS_MAX_INODE_NUM 16777216

#define SUFS_SUPER_PAGE_SIZE 4096

/* TODO: Obtain the maximum number of process from sysctl */
#define SUFS_MAX_PROCESS 4194304

/*
 * Change the type of sufs_kfs_pid_to_tgroup before increasing this value to
 * above 256
 */

#define SUFS_MAX_TGROUP 256

#define SUFS_MAX_PROCESS_PER_TGROUP 32

#define SUFS_NAME_MAX 255

#define SUFS_ROOT_PERM                                                         \
    (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)

#define SUFS_FILE_BLOCK_PAGE_CNT 8
#define SUFS_FILE_BLOCK_SIZE (SUFS_FILE_BLOCK_PAGE_CNT * SUFS_PAGE_SIZE)
#define SUFS_FILE_BLOCK_SHIFT 15

#define SUFS_DELEGATION_ENABLE 1

/* write delegation limits: 256 */
#define SUFS_WRITE_DELEGATION_LIMIT 256

/* read delegation limits: 32K */
#define SUFS_READ_DELEGATION_LIMIT (32 * 1024)

#define SUFS_CLWB_FLUSH 1

#endif /* SUFS_CONFIG_H_ */
