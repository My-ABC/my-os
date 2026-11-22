/* SPDX-License-Identifier: MIT */
/* Ring 3 user-mode shell.
 *
 * Portable port of src/shell/shell.c onto the int 0x80 syscall ABI
 * (mini-libc sys/syscall.h). Built as a standalone ELF32 linked with
 * libmyc.a and started with the `run` command. Commands that need
 * kernel-only facilities (ring3/elf/disk, serial diagnostics, kernel
 * FAT32 internals) are omitted; everything else is reimplemented over
 * SYS_IO / SYS_TIME / SYS_PROCESS / SYS_FS / SYS_SIG / SYS_POWER.
 *
 * Known v1 limitations:
 *  - line input uses IO_READ (kernel gets(): echo + backspace), so no
 *    history recall or arrow-key editing;
 *  - `bg <pid>` only works for the current foreground PID (the ABI has
 *    no set-background-by-pid call);
 *  - console output requires this process to be foreground — after
 *    `run` launches a child, shell output is suppressed until the
 *    child exits or `fg <pid>` restores it;
 *  - FS_* paths resolve against the shared kernel cwd, so `cd` here
 *    moves the same directory the kernel shell sees. */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include "sys/syscall.h"
#include "shell_ring3.h"

#define LINE_MAX      128
#define ARGV_MAX      16
#define PATH_MAX      256
#define READDIR_CAP   4096

/* ---- small syscall helpers ---- */

static int fs_stat(const char *path) {
    return (int)syscall1(SYS_FS, FS_STAT, (unsigned)path);
}

static int path_is_dir(const char *path) {
    return fs_stat(path) == -2;
}

static int path_join(const char *base, const char *name,
                     char *out, size_t size) {
    size_t blen = strlen(base);
    size_t nlen = strlen(name);
    int sep = blen > 0 && base[blen - 1] != '/';
    if (blen + (size_t)sep + nlen + 1 > size) return -1;
    memcpy(out, base, blen);
    size_t pos = blen;
    if (sep) out[pos++] = '/';
    memcpy(out + pos, name, nlen + 1);
    return 0;
}

static const char *path_basename(const char *path) {
    const char *base = path;
    for (const char *p = path; *p; ++p)
        if (*p == '/') base = p + 1;
    return base;
}

static void *umalloc(size_t size) {
    return (void *)(unsigned)syscall1(SYS_MEMORY, MEMORY_MALLOC, (unsigned)size);
}

static void ufree(void *p) {
    if (p) syscall1(SYS_MEMORY, MEMORY_FREE, (unsigned)p);
}

/* When dst names an existing directory the source goes inside it:
 * `cp file dir` -> dir/file, same for mv. Ported from shell.c. */
static int resolve_destination(const char *src, const char *dst,
                               char *out, size_t size) {
    if (!path_is_dir(dst)) {
        size_t len = strlen(dst);
        if (len + 1 > size) return -1;
        memcpy(out, dst, len + 1);
        return 0;
    }
    return path_join(dst, path_basename(src), out, size);
}

/* dst inside src? (approximation of fat32_path_within on the typed
 * paths — absolute paths compare exactly, relative ones may differ) */
static int path_within(const char *src, const char *dst) {
    size_t slen = strlen(src);
    if (strncmp(dst, src, slen) != 0) return 0;
    return dst[slen] == '/' || dst[slen] == '\0';
}

/* ---- argument splitting / unescape (ported verbatim) ---- */

static int split_line(char *line, char *argv[], int max_args) {
    int argc = 0;
    char *p = line;

    while (*p && argc < max_args - 1) {
        while (*p == ' ') p++;
        if (*p == '\0') break;
        if (*p == '"') {
            char *close = p + 1;
            while (*close != '\0') {
                if (*close == '\\' && close[1] != '\0') {
                    close += 2;
                    continue;
                }
                if (*close == '"') break;
                ++close;
            }
            if (*close == '"' && close != p + 1) {
                argv[argc++] = ++p;
                while (*p != '\0') {
                    if (*p == '\\' && p[1] != '\0') {
                        p += 2;
                        continue;
                    }
                    if (*p == '"') break;
                    ++p;
                }
                if (*p == '"') {
                    *p = '\0';
                    ++p;
                }
                continue;
            }
        }
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) {
            *p = '\0';
            p++;
        }
    }
    argv[argc] = NULL;
    return argc;
}

static void unescape_arg(char *arg) {
    char *r = arg;
    char *w = arg;
    while (*r != '\0') {
        if (*r == '\\' && r[1] == 'n') {
            *w++ = '\n';
            r += 2;
        } else if (*r == '\\' && r[1] == '\\') {
            *w++ = '\\';
            r += 2;
        } else if (*r == '\\' && r[1] == '"') {
            *w++ = '"';
            r += 2;
        } else {
            if (*r == '\\' && r[1] != '\0') {
                printf("Warning: unknown escape '\\%c' kept literal\n", r[1]);
            }
            *w++ = *r++;
        }
    }
    *w = '\0';
}

/* ---- console helpers ---- */

/* IO_WRITE is a C-string call: emit each NUL-free segment of a
 * binary buffer as its own write. */
static void out_bytes(const unsigned char *p, size_t n) {
    size_t i = 0;
    while (i < n) {
        size_t j = i;
        while (j < n && p[j] != '\0') ++j;
        if (j > i) {
            char tmp[512];
            size_t pos = 0;
            for (size_t k = i; k < j;) {
                size_t chunk = j - k < sizeof(tmp) - 1 ? j - k : sizeof(tmp) - 1;
                memcpy(tmp, p + k, chunk);
                tmp[chunk] = '\0';
                syscall1(SYS_IO, IO_WRITE, (unsigned)tmp);
                k += chunk;
                pos += chunk;
            }
            (void)pos;
        }
        i = j + 1;
    }
}

static int read_line(char *line, int size) {
    char cwd[128];
    if (syscall2(SYS_FS, FS_GETCWD, (unsigned)cwd, sizeof(cwd)) >= 0)
        printf("myos3 %s > ", cwd);
    else
        printf("myos3 > ");
    syscall2(SYS_IO, IO_READ, (unsigned)line, (unsigned)(size - 1));
    return 0;
}

static int parse_pid(const char *s, uint32_t *out) {
    uint32_t pid = 0;
    if (*s == '\0') return -1;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9') return -1;
        pid = pid * 10U + (uint32_t)(*s - '0');
    }
    *out = pid;
    return 0;
}

/* ---- commands ---- */

static int cmd_time(int argc, char *argv[]) {
    int use_12h = 0;
    if (argc > 1) {
        if (strcmp(argv[1], "12") == 0) use_12h = 1;
        else if (strcmp(argv[1], "24") != 0) {
            printf("Usage: time [12|24]\n");
            return -1;
        }
    }
    /* rtc_time_t: u8 sec,min,hour,day,month; u16 year at offset 6
     * (natural alignment, sizeof=8) — Beijing = UTC+8 */
    struct rtc_time {
        uint8_t second, minute, hour, day, month;
        uint16_t year;
    } t;
    memset(&t, 0, sizeof(t));
    if (syscall2(SYS_TIME, TIME_TO, 28800, (unsigned)&t) != 0) {
        printf("Unable to read time\n");
        return -1;
    }
    uint32_t hour = t.hour;
    const char *ampm = "";
    if (use_12h) {
        ampm = hour < 12 ? " AM" : " PM";
        hour = hour % 12;
        if (hour == 0) hour = 12;
    }
    printf("%04u-%02u-%02u %02u:%02u:%02u%s\n",
           t.year, t.month, t.day, hour, t.minute, t.second, ampm);
    return 0;
}

static int cmd_echo(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        printf("%s", argv[i]);
        if (i < argc - 1) printf(" ");
    }
    printf("\n");
    return 0;
}

static int cmd_reboot(int argc, char *argv[]) {
    (void)argc; (void)argv;
    printf("Rebooting...\n");
    syscall1(SYS_POWER, PWR_REBOOT, 0);
    return 0;
}

static int cmd_shutdown(int argc, char *argv[]) {
    (void)argc; (void)argv;
    printf("Shutting down...\n");
    syscall1(SYS_POWER, PWR_SHUTDOWN, 0);
    return 0;
}

static int cmd_help(int argc, char *argv[]) {
    extern const char *const ring3_cmd_names[];
    extern const char *const ring3_cmd_descs[];
    (void)argc; (void)argv;
    printf("Available commands (Ring 3 shell):\n");
    for (int i = 0; ring3_cmd_names[i] != 0; ++i) {
        printf("  %-10s %s\n", ring3_cmd_names[i], ring3_cmd_descs[i]);
    }
    printf("Note: ring3/elf/disk are kernel-shell only.\n");
    return 0;
}

static int cmd_clear(int argc, char *argv[]) {
    (void)argc; (void)argv;
    syscall0(SYS_IO, IO_CLEAR);
    return 0;
}

static int cmd_hello(int argc, char *argv[]) {
    if (argc > 1) {
        printf("Hello, ");
        for (int i = 1; i < argc; i++) {
            printf("%s", argv[i]);
            if (i < argc - 1) printf(" ");
        }
        printf("!\n");
    } else {
        printf("Hello, my-os!\n");
    }
    return 0;
}

static int cmd_ps(int argc, char *argv[]) {
    (void)argc; (void)argv;
    static uint32_t recs[16 * 3];
    int count = (int)syscall2(SYS_PROCESS, PROC_LIST,
                              (unsigned)recs, sizeof(recs));
    if (count < 0) {
        printf("Unable to list processes\n");
        return -1;
    }
    printf("PID  STATE\n");
    for (int i = 0; i < count; ++i) {
        uint32_t pid = recs[i * 3];
        if (pid == 0) continue;
        uint32_t state = recs[i * 3 + 1];
        const char *name = state == 1 ? "READY"
                         : state == 2 ? "RUNNING"
                         : state == 3 ? "DEAD" : "UNUSED";
        printf("%d    %s%s\n", pid, name, recs[i * 3 + 2] ? " (FOREGROUND)" : "");
    }
    return 0;
}

static int cmd_fg(int argc, char *argv[]) {
    uint32_t pid;
    if (argc != 2 || parse_pid(argv[1], &pid) != 0) {
        printf("Usage: fg <pid>\n");
        return -1;
    }
    if (syscall1(SYS_PROCESS, PROC_SET_FG, pid) != 0) {
        printf("No running process with PID %d\n", pid);
        return -1;
    }
    printf("Process %d output is now visible\n", pid);
    return 0;
}

static int cmd_bg(int argc, char *argv[]) {
    uint32_t pid = 0;
    if (argc > 2 || (argc == 2 && parse_pid(argv[1], &pid) != 0)) {
        printf("Usage: bg [pid]\n");
        return -1;
    }
    /* ABI only backgrounds the current foreground process */
    if (syscall1(SYS_PROCESS, PROC_SET_FG, 0) != 0) {
        printf("No foreground process to hide\n");
        return -1;
    }
    printf("Foreground process output is hidden\n");
    (void)pid;
    return 0;
}

static int cmd_kill(int argc, char *argv[]) {
    uint32_t pid;
    if (argc != 2 || parse_pid(argv[1], &pid) != 0) {
        printf("Usage: kill <pid>\n");
        return -1;
    }
    if (syscall2(SYS_SIG, SIG_KILL, pid, 9) != 0) {
        printf("No running process with PID %d\n", pid);
        return -1;
    }
    printf("Killed process %d\n", pid);
    return 0;
}

static int cmd_ls(int argc, char *argv[]) {
    char target[PATH_MAX];
    if (argc <= 1) {
        if (syscall2(SYS_FS, FS_GETCWD, (unsigned)target, sizeof(target)) < 0)
            return -1;
    } else {
        strncpy(target, argv[1], sizeof(target) - 1);
        target[sizeof(target) - 1] = '\0';
    }
    char *buf = (char *)umalloc(READDIR_CAP);
    if (buf == 0) { printf("Out of memory\n"); return -1; }
    int n = (int)syscall3(SYS_FS, FS_READDIR, (unsigned)target,
                          (unsigned)buf, READDIR_CAP);
    if (n < 0) {
        ufree(buf);
        printf("Unable to list %s\n", target);
        return -1;
    }
    printf("Listing %s\n", target);
    int pos = 0;
    while (pos < n) {
        int is_dir = buf[pos] == 'd';
        char *name = buf + pos + 2;
        char *eol = name;
        while (eol < buf + n && *eol != '\n') ++eol;
        *eol = '\0';
        pos = (int)(eol - buf) + 1;
        if (is_dir) {
            printf("%s/   <DIR>\n", name);
        } else {
            char full[PATH_MAX];
            int sz = -1;
            if (path_join(target, name, full, sizeof(full)) == 0)
                sz = fs_stat(full);
            printf("%s   %d bytes\n", name, sz);
        }
    }
    ufree(buf);
    return 0;
}

static int cmd_cat(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: cat <file>\n");
        return -1;
    }
    int fd = (int)syscall2(SYS_FS, FS_OPEN, (unsigned)argv[1],
                           SYSFS_O_RDONLY);
    if (fd < 0) {
        printf("Unable to read %s\n", argv[1]);
        return -1;
    }
    static unsigned char buf[1024];
    int n;
    while ((n = (int)syscall3(SYS_FS, FS_READ, (unsigned)fd,
                              (unsigned)buf, sizeof(buf))) > 0) {
        out_bytes(buf, (size_t)n);
    }
    syscall1(SYS_FS, FS_CLOSE, (unsigned)fd);
    printf("\n");
    return 0;
}

static int cmd_write(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: write <file> <text>\n");
        return -1;
    }
    char text[1024];
    size_t off = 0;
    for (int i = 2; i < argc; i++) {
        if (i > 2 && off + 1 < sizeof(text)) text[off++] = ' ';
        size_t len = strlen(argv[i]);
        if (len > sizeof(text) - 1 - off) len = sizeof(text) - 1 - off;
        memcpy(text + off, argv[i], len);
        off += len;
    }
    /* no truncate syscall: unlink first so stale tail bytes vanish */
    syscall1(SYS_FS, FS_UNLINK, (unsigned)argv[1]);
    int fd = (int)syscall2(SYS_FS, FS_OPEN, (unsigned)argv[1],
                           SYSFS_O_RDWR);
    if (fd < 0) {
        printf("Unable to write %s\n", argv[1]);
        return -1;
    }
    int n = (int)syscall3(SYS_FS, FS_WRITE, (unsigned)fd,
                          (unsigned)text, (unsigned)off);
    syscall1(SYS_FS, FS_CLOSE, (unsigned)fd);
    if (n < 0) {
        printf("Unable to write %s\n", argv[1]);
        return -1;
    }
    printf("Wrote %d bytes to %s\n", n, argv[1]);
    return 0;
}

/* copy one file src->dst via fd I/O (dst already unlinked/truncated) */
static int copy_file_fd(const char *src, const char *dst) {
    int sfd = (int)syscall2(SYS_FS, FS_OPEN, (unsigned)src, SYSFS_O_RDONLY);
    if (sfd < 0) return -1;
    syscall1(SYS_FS, FS_UNLINK, (unsigned)dst);
    int dfd = (int)syscall2(SYS_FS, FS_OPEN, (unsigned)dst, SYSFS_O_RDWR);
    if (dfd < 0) { syscall1(SYS_FS, FS_CLOSE, (unsigned)sfd); return -1; }
    static unsigned char buf[4096];
    int n, rc = 0;
    while ((n = (int)syscall3(SYS_FS, FS_READ, (unsigned)sfd,
                              (unsigned)buf, sizeof(buf))) > 0) {
        int w = (int)syscall3(SYS_FS, FS_WRITE, (unsigned)dfd,
                              (unsigned)buf, (unsigned)n);
        if (w != n) { rc = -1; break; }
    }
    if (n < 0) rc = -1;
    syscall1(SYS_FS, FS_CLOSE, (unsigned)sfd);
    syscall1(SYS_FS, FS_CLOSE, (unsigned)dfd);
    return rc;
}

static int cp_recursive(const char *src, const char *dst, int depth) {
    if (depth > 32) return -1;
    if (!path_is_dir(src)) {
        return copy_file_fd(src, dst);
    }
    if (syscall1(SYS_FS, FS_MKDIR, (unsigned)dst) != 0) return -1;

    /* collect entries first: the callback buffer is per-call, but the
     * child recursion needs `src`/`dst` paths — build them per entry */
    char *buf = (char *)umalloc(READDIR_CAP);
    if (buf == 0) return -1;
    int n = (int)syscall3(SYS_FS, FS_READDIR, (unsigned)src,
                          (unsigned)buf, READDIR_CAP);
    if (n < 0) { ufree(buf); return -1; }
    int rc = 0, pos = 0;
    while (pos < n && rc == 0) {
        int is_dir = buf[pos] == 'd';
        char *name = buf + pos + 2;
        char *eol = name;
        while (eol < buf + n && *eol != '\n') ++eol;
        *eol = '\0';
        pos = (int)(eol - buf) + 1;
        char cs[PATH_MAX], cd[PATH_MAX];
        if (path_join(src, name, cs, sizeof(cs)) != 0 ||
            path_join(dst, name, cd, sizeof(cd)) != 0) { rc = -1; break; }
        rc = cp_recursive(cs, cd, depth + 1);
        (void)is_dir;
    }
    ufree(buf);
    return rc;
}

static int cmd_cp(int argc, char *argv[]) {
    int recursive = 0, first = 1;
    if (argc > 1 && strcmp(argv[1], "-r") == 0) {
        recursive = 1;
        first = 2;
    }
    if (argc - first != 2) {
        printf("Usage: cp [-r] <src> <dst>\n");
        return -1;
    }
    char src[PATH_MAX], dst[PATH_MAX], dst_final[PATH_MAX];
    strncpy(src, argv[first], sizeof(src) - 1); src[sizeof(src)-1] = 0;
    strncpy(dst, argv[first + 1], sizeof(dst) - 1); dst[sizeof(dst)-1] = 0;
    if (resolve_destination(src, dst, dst_final, sizeof(dst_final)) != 0) {
        printf("Path is too long\n");
        return -1;
    }
    if (recursive && path_within(src, dst_final)) {
        printf("Cannot copy %s into itself\n", src);
        return -1;
    }
    int rc = recursive ? cp_recursive(src, dst_final, 0)
                       : copy_file_fd(src, dst_final);
    if (rc != 0) {
        printf("Unable to copy %s to %s\n", src, dst_final);
        return -1;
    }
    printf("Copied %s to %s\n", src, dst_final);
    return 0;
}

static int cmd_mv(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: mv <src> <dst>\n");
        return -1;
    }
    char dst_final[PATH_MAX];
    if (resolve_destination(argv[1], argv[2], dst_final,
                            sizeof(dst_final)) != 0) {
        printf("Path is too long\n");
        return -1;
    }
    if (syscall2(SYS_FS, FS_RENAME, (unsigned)argv[1],
                 (unsigned)dst_final) != 0) {
        printf("Unable to move %s to %s\n", argv[1], dst_final);
        return -1;
    }
    printf("Moved %s to %s\n", argv[1], dst_final);
    return 0;
}

static int cmd_rm(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: rm <file>\n");
        return -1;
    }
    if (syscall1(SYS_FS, FS_UNLINK, (unsigned)argv[1]) != 0) {
        printf("Unable to delete %s\n", argv[1]);
        return -1;
    }
    printf("Deleted %s\n", argv[1]);
    return 0;
}

static int cmd_cd(int argc, char *argv[]) {
    if (argc > 2) {
        printf("Usage: cd [directory]\n");
        return -1;
    }
    const char *target = argc == 2 ? argv[1] : "/";
    if (syscall1(SYS_FS, FS_CHDIR, (unsigned)target) != 0) {
        printf("Directory not found: %s\n", target);
        return -1;
    }
    char cwd[128];
    if (syscall2(SYS_FS, FS_GETCWD, (unsigned)cwd, sizeof(cwd)) >= 0)
        printf("Current directory: %s\n", cwd);
    return 0;
}

static int cmd_mkdir(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: mkdir <directory>\n");
        return -1;
    }
    if (syscall1(SYS_FS, FS_MKDIR, (unsigned)argv[1]) != 0) {
        printf("Unable to create %s\n", argv[1]);
        return -1;
    }
    printf("Created %s\n", argv[1]);
    return 0;
}

/* recursive delete; refuses "/" and the current directory (approximation:
 * the FS layer resolves vs the shared cwd, so a relative path that
 * reaches cwd cannot be detected reliably — documented limitation) */
static int rmdir_recursive(const char *path, int depth) {
    if (depth > 32) return -1;
    if (!path_is_dir(path)) return -1;

    char *buf = (char *)umalloc(READDIR_CAP);
    if (buf == 0) return -1;
    int n = (int)syscall3(SYS_FS, FS_READDIR, (unsigned)path,
                          (unsigned)buf, READDIR_CAP);
    if (n < 0) { ufree(buf); return -1; }
    int rc = 0, pos = 0;
    while (pos < n && rc == 0) {
        int is_dir = buf[pos] == 'd';
        char *name = buf + pos + 2;
        char *eol = name;
        while (eol < buf + n && *eol != '\n') ++eol;
        *eol = '\0';
        pos = (int)(eol - buf) + 1;
        char child[PATH_MAX];
        if (path_join(path, name, child, sizeof(child)) != 0) { rc = -1; break; }
        if (is_dir) rc = rmdir_recursive(child, depth + 1);
        else rc = syscall1(SYS_FS, FS_UNLINK, (unsigned)child) == 0 ? 0 : -1;
    }
    ufree(buf);
    if (rc != 0) return -1;
    return syscall1(SYS_FS, FS_RMDIR, (unsigned)path) == 0 ? 0 : -1;
}

static int cmd_rmdir(int argc, char *argv[]) {
    int force = 0;
    const char *target;
    if (argc == 2) target = argv[1];
    else if (argc == 3 && strcmp(argv[1], "-f") == 0) {
        force = 1;
        target = argv[2];
    } else {
        printf("Usage: rmdir [-f] <directory>\n");
        return -1;
    }
    if (strcmp(target, "/") == 0) {
        printf("Cannot remove the root directory\n");
        return -1;
    }
    int rc = force ? rmdir_recursive(target, 0)
                   : (syscall1(SYS_FS, FS_RMDIR, (unsigned)target) == 0 ? 0 : -1);
    if (rc != 0) {
        printf("Unable to remove %s%s\n", target,
               force ? "" : " (use -f to remove a non-empty directory)");
        return -1;
    }
    printf("Removed %s%s\n", target, force ? " recursively" : "");
    return 0;
}

static int cmd_run(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: run <elf32-file>\n");
        return -1;
    }
    if (syscall1(SYS_PROCESS, PROC_EXEC, (unsigned)argv[1]) != 0) {
        printf("Failed to run %s\n", argv[1]);
        return -1;
    }
    printf("Started %s\n", argv[1]);
    return 0;
}

/* ---- command table ---- */

typedef int (*cmd_func)(int argc, char *argv[]);
typedef struct { const char *name; cmd_func func; const char *desc; } cmd_t;

const char *const ring3_cmd_names[] = {
    "time", "echo", "reboot", "shutdown", "help", "clear", "hello",
    "ps", "fg", "bg", "kill", "ls", "cat", "write", "cp", "mv",
    "rm", "cd", "mkdir", "rmdir", "run", 0
};
const char *const ring3_cmd_descs[] = {
    "Show current time", "Print text", "Reboot system",
    "Shutdown system", "Show this help", "Clear screen",
    "Print hello message", "List Ring 3 processes",
    "Show a Ring 3 process in foreground",
    "Hide the foreground process output", "Terminate a process by PID",
    "List a FAT32 directory", "Print a FAT32 file",
    "Write text to a FAT32 file", "Copy a FAT32 file or directory (-r)",
    "Move or rename a FAT32 file or directory",
    "Delete a FAT32 file", "Change FAT32 directory",
    "Create a FAT32 directory", "Remove a directory (-f recursive)",
    "Load and run an ELF32 file from disk"
};

static cmd_t ring3_cmd_table[] = {
    {"time",     cmd_time,     "Show current time"},
    {"echo",     cmd_echo,     "Print text"},
    {"reboot",   cmd_reboot,   "Reboot system"},
    {"shutdown", cmd_shutdown, "Shutdown system"},
    {"help",     cmd_help,     "Show this help"},
    {"clear",    cmd_clear,    "Clear screen"},
    {"hello",    cmd_hello,    "Print hello message"},
    {"ps",       cmd_ps,       "List Ring 3 processes"},
    {"fg",       cmd_fg,       "Show a Ring 3 process in foreground"},
    {"bg",       cmd_bg,       "Hide the foreground process output"},
    {"kill",     cmd_kill,     "Terminate a process by PID"},
    {"ls",       cmd_ls,       "List a FAT32 directory"},
    {"cat",      cmd_cat,      "Print a FAT32 file"},
    {"write",    cmd_write,    "Write text to a FAT32 file"},
    {"cp",       cmd_cp,       "Copy a FAT32 file or directory (-r)"},
    {"mv",       cmd_mv,       "Move or rename a FAT32 file or directory"},
    {"rm",       cmd_rm,       "Delete a FAT32 file"},
    {"cd",       cmd_cd,       "Change FAT32 directory"},
    {"mkdir",    cmd_mkdir,    "Create a FAT32 directory"},
    {"rmdir",    cmd_rmdir,    "Remove a directory (-f recursive)"},
    {"run",      cmd_run,      "Load and run an ELF32 file from disk"},
    {0, 0, 0}
};

int ring3_shell_main(void) {
    char line[LINE_MAX];
    char *argv[ARGV_MAX];

    printf("MyOS Ring 3 Shell\n");
    printf("Type 'help' for available commands.\n");

    for (;;) {
        read_line(line, sizeof(line));
        int argc = split_line(line, argv, ARGV_MAX);
        for (int i = 0; i < argc; ++i) unescape_arg(argv[i]);
        if (argc == 0) continue;
        int found = 0;
        for (int i = 0; ring3_cmd_table[i].name != 0; ++i) {
            if (strcmp(argv[0], ring3_cmd_table[i].name) == 0) {
                ring3_cmd_table[i].func(argc, argv);
                found = 1;
                break;
            }
        }
        if (!found) printf("Unknown command: %s\n", argv[0]);
    }
    return 0;
}

int main(void) {
    return ring3_shell_main();
}
