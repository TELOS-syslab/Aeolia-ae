#ifndef TFS_CMD_H_
#define TFS_CMD_H_

// static int my_open(const char *pathname, int flags, mode_t mode) {
//     int fd = syscall(SYS_open, pathname, flags, mode);
    
//     if (fd == -1) {
//         LOG_ERROR("open failed");
//     }
    
//     return fd;
// }

void tfs_cmd_init(void);
void tfs_cmd_fini(void);

int tfs_cmd_alloc_inodes(unsigned long *ibm_lba, int *inode, int *num, int cpu);
int tfs_cmd_free_inodes(int ibm_lba, int num);

int tfs_cmd_alloc_blocks(unsigned long *bbm_lba, unsigned long *st_block,
                         int *num, int cpu);
int tfs_cmd_free_blocks(unsigned long bbm_lba, int num);
int tfs_cmd_map_file(int ino, int writable);
int tfs_cmd_unmap_file(int ino);
#endif // TFS_CMD_H_