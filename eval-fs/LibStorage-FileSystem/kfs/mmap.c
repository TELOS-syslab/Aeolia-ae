#include <linux/cred.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>

#include "../include/common_inode.h"
#include "../include/ioctl.h"
#include "../include/kfs_config.h"
#include "inode.h"
#include "mmap.h"
#include "util.h"

/*
 * write != 0, mmaped as read and write,
 * otherwise, mmaped as read
 */
static long sufs_do_mmap_file(struct sufs_super_block *sb, int ino,
                              int writable, long *index_offset) {
    int ret = 0;
    //ret = sufs_kfs_mmap_lease(ino, writable);
    return ret;
}

long sufs_mmap_file(unsigned long arg) {
    long ret = 0;
    struct sufs_ioctl_map_entry entry;

    if (copy_from_user(&entry, (void *)arg,
                       sizeof(struct sufs_ioctl_map_entry)))
        return -EFAULT;

    ret = sufs_do_mmap_file(&sufs_sb, entry.inode, entry.perm,
                            &entry.index_offset);

#if 0
    printk("mmap ret is %ld\n", ret);
#endif

    if (ret == 0) {
        if (copy_to_user((void *)arg, &entry,
                         sizeof(struct sufs_ioctl_map_entry)))
            return -EFAULT;
    }

    return ret;
}

long sufs_do_unmap_file(struct sufs_super_block *sb, int ino) {
    int ret = 0;
    //ret = sufs_kfs_munmap_lease(ino);
    return 0;
}

long sufs_unmap_file(unsigned long arg) {
    struct sufs_ioctl_map_entry entry;

    if (copy_from_user(&entry, (void *)arg,
                       sizeof(struct sufs_ioctl_map_entry)))
        return -EFAULT;

    return sufs_do_unmap_file(&sufs_sb, entry.inode);
}

long sufs_chown(unsigned long arg) { return 0; }

long sufs_chmod(unsigned long arg) { return 0; }
