/* SPDX-License-Identifier: MIT */
/* Ring 3 user-mode shell — portable core stage.
 *
 * This file intentionally contains NO direct syscall usage: everything
 * goes through the mini-libc public API (stdio/string/time). Kernel
 * services that mini-libc does not expose yet (raw key events, cursor
 * positioning, fs/process/power/signal calls) are deferred until the
 * library grows proper wrappers — the v1 syscall-based command set is
 * preserved in git history (commit e23e7f3).
 *
 * What lives here now:
 *  - pure computation: argument splitting, escape handling, path
 *    utilities, command history;
 *  - stdio-backed commands: time (via time.h), echo, hello, help,
 *    exit (returning from main exits through crt0);
 *  - line input via mini-libc getline() (IO_READ: echo + backspace,
 *    no arrow keys/history recall yet — needs a console API).
 *
 * Deferred until mini-libc provides the APIs:
 *  - ls/cat/write/cp/mv/rm/cd/mkdir/rmdir/pwd (SYS_FS)
 *  - run/ps/fg/bg/kill (SYS_PROCESS/SYS_SIG)
 *  - reboot/shutdown/clear (SYS_POWER/SYS_IO cursor ops)
 *  - arrow-key editing, history recall, Ctrl+C handling (raw keys,
 *    cursor get/set, SIG_SIGNAL)
 *  - foreground arbitration for `run` children (PROC_LIST/SET_FG) */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include "shell_ring3.h"

#define LINE_MAX      128
#define ARGV_MAX      16
#define HISTORY_MAX   16

/* ---- argument splitting / unescape (pure, ported from shell.c) ---- */

int split_line(char *line, char *argv[], int max_args) {
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

void unescape_arg(char *arg) {
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

/* ---- path utilities (pure; used by deferred fs commands) ---- */

#define PATH_MAX 256

int path_join(const char *base, const char *name,
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

const char *path_basename(const char *path) {
    const char *base = path;
    for (const char *p = path; *p; ++p)
        if (*p == '/') base = p + 1;
    return base;
}

/* dst inside src? (approximation of fat32_path_within on typed paths) */
int path_within(const char *src, const char *dst) {
    size_t slen = strlen(src);
    if (strncmp(dst, src, slen) != 0) return 0;
    return dst[slen] == '/' || dst[slen] == '\0';
}

/* Canonicalize an absolute path textually (resolve "." and ".."). */
int path_normalize(const char *path, char *out, size_t size) {
    static char seg[64][64];
    int n = 0;
    const char *p = path;
    if (*p != '/') return -1;
    while (*p != '\0') {
        while (*p == '/') ++p;
        const char *s = p;
        while (*p != '\0' && *p != '/') ++p;
        int len = (int)(p - s);
        if (len == 0) break;
        if (len == 1 && s[0] == '.') continue;
        if (len == 2 && s[0] == '.' && s[1] == '.') {
            if (n > 0) --n;
            continue;
        }
        if (n >= 64 || len >= 64) return -1;
        memcpy(seg[n], s, (size_t)len);
        seg[n][len] = '\0';
        ++n;
    }
    size_t olen = 0;
    if (size < 2) return -1;
    out[olen++] = '/';
    for (int i = 0; i < n; ++i) {
        size_t l = strlen(seg[i]);
        if (olen + l + 1 > size) return -1;
        memcpy(out + olen, seg[i], l);
        olen += l;
        out[olen++] = '/';
    }
    if (olen > 1) --olen;
    out[olen] = '\0';
    return 0;
}

int parse_pid(const char *s, uint32_t *out) {
    uint32_t pid = 0;
    if (*s == '\0') return -1;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9') return -1;
        pid = pid * 10U + (uint32_t)(*s - '0');
    }
    *out = pid;
    return 0;
}

/* ---- command history (pure) ---- */

static char history[HISTORY_MAX][LINE_MAX];
static int  history_count;

static void history_push(const char *line) {
    size_t length = strlen(line);
    int blank = 1;
    for (size_t i = 0; i < length; ++i) {
        if (line[i] != ' ') { blank = 0; break; }
    }
    if (blank) return;
    if (history_count > 0 &&
        strcmp(history[history_count - 1], line) == 0)
        return;
    if (history_count == HISTORY_MAX) {
        memmove(&history[0], &history[1],
                (HISTORY_MAX - 1) * sizeof(history[0]));
        --history_count;
    }
    strncpy(history[history_count], line, LINE_MAX - 1);
    history[history_count][LINE_MAX - 1] = '\0';
    ++history_count;
}

/* ---- console I/O (mini-libc stdio only) ---- */

static void read_line(char *line, int size) {
    printf("myos3 > ");
    if (getline(line, size) < 0)
        line[0] = '\0';
}

/* ---- commands available without new mini-libc APIs ---- */

static int cmd_time(int argc, char *argv[]) {
    (void)argc; (void)argv;
    time_t t = time(NUL);
    struct tm *lt = localtime(&t);
    char buf[32];
    if (lt == NUL || strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", lt) == 0) {
        printf("Unable to read time\n");
        return -1;
    }
    printf("%s (UTC+8)\n", buf);
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

static int cmd_help(int argc, char *argv[]) {
    (void)argc; (void)argv;
    printf("Available commands (Ring 3 shell, stdio stage):\n");
    printf("  time      Show current time (UTC+8)\n");
    printf("  echo      Print text\n");
    printf("  hello     Print hello message\n");
    printf("  help      Show this help\n");
    printf("  exit      Return to the kernel shell\n");
    printf("Deferred until mini-libc grows fs/process/console APIs:\n");
    printf("  ls cat write cp mv rm cd mkdir rmdir pwd run ps fg bg\n");
    printf("  kill reboot shutdown clear\n");
    return 0;
}

static int cmd_exit(int argc, char *argv[]) {
    (void)argc; (void)argv;
    printf("Leaving Ring 3 shell...\n");
    return 0;   /* main returns -> crt0 exits via PROC_EXIT */
}

/* ---- command table ---- */

typedef int (*cmd_func)(int argc, char *argv[]);
typedef struct { const char *name; cmd_func func; const char *desc; } cmd_t;

static cmd_t cmd_table[] = {
    {"time",  cmd_time,  "Show current time (UTC+8)"},
    {"echo",  cmd_echo,  "Print text"},
    {"hello", cmd_hello, "Print hello message"},
    {"help",  cmd_help,  "Show this help"},
    {"exit",  cmd_exit,  "Return to the kernel shell"},
    {0, 0, 0}
};

int ring3_shell_main(void) {
    char line[LINE_MAX];
    char *argv[ARGV_MAX];

    printf("MyOS Ring 3 Shell (stdio stage)\n");
    printf("Type 'help' for available commands.\n");

    for (;;) {
        read_line(line, sizeof(line));
        history_push(line);
        int argc = split_line(line, argv, ARGV_MAX);
        for (int i = 0; i < argc; ++i) unescape_arg(argv[i]);
        if (argc == 0) continue;
        int found = 0;
        for (int i = 0; cmd_table[i].name != 0; ++i) {
            if (strcmp(argv[0], cmd_table[i].name) == 0) {
                if (cmd_table[i].func(argc, argv) == 0 &&
                    strcmp(argv[0], "exit") == 0)
                    return 0;
                found = 1;
                break;
            }
        }
        if (!found) printf("Unknown command: %s\n", argv[0]);
    }
}

int main(void) {
    return ring3_shell_main();
}
