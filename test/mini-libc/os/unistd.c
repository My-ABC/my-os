/* SPDX-License-Identifier: MIT */
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fs.h>

static int fails;
#define CHK(c, n) do { if (c) printf("ok %s\n", n); \
    else { printf("FAIL %s\n", n); fails++; } } while (0)

int main(void) {
    CHK(getpid() > 0, "getpid");

    char cwd[128];
    CHK(getcwd(cwd, sizeof(cwd)) != 0 && cwd[0] == '/', "getcwd");

    CHK(access("tdata.txt", 0) == 0, "access-exists");
    errno = 0;
    CHK(access("nope.xyz", 0) == -1 && errno == ENOENT, "access-missing");

    /* fd-level read/write on the injected file */
    int fd = fs_open("tdata.txt", FS_O_RDONLY);
    CHK(fd >= 0, "open");
    char buf[32];
    ssize_t n = read(fd, buf, sizeof(buf));
    CHK(n > 0, "read");
    close(fd);

    /* fd 0/1/2 map to the console: write(1)/write(2) go through
     * IO_WRITE (VGA + COM3), read(0) reads one console line. */
    {
        const char *w1 = "W1-OK\n";
        ssize_t wn = write(1, w1, 6);
        CHK(wn == 6, "write-fd1-ret");
        const char *w2 = "W2-OK\n";
        CHK(write(2, w2, 6) == 6, "write-fd2-ret");
        CHK(write(1, 0, 0) == 0, "write-fd1-empty");
    }

    errno = 0;
    CHK(lseek(fd, 0, 0) == -1 && errno == ENOSYS, "lseek-enosys");
    errno = 0;
    CHK(fork() == -1 && errno == ENOSYS, "fork-enosys");
    errno = 0;
    CHK(getppid() == -1 && errno == ENOSYS, "getppid-enosys");

    CHK(unlink("unlinked-by-unistd.tmp") == -1 || 1, "unlink-callable");

    printf("DONE-unistd fails=%d\n", fails);
    exit(fails);
}
