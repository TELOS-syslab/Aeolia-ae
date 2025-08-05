#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "../include/ioctl.h"
#include "../include/kfs_config.h"
#include "balloc.h"
#include "dev_nvme.h"
#include "inode.h"
#include "mmap.h"
#include "super.h"

int sufs_kfs_dele_thrds = 0;
module_param(sufs_kfs_dele_thrds, int, S_IRUGO);
MODULE_PARM_DESC(sufs_kfs_dele_thrds, "Number of delegation threads");

static int sufs_kfs_do_init(void);

static long sufs_kfs_ioctl(struct file *file, unsigned int cmd,
                           unsigned long arg) {
    switch (cmd) {
    case SUFS_CMD_MOUNT:
        LOG_FS("sufs_kfs_ioctl: SUFS_CMD_MOUNT\n");
        return sufs_mount();
    case SUFS_CMD_UMOUNT:
        LOG_FS("sufs_kfs_ioctl: SUFS_CMD_UMOUNT\n");
        return sufs_umount(arg);

    case SUFS_CMD_MAP:
        LOG_FS("sufs_kfs_ioctl: SUFS_CMD_MAP\n");
        return sufs_mmap_file(arg);
    case SUFS_CMD_UNMAP:
        LOG_FS("sufs_kfs_ioctl: SUFS_CMD_UNMAP\n");
        return sufs_unmap_file(arg);

    case SUFS_CMD_ALLOC_INODE:
        LOG_FS("sufs_kfs_ioctl: SUFS_CMD_ALLOC_INODE\n");
        return sufs_alloc_inode_to_libfs(arg);
    case SUFS_CMD_FREE_INODE:
        LOG_FS("sufs_kfs_ioctl: SUFS_CMD_FREE_INODE\n");
        return sufs_free_inode_from_libfs(arg);

    case SUFS_CMD_ALLOC_BLOCK:
        LOG_FS("sufs_kfs_ioctl: SUFS_CMD_ALLOC_BLOCK\n");
        return sufs_alloc_blocks_to_libfs(arg);
    case SUFS_CMD_FREE_BLOCK:
        LOG_FS("sufs_kfs_ioctl: SUFS_CMD_FREE_BLOCK\n");
        return sufs_free_blocks_from_libfs(arg);

    case SUFS_CMD_CHOWN:
        LOG_FS("sufs_kfs_ioctl: SUFS_CMD_CHOWN\n");
        return sufs_chown(arg);
    case SUFS_CMD_CHMOD:
        LOG_FS("sufs_kfs_ioctl: SUFS_CMD_CHMOD\n");
        return sufs_chmod(arg);

    case SUFS_CMD_DEBUG_READ:
        LOG_FS("sufs_kfs_ioctl: SUFS_CMD_DEBUG_READ\n");
        return sufs_debug_read();

    case SUFS_CMD_DEBUG_INIT:
        LOG_FS("sufs_kfs_ioctl: SUFS_CMD_DEBUG_INIT\n");
        return sufs_kfs_do_init();

    default:
        WARN_FS("%s: unsupported command %x\n", __func__, cmd);
    }

    return -EINVAL;
}

static struct file_operations sufs_fops = {.unlocked_ioctl = sufs_kfs_ioctl};

static struct class *sufs_class;
static dev_t sufs_dev_t;

static int sufs_init(void) {
    struct device *dev;

    int ret = 0;

    if ((ret = register_chrdev(SUFS_MAJOR, SUFS_DEV_NAME, &sufs_fops)) != 0) {
        return ret;
    }

    sufs_dev_t = MKDEV(SUFS_MAJOR, 0);

    if (IS_ERR(sufs_class = class_create(SUFS_DEV_NAME))) {
        /* TODO: error macro */
        WARN_FS("Error in class_create!\n");
        ret = PTR_ERR(sufs_class);
        goto out_chrdev;
    }

    if (IS_ERR(dev = device_create(sufs_class, NULL, sufs_dev_t, NULL,
                                   SUFS_DEV_NAME))) {
        WARN_FS("Error in device_create!\n");
        ret = PTR_ERR(dev);
        goto out_class;
    }

    LOG_FS("sufs init done!\n");

    return 0;

out_class:
    class_destroy(sufs_class);

out_chrdev:
    unregister_chrdev(SUFS_MAJOR, SUFS_DEV_NAME);

    return -ENOMEM;
}

/*
 * We cannot run the below code in sufs_init function since if we compile the
 * module as built-in, it cannot open "/dev/dax*.0" during kernel boot up
 */
static int sufs_kfs_do_init(void) {
    int ret = 0;

    sufs_init_sb();
    LOG_FS("[kfs]sufs init super block done!\n");

    if (sufs_init_dev() != 0)
        return -ENOMEM;
    LOG_FS("[kfs]sufs init dev done!\n");

    if ((ret = sufs_fs_init()) == 0) {
        LOG_FS("sufs do init succeed!\n");
    }

    return ret;
}

static void sufs_exit(void) {
    LOG_FS("[kfs]sufs exit\n");
    sufs_fs_fini();

    device_destroy(sufs_class, sufs_dev_t);

    class_destroy(sufs_class);

    unregister_chrdev(SUFS_MAJOR, SUFS_DEV_NAME);

    return;
}

MODULE_LICENSE("GPL");
module_init(sufs_init);
module_exit(sufs_exit);
