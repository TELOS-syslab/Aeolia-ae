#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "../include/common_inode.h"
#include "../include/libfs_config.h"
#include "./include/cmd.h"
#include "./include/file.h"
#include "./include/filetable.h"
#include "./include/mfs.h"
#include "./include/mnode.h"
#include "./include/proc.h"
#include "./include/syscall.h"
#include "./include/util.h"

// OK
static struct sufs_libfs_file_mnode *
sufs_libfs_getfile(struct sufs_libfs_proc *proc, int fd) {
    struct sufs_libfs_filetable *filetable = proc->ftable;
    return sufs_libfs_filetable_getfile(filetable, fd);
}

/*
 * Allocate a file descriptor for the given file.
 * Takes over file reference from caller on success.
 */
// OK
static int sufs_libfs_fdalloc(struct sufs_libfs_proc *proc,
                              struct sufs_libfs_file_mnode *f, int omode) {
    struct sufs_libfs_filetable *filetable = proc->ftable;
    if (!f)
        return -1;

    return sufs_libfs_filetable_allocfd(filetable, f, omode & O_ANYFD,
                                        omode & O_CLOEXEC);
}

// OK
/* How to handle concurrent access to the same file descriptor */
off_t sufs_libfs_sys_lseek(struct sufs_libfs_proc *proc, int fd, off_t offset,
                           int whence) {
    struct sufs_libfs_file_mnode *f = sufs_libfs_getfile(proc, fd);
    LOG_FS("----------- sufs_libfs_sys_lseek fd %d, offset %ld, whence %d\n", fd,
           offset, whence);
    if (!f)
        return -1;

    if (sufs_libfs_mnode_type(f->m) != SUFS_FILE_TYPE_REG)
        return -1; // ESPIPE

    if (whence == SEEK_CUR) {
        offset += f->off;
    } else if (whence == SEEK_END) {
        u64 size = sufs_libfs_mnode_file_size(f->m);
        offset += size;
    }

    if (offset < 0)
        return -1;

    f->off = offset;

    return offset;
}

// OK
int sufs_libfs_sys_fstat(struct sufs_libfs_proc *proc, int fd,
                         struct stat *stat) {
    LOG_FS("----------- sufs_libfs_sys_fstat fd %d\n", fd);
    struct sufs_libfs_file_mnode *f = sufs_libfs_getfile(proc, fd);
    if (!f)
        return -1;

    if (sufs_libfs_mnode_stat(f->m, stat) < 0)
        return -1;

    return 0;
}

// OK
int sufs_libfs_sys_lstat(struct sufs_libfs_proc *proc, char *path,
                         struct stat *stat) {
    LOG_FS("----------- sufs_libfs_sys_lstat path %s\n", path);
    struct sufs_libfs_mnode *m = NULL;

    m = sufs_libfs_namei(proc->cwd_m, path);
    if (!m) {
        errno = ENOENT;
        return -1;
    }

    if (sufs_libfs_mnode_stat(m, stat) < 0)
        return -1;

    return 0;
}

// OK
int sufs_libfs_sys_close(struct sufs_libfs_proc *proc, int fd) {
    LOG_FS("----------- sufs_libfs_sys_close fd %d\n", fd);
    struct sufs_libfs_file_mnode *f = sufs_libfs_getfile(proc, fd);
    if (!f)
        return -1;
    struct sufs_libfs_mnode *m = f->m;

    /* Informs the kernel that this file is no longer in the critical section */
    sufs_libfs_file_exit_cs(f->m);

    sufs_libfs_filetable_close(proc->ftable, fd);

    return 0;
}

/* A special version for directory lookup used in rename */
// OK
static struct sufs_libfs_mnode *
sufs_libfs_rename_dir_lookup(struct sufs_libfs_mnode *mnode, char *name) {
    unsigned long ino = 0;

    sufs_libfs_chainhash_lookup(&mnode->data.dir_data.map_, name, SUFS_NAME_MAX,
                                &ino, NULL);

    return sufs_libfs_mnode_array[ino];
}

// OK
int sufs_libfs_sys_rename(struct sufs_libfs_proc *proc, char *old_path,
                          char *new_path) {
    LOG_FS("----------- sufs_libfs_sys_rename old_path %s, new_path %s\n",
              old_path, new_path);
    char oldname[SUFS_NAME_MAX], newname[SUFS_NAME_MAX];
    struct sufs_libfs_mnode *cwd_m = NULL, *mdold = NULL, *mdnew = NULL,
                            *mfold = NULL, *mfroadblock = NULL;

    struct sufs_libfs_ch_item *item = NULL;

    int ret = 0;

    int mfold_type = 0;

    cwd_m = proc->cwd_m;

    mdold = sufs_libfs_nameiparent(cwd_m, old_path, oldname);
    if (!mdold){
        DEBUG("sufs_libfs_sys_rename mdold is NULL, old_path is %s, new_path is %s\n",
              old_path, new_path);
        return -55;
    }

    mdnew = sufs_libfs_nameiparent(cwd_m, new_path, newname);
    if (!mdnew)
        return -2;

    // if (strcmp(oldname, ".") == 0 || strcmp(oldname, "..") == 0 ||
    //     strcmp(newname, ".") == 0 || strcmp(newname, "..") == 0)
    //     abort();

    sufs_libfs_file_enter_cs(mdold);

    if (sufs_libfs_map_file(mdold, 1) != 0){
        ret = -3;
        goto out_err_mdold;
    }

    sufs_libfs_file_enter_cs(mdnew);

    if (sufs_libfs_map_file(mdnew, 1) != 0){
        ret = -4;
        goto out;
    }

    if (!(mfold = sufs_libfs_rename_dir_lookup(mdold, oldname))){
        ret = -5;
        goto out;
    }

    mfold_type = sufs_libfs_mnode_type(mfold);

    if (mdold == mdnew && oldname == newname)
        return 0;

    mfroadblock = sufs_libfs_rename_dir_lookup(mdnew, newname);

    if (mfroadblock) {
        int mfroadblock_type = sufs_libfs_mnode_type(mfroadblock);

        /*
         * Directories can be renamed to directories; and non-directories can
         * be renamed to non-directories. No other combinations are allowed.
         */

        if (mfroadblock_type != mfold_type){
            ret = -6;
            goto out;
        }
    }

    if (mfroadblock == mfold) {
        /*
         * If the old and new paths point to the same inode, POSIX specifies
         * that we return successfully with no further action.
         */
        ret = 0;
        goto out;
    }

#if 0
    if (mdold != mdnew && mfold_type == SUFS_FILE_TYPE_DIR)
    {
        /* Loop avoidance: Abort if the source is
         * an ancestor of the destination. */

        struct sufs_libfs_mnode *md = mdnew;
        while (1)
        {
            if (mfold == md)
                return -1;
            if (md->mnum_ == sufs_root_mnum)
                break;

            md = sufs_libfs_mnode_dir_lookup(md, "..");
        }
    }
#endif
    if (mfroadblock) {
        ret = sufs_libfs_cmd_rename(mfold->ino_num, mdold->ino_num,
                                    mfroadblock->ino_num, mdnew->ino_num,
                                    newname);
    } else {
        ret = sufs_libfs_cmd_rename(mfold->ino_num, mdold->ino_num, 0,
                                    mdnew->ino_num, newname);
    }
    if(ret < 0) {
        ret = -7;
        goto out;
    }
    /* Perform the actual rename operation in hash table */
    if (sufs_libfs_mnode_dir_replace_from(
            mdnew, newname, mfroadblock, mdold, oldname, mfold,
            mfold_type == SUFS_FILE_TYPE_DIR ? mfold : NULL, &item)) {
        

        struct sufs_dir_entry *new_dir = NULL, *old_dir = NULL, *rb_dir = NULL;

        int name_len = strlen(newname) + 1;
        new_dir = (struct sufs_dir_entry *)malloc(
            sizeof(struct sufs_dir_entry) + name_len);

        strcpy(new_dir->name, newname);
        LOG_FS("new_dir->name is %s, should be %s\n", new_dir->name, newname);
        new_dir->ino_num = mfold->ino_num;
        new_dir->rec_len = name_len;

        memcpy(&(new_dir->inode), mfold->inode, sizeof(struct sufs_inode));

        old_dir = container_of(mfold->inode, struct sufs_dir_entry, inode);

        if (mfroadblock) {
            rb_dir =
                container_of(mfroadblock->inode, struct sufs_dir_entry, inode);
        }

        new_dir->name_len = name_len;

        old_dir->ino_num = SUFS_INODE_TOMBSTONE;

        if (mfroadblock) {
            rb_dir->ino_num = SUFS_INODE_TOMBSTONE;
            
        }
        item->val2 = (unsigned long)new_dir;

        mfold->inode = &(new_dir->inode);
        mfold->parent_mnum = mdnew->ino_num;

        ret = 0;
    } else {
        ret = -8;
    }

out:
    sufs_libfs_file_exit_cs(mdnew);

out_err_mdold:
    sufs_libfs_file_exit_cs(mdold);
    if (ret<0){
        DEBUG("sufs_libfs_sys_rename failed %d, old_path %s, new_path %s\n",
              ret, old_path, new_path);
    }
    return ret;
}

// OK
static struct sufs_libfs_mnode *
sufs_libfs_create(struct sufs_libfs_mnode *cwd, char *path, short type,
                  unsigned int mode, unsigned int uid, unsigned int gid,
                  bool excl, int *error) {
    int inode = 0;

    char name[SUFS_NAME_MAX];
    struct sufs_libfs_mnode *md = sufs_libfs_nameiparent(cwd, path, name);
    struct sufs_libfs_mnode *mf = NULL;
    struct sufs_dir_entry *dir = NULL;
    int name_len = 0;
    int ret;

    LOG_FS("sufs_libfs_create: path: %s, name: %s\n", path, name);
    if (!md || sufs_libfs_mnode_dir_killed(md)) {

        WARN_FS("Failed because md is %lx!\n", (unsigned long)md);

        return NULL;
    }

    if (excl && sufs_libfs_mnode_dir_exists(md, name)) {
        if (error) {
            *error = EEXIST;
        }
        return NULL;
    }

    LOG_FS("sufs_libfs_create: md->ino_num: %d, lookup\n", md->ino_num);
    mf = sufs_libfs_mnode_dir_lookup(md, name);

    if (mf) {
        if (type != SUFS_FILE_TYPE_REG ||
            !(sufs_libfs_mnode_type(mf) == SUFS_FILE_TYPE_REG) || excl){
                WARN_FS("Failed because type error!\n", name);
            return NULL;
        }

        return mf;
    }

    name_len = strlen(name) + 1;

    ret = sufs_libfs_cmd_alloc_inode_in_directory(&inode, md->ino_num, name_len,
                                                  type, mode, uid, gid, name);

    // e3 = sufs_libfs_rdtsc();
    if (inode <= 0 || ret < 0) {
        WARN_FS("Failed to allocate inode %d\n", inode);
        if (error) {
            *error = ENOSPC;
        }

        return NULL;
    }

    mf = sufs_libfs_mfs_mnode_init(type, inode, md->ino_num, NULL);
    LOG_FS("sufs_libfs_create: inode: %d\n", inode);

    if (sufs_libfs_mnode_dir_insert(md, name, name_len, mf, &dir)) {

        sufs_libfs_inode_init(&(dir->inode), type, mode, uid, gid, 0);

        mf->inode = &(dir->inode);
        mf->index_start = NULL;
        mf->index_end = NULL;
        mf->fidx_entry_page_head = NULL;
        mf->fidx_entry_page_tail = NULL;

        /* update name_len here to finish the creation */
        dir->name_len = name_len;

        return mf;
    }

    sufs_libfs_mnode_array[inode] = NULL;

    free(mf);
    WARN_FS("Failed to insert name %s\n", name);
    return NULL;
}

// OK
int sufs_libfs_sys_openat(struct sufs_libfs_proc *proc, int dirfd, char *path,
                          int flags, int mode) {
    LOG_FS("----------- sufs_libfs_sys_openat path %s, flags %d, mode %d\n",
           path, flags, mode);
    struct sufs_libfs_mnode *cwd = NULL, *m = NULL;
    struct sufs_libfs_file_mnode *f = NULL;
    int rwmode = 0;
    int ret = 0;
    int err = 0;

    LOG_FS("file open at: %s\n", path);
    if (dirfd == AT_FDCWD) {
        cwd = proc->cwd_m;
    } else {
        struct sufs_libfs_file_mnode *fdirm =
            (struct sufs_libfs_file_mnode *)sufs_libfs_getfile(proc, dirfd);

        if (!fdirm)
            return -1;

        cwd = fdirm->m;
    }

    if (flags & O_CREAT){
        LOG_FS("sufs_libfs_sys_openat O_CREAT\n");
        m = sufs_libfs_create(cwd, path, SUFS_FILE_TYPE_REG, mode, proc->uid,
                              proc->gid, flags & O_EXCL, &err);
        LOG_FS("sufs_libfs_sys_openat sufs_libfs_create success!\n");
    }
    else{
        LOG_FS("sufs_libfs_sys_openat sufs_libfs_namei\n");
        m = sufs_libfs_namei(cwd, path);
        LOG_FS("sufs_libfs_sys_openat sufs_libfs_namei success!\n");
    }

    if (!m) {
        WARN_FS("Failed to create/namei dirfd %d , cwd inode:%d ,file %s\n",dirfd,cwd ->ino_num,
                path);
        errno = err;
        return -1;
    }

    LOG_FS("sufs_libfs_sys_openat mnode %d\n", m->ino_num);

    rwmode = flags & (O_RDONLY | O_WRONLY | O_RDWR);
    if ((sufs_libfs_mnode_type(m) == SUFS_FILE_TYPE_DIR) &&
        (rwmode != O_RDONLY)){
            WARN_FS("Failed to type :dirfd %d , cwd inode:%d ,file %s\n",dirfd,cwd ->ino_num,
                path);
            return -1;
    }

#if 0
    printf("mnode %d map: %d\n", m->ino_num, sufs_libfs_file_is_mapped(m));
#endif

    if ((ret = sufs_libfs_map_file(m, !(rwmode == O_RDONLY))) != 0)
        return ret;
    LOG_FS("sufs_libfs_sys_openat map success!\n");

    /* release it during close */
    sufs_libfs_file_enter_cs(m);

    if ((sufs_libfs_mnode_type(m) == SUFS_FILE_TYPE_REG) && (flags & O_TRUNC)) {
        if (sufs_libfs_mnode_file_size(m))
            sufs_libfs_mnode_file_truncate_zero(m);
    }

    f = sufs_libfs_file_mnode_init(m, !(rwmode == O_WRONLY),
                                   !(rwmode == O_RDONLY), !!(flags & O_APPEND));
    LOG_FS("sufs_libfs_sys_openat end!\n");
    return sufs_libfs_fdalloc(proc, f, flags);
}

// OK
int sufs_libfs_sys_unlink(struct sufs_libfs_proc *proc, char *path) {
    LOG_FS("----------- sufs_libfs_sys_unlink path %s\n", path);
    char name[SUFS_NAME_MAX];
    struct sufs_libfs_mnode *md = NULL, *cwd_m = NULL, *mf = NULL;
    int mf_type = 0;

    cwd_m = proc->cwd_m;

    md = sufs_libfs_nameiparent(cwd_m, path, name);
    if (!md)
        return -1;

    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return -1;

    mf = sufs_libfs_mnode_dir_lookup(md, name);
    if (!mf)
        return -1;

    mf_type = sufs_libfs_mnode_type(mf);

    if (mf_type == SUFS_FILE_TYPE_DIR) {
        /*
         * Remove a subdirectory only if it has zero files in it.  No files
         * or sub-directories can be subsequently created in that directory.
         */
        if (!sufs_libfs_mnode_dir_kill(mf)) {
            return -1;
        }
    }


    assert(sufs_libfs_mnode_dir_remove(md, name));

    LOG_FS("sufs_libfs_sys_unlink file delete, inode: %d\n", mf->ino_num);
 
    sufs_libfs_mnode_file_delete(mf);

    LOG_FS("sufs_libfs_sys_unlink sufs_libfs_mnode_file_delete success!\n");

    sufs_libfs_mnode_array[mf->ino_num] = NULL;

    sufs_libfs_mnode_free(mf);
    LOG_FS("sufs_libfs_sys_unlink sufs_libfs_mnode_free success!\n");
    return 0;
}

// OK
ssize_t sufs_libfs_sys_read(struct sufs_libfs_proc *proc, int fd, void *p,
                            size_t n) {
    LOG_FS("----------- sufs_libfs_sys_read fd %d, n %ld\n", fd, n);
    struct sufs_libfs_file_mnode *f = NULL;
    ssize_t res = 0;
    LOG_FS("sufs_libfs_sys_read\n");
    f = sufs_libfs_getfile(proc, fd);

    LOG_FS("getfile success\n");

    if (!f) {
        fprintf(stderr, "Cannot find file from fd: %d\n", fd);
        return -1;
    }

    res = sufs_libfs_file_mnode_read(f, (char *)p, n);
    LOG_FS("sufs_libfs_file_mnode_read %ld\n", res);
    if (res < 0) {
        res = -1;
        goto out;
    }

out:
    return res;
}

// OK
ssize_t sufs_libfs_sys_pread(struct sufs_libfs_proc *proc, int fd, void *ubuf,
                             size_t count, off_t offset) {
    LOG_FS("----------- sufs_libfs_sys_pread fd %d, count %ld, offset %ld\n", fd,
           count, offset);
    struct sufs_libfs_file_mnode *f = NULL;
    ssize_t r = 0;

    f = sufs_libfs_getfile(proc, fd);

    if (!f) {
        fprintf(stderr, "Cannot find file from fd: %d\n", fd);
        return -1;
    }

    r = sufs_libfs_file_mnode_pread(f, (char *)ubuf, count, offset);

    return r;
}

// OK
ssize_t sufs_libfs_sys_pwrite(struct sufs_libfs_proc *proc, int fd, void *ubuf,
                              size_t count, off_t offset) {
    LOG_FS("----------- sufs_libfs_sys_pwrite fd %d, count %ld, offset %ld\n", fd,
           count, offset);
    struct sufs_libfs_file_mnode *f = NULL;
    ssize_t r = 0;

    f = sufs_libfs_getfile(proc, fd);

    if (!f) {
        fprintf(stderr, "Cannot find file from fd: %d\n", fd);
        return -1;
    }

    r = sufs_libfs_file_mnode_pwrite(f, (char *)ubuf, count, offset);

    return r;
}

// OK
ssize_t sufs_libfs_sys_write(struct sufs_libfs_proc *proc, int fd, void *p,
                             size_t n) {
    LOG_FS("----------- sufs_libfs_sys_write fd %d, n %ld\n", fd, n);
    struct sufs_libfs_file_mnode *f = NULL;

    ssize_t res = 0;
    LOG_FS("sufs_libfs_sys_write\n");

    f = sufs_libfs_getfile(proc, fd);

    if (!f) {
        fprintf(stderr, "Cannot find file from fd: %d\n", fd);
        return -1;
    }

    res = sufs_libfs_file_mnode_write(f, (char *)p, n);
    LOG_FS("sys write success\n");
    return res;
}

// OK
int sufs_libfs_sys_mkdirat(struct sufs_libfs_proc *proc, int dirfd, char *path,
                           mode_t mode) {
    LOG_FS("----------- sufs_libfs_sys_mkdirat path %s, mode %d\n", path, mode);
    struct sufs_libfs_mnode *cwd = NULL;
    int err = 0;

    if (strcmp(path, "/") == 0) {
        errno = EEXIST;
        return -1;
    }

    if (dirfd == AT_FDCWD) {
        cwd = proc->cwd_m;
    } else {
        struct sufs_libfs_file_mnode *fdir = sufs_libfs_getfile(proc, dirfd);
        if (!fdir)
            return -1;

        cwd = fdir->m;
    }

    if (!sufs_libfs_create(cwd, path, SUFS_FILE_TYPE_DIR, mode, proc->uid,
                           proc->gid, true, &err)) {
        errno = err;
        return -1;
    }

    return 0;
}

// OK
int sufs_libfs_sys_chown(struct sufs_libfs_proc *proc, char *path, uid_t owner,
                         gid_t group) {
    LOG_FS("----------- sufs_libfs_sys_chown path %s, owner %d, group %d\n",
           path, owner, group);
    struct sufs_libfs_mnode *m = NULL;

    m = sufs_libfs_namei(proc->cwd_m, path);
    if (!m)
        return -1;

    return sufs_libfs_cmd_chown(m->ino_num, owner, group);
}

// OK
int sufs_libfs_sys_chmod(struct sufs_libfs_proc *proc, char *path,
                         mode_t mode) {
    LOG_FS("----------- sufs_libfs_sys_chmod path %s, mode %d\n", path, mode);
    struct sufs_libfs_mnode *m = NULL;

    m = sufs_libfs_namei(proc->cwd_m, path);
    if (!m)
        return -1;

    return sufs_libfs_cmd_chmod(m->ino_num, mode);
}

// OK
int sufs_libfs_sys_ftruncate(struct sufs_libfs_proc *proc, int fd,
                             off_t length) {
    LOG_FS("----------- sufs_libfs_sys_ftruncate fd %d, length %ld\n", fd,
           length);
    struct sufs_libfs_file_mnode *f = NULL;

    ssize_t res = 0;

    f = sufs_libfs_getfile(proc, fd);

    if (!f) {
        fprintf(stderr, "Cannot find file from fd: %d\n", fd);
        return -1;
    }

    res = sufs_libfs_file_mnode_truncate(f, length);

    return res;
}

int sufs_libfs_sys_chdir(struct sufs_libfs_proc *proc, char *path) {
#if 0
    struct sufs_libfs_mnode *m = NULL;

    m = sufs_libfs_namei(proc->cwd_m, path);
    if (!m || sufs_libfs_mnode_type(m) != SUFS_MNODE_TYPE_DIR)
        return -1;

    proc->cwd_m = m;

    return 0;
#endif
    return -1;
}

// OK
int sufs_libfs_sys_readdir(struct sufs_libfs_proc *proc, int dirfd,
                           char *prevptr, char *nameptr) {
    LOG_FS("----------- sufs_libfs_sys_readdir dirfd %d, prevptr %s, nameptr %s\n",
           dirfd, prevptr, nameptr);
    struct sufs_libfs_file_mnode *df = sufs_libfs_getfile(proc, dirfd);

    if (!df)
        return -1;

    if (sufs_libfs_mnode_type(df->m) != SUFS_FILE_TYPE_DIR)
        return -1;

    if (!sufs_libfs_mnode_dir_enumerate(df->m, prevptr, nameptr)) {
        return 0;
    }

    return 1;
}

// OK
__ssize_t sufs_libfs_sys_getdents(struct sufs_libfs_proc *proc, int dirfd,
                                  void *buffer, size_t length) {
    LOG_FS("----------- sufs_libfs_sys_getdents dirfd %d, length %ld\n", dirfd,
           length);
    struct sufs_libfs_file_mnode *df = sufs_libfs_getfile(proc, dirfd);

    if (!df)
        return -1;

    if (sufs_libfs_mnode_type(df->m) != SUFS_FILE_TYPE_DIR)
        return -1;

    return sufs_libfs_mnode_dir_getdents(df->m, &(df->off), buffer, length);
}

int sufs_libfs_sys_fsync(struct sufs_libfs_proc *proc, int fd) {
    LOG_FS("----------- sufs_libfs_sys_fsync fd %d\n", fd);
    struct sufs_libfs_file_mnode *f = sufs_libfs_getfile(proc, fd);
    int ret = 0;
    if (!f)
        return -1;
    if (sufs_libfs_mnode_type(f->m) == SUFS_FILE_TYPE_NONE) {
        return -1;
    }

    if (sufs_libfs_mnode_type(f->m) == SUFS_FILE_TYPE_REG) {
        ret = sufs_libfs_sync_page_cache(f->m);
        if (ret < 0) {
            return -1;
        }
    }
    ret = sufs_libfs_cmd_sync();
    return ret;
}

int sufs_libfs_sys_fdatasync(struct sufs_libfs_proc *proc, int fd) {
    LOG_FS("----------- sufs_libfs_sys_fdatasync fd %d\n", fd);
    struct sufs_libfs_file_mnode *f = sufs_libfs_getfile(proc, fd);

    if (!f)
        return -1;

    if (sufs_libfs_mnode_type(f->m) != SUFS_FILE_TYPE_REG)
        return -1;

    return sufs_libfs_sync_page_cache(f->m);
}