#ifndef KFS_LOGGER_H
#define KFS_LOGGER_H
#include <linux/kernel.h>

#ifdef DEBUG
#define LOG_FS(fmt, ...) printk(KERN_INFO "[KernFS] " fmt, ##__VA_ARGS__)
#else
#define LOG_FS(fmt, ...) // Empty
#endif

#define WARN_FS(fmt, ...) printk(KERN_ERR "[KernFS] " fmt, ##__VA_ARGS__)

#endif