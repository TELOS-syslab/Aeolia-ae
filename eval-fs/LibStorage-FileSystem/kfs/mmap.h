#ifndef SUFS_KFS_MMAP_H_
#define SUFS_KFS_MMAP_H_

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>

#include "../include/kfs_config.h"
#include "balloc.h"
#include "super.h"

long sufs_mmap_file(unsigned long arg);

long sufs_do_unmap_file(struct sufs_super_block *sb, int ino);

long sufs_unmap_file(unsigned long arg);

long sufs_chown(unsigned long arg);

long sufs_chmod(unsigned long arg);

#endif /* SUFS_KFS_MMAP_H_ */
