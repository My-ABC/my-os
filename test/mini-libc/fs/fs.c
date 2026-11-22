/* SPDX-License-Identifier: MIT */
#include <fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHK(c, n) do { if (c) printf("ok %s\n", n); \
    else { printf("FAIL %s\n", n); fails++; } } while (0)

int main(void) {
    char cwd[128];
    CHK(fs_getcwd(cwd, sizeof(cwd)) >= 0, "getcwd");
    printf("  cwd=%s\n", cwd);

    CHK(fs_stat("tfs.txt") > 0, "stat-file");
    CHK(fs_stat("definitely-missing.xyz") < 0, "stat-missing");

    /* read raw fd */
    int fd = fs_open("tfs.txt", FS_O_RDONLY);
    CHK(fd >= 0, "open-rdonly");
    char buf[64];
    int n = fs_read(fd, buf, sizeof(buf));
    CHK(n > 0 && memcmp(buf, "fs-fixture-data-123", 19) == 0, "read-raw");
    fs_close(fd);

    /* write to an existing file (RDWR, no O_TRUNC) */
    int wfd = fs_open("tfs.txt", FS_O_RDWR);
    CHK(wfd >= 0, "open-rdwr");
    if (wfd >= 0) {
        const char *msg = "W";
        fs_write(wfd, msg, 1);
        fs_close(wfd);
    }

    /* directory ops */
    CHK(fs_mkdir("tdir") == 0 || fs_stat("tdir") == -2, "mkdir-or-exists");
    CHK(fs_chdir("tdir") == 0, "chdir-into");
    CHK(fs_getcwd(cwd, sizeof(cwd)) >= 0 && strstr(cwd, "tdir") != 0,
        "cwd-inside");
    CHK(fs_chdir("..") == 0, "chdir-parent");

    CHK(fs_rename("tfs.txt", "tfs2.txt") == 0, "rename");
    CHK(fs_stat("tfs2.txt") > 0 && fs_stat("tfs.txt") < 0,
        "rename-moved");

    /* readdir record: kind byte ('-' file / 'd' dir) + ' ' + name + '\n' */
    char listing[512];
    int rn = fs_readdir(".", listing, sizeof(listing));
    CHK(rn > 0, "readdir");
    int saw_tdata2 = 0, saw_tdir = 0;
    for (int i = 0; i < rn; ) {
        char kind = listing[i];
        const char *rec = listing + i + 2;   /* skip kind + space */
        const char *nl = strchr(rec, '\n');
        int len = nl ? (int)(nl - rec) : rn - i - 2;
        if (kind == '-' && len == 8 && memcmp(rec, "tfs2.txt", 8) == 0)
            saw_tdata2 = 1;
        if (kind == 'd' && len == 4 && memcmp(rec, "tdir", 4) == 0)
            saw_tdir = 1;
        i += 2 + len + (nl ? 1 : 0);
    }
    CHK(saw_tdata2, "readdir-file");
    CHK(saw_tdir, "readdir-dir");

    CHK(fs_unlink("tfs2.txt") == 0, "unlink");
    CHK(fs_rmdir("tdir") == 0, "rmdir");

    printf("DONE-fs fails=%d\n", fails);
    exit(fails);
}
