#include <stdio.h>
#include <stdlib.h>

#include "../include/libfs_config.h"
#include "../include/nvme-driver.h"
#include "./include/cmd.h"
#include "./include/super.h"

#include <string.h>

struct sufs_libfs_super_block sufs_libfs_sb;

// OK
void sufs_libfs_init_super_block(struct sufs_libfs_super_block *sb) {
    sb->start_addr = 0;
}
