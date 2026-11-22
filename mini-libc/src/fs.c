/* SPDX-License-Identifier: MIT */
#include <fs.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include "sys/syscall.h"

int fs_open(const char *path, int mode) {
    return syscall2(SYS_FS, FS_OPEN, (unsigned)path, (unsigned)mode);
}

int fs_close(int fd) {
    return syscall1(SYS_FS, FS_CLOSE, (unsigned)fd);
}

int fs_read(int fd, void *buf, unsigned size) {
    return syscall3(SYS_FS, FS_READ, (unsigned)fd, (unsigned)buf, size);
}

int fs_write(int fd, const void *buf, unsigned size) {
    return syscall3(SYS_FS, FS_WRITE, (unsigned)fd,
                    (unsigned)buf, size);
}

int fs_unlink(const char *path) {
    return syscall1(SYS_FS, FS_UNLINK, (unsigned)path);
}

int fs_mkdir(const char *path) {
    return syscall1(SYS_FS, FS_MKDIR, (unsigned)path);
}

int fs_rmdir(const char *path) {
    return syscall1(SYS_FS, FS_RMDIR, (unsigned)path);
}

int fs_stat(const char *path) {
    return syscall1(SYS_FS, FS_STAT, (unsigned)path);
}

int fs_getcwd(char *buf, unsigned size) {
    return syscall2(SYS_FS, FS_GETCWD, (unsigned)buf, size);
}

int fs_chdir(const char *path) {
    return syscall1(SYS_FS, FS_CHDIR, (unsigned)path);
}

int fs_rename(const char *old_path, const char *new_path) {
    return syscall2(SYS_FS, FS_RENAME, (unsigned)old_path,
                    (unsigned)new_path);
}

int fs_readdir(const char *path, void *buf, unsigned size) {
    return syscall3(SYS_FS, FS_READDIR, (unsigned)path, (unsigned)buf, size);
}

/* ---- FILE over fs fds ---- */

static FILE *file_alloc(void) {
    FILE *f = (FILE *)malloc(sizeof(FILE));
    if (f == 0)
        return 0;   /* malloc already set errno = ENOMEM */
    f->fd = -1;
    f->eof = 0;
    f->err = 0;
    f->kind = FS_FILE_REG;
    f->ungot = -1;
    return f;
}

FILE *fs_fopen(const char *path, const char *mode) {
    int omode;
    if (mode == 0 || (mode[0] != 'r' && mode[0] != 'w' &&
                      mode[0] != 'a')) {
        errno = EINVAL;
        return 0;
    }
    /* kernel only has RDONLY/RDWR; 'w'/'a' both map to RDWR (the
     * caller unlinks first if truncation is wanted — no O_TRUNC yet) */
    omode = (mode[0] == 'r') ? FS_O_RDONLY : FS_O_RDWR;
    int fd = fs_open(path, omode);
    if (fd < 0) {
        errno = (omode == FS_O_RDONLY) ? ENOENT : EACCES;
        return 0;
    }
    FILE *f = file_alloc();
    if (f == 0) {
        fs_close(fd);
        return 0;
    }
    f->fd = fd;
    return f;
}

int fs_fclose(FILE *f) {
    if (f == 0) {
        errno = EINVAL;
        return EOF;
    }
    int rc = 0;
    if (f->fd >= 0) rc = fs_close(f->fd);
    f->fd = -1;
    free(f);
    return rc;
}

size_t fs_fread(void *buf, size_t size, size_t count, FILE *f) {
    if (f == 0 || f->fd < 0 || buf == 0 || size == 0 || count == 0) {
        if (f) f->err = 1;
        errno = EBADF;
        return 0;
    }
    size_t total = size * count;
    size_t done = 0;
    while (done < total) {
        int n = fs_read(f->fd, (char *)buf + done,
                        (unsigned)(total - done));
        if (n < 0) { f->err = 1; errno = EIO; break; }
        if (n == 0) { f->eof = 1; break; }
        done += (size_t)n;
    }
    return done / size;
}

size_t fs_fwrite(const void *buf, size_t size, size_t count, FILE *f) {
    if (f == 0 || f->fd < 0 || buf == 0 || size == 0 || count == 0) {
        if (f) f->err = 1;
        errno = EBADF;
        return 0;
    }
    size_t total = size * count;
    size_t done = 0;
    while (done < total) {
        int n = fs_write(f->fd, (const char *)buf + done,
                         (unsigned)(total - done));
        if (n < 0) { f->err = 1; errno = EIO; break; }
        done += (size_t)n;
    }
    return done / size;
}

int fs_fgetc(FILE *f) {
    unsigned char c;
    if (fs_fread(&c, 1, 1, f) != 1) return EOF;
    return c;
}

int fs_fputc(int c, FILE *f) {
    unsigned char ch = (unsigned char)c;
    if (fs_fwrite(&ch, 1, 1, f) != 1) return EOF;
    return c;
}

int fs_feof(FILE *f) { return f ? f->eof : 0; }
int fs_ferror(FILE *f) { return f ? f->err : 0; }

void fs_clearerr(FILE *f) {
    if (f) { f->eof = 0; f->err = 0; }
}
