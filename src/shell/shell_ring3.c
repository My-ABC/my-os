/* SPDX-License-Identifier: MIT */
/* Ring 3 user-mode shell — final version.
 *
 * A port of the kernel shell's command set onto the mini-libc public
 * API (stdio/string/stdlib/fs/unistd/process/signal/console/power/
 * time). Runs as a normal ELF32 user program built against
 * build/libmyc.a and launched via `run shell3.elf`.
 *
 * Line editing is implemented in user space on top of console.h:
 * con_getchar() returns raw key bytes (printable ASCII plus the
 * KEY_* control codes for arrows), con_cursor_get/set + con_size
 * drive the repaint logic ported from shell.c. Ctrl+C arrives as a
 * delivered SIGINT (the kernel consumes the 0x05 byte inside
 * IO_GETCHAR), so a handler sets a flag that aborts the edit.
 *
 * Foreground arbitration: `run`/`elf` children become the kernel
 * foreground process, which suppresses this shell's IO_WRITE output.
 * While another process is foreground the editor degrades to a
 * silent append-only line reader; when it exits the shell reclaims
 * the foreground slot via proc_set_foreground(getpid()). */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <time.h>
#include <fs.h>
#include <unistd.h>
#include <process.h>
#include <signal.h>
#include <console.h>
#include <power.h>
#include "shell_ring3.h"

#define LINE_MAX      128
#define ARGV_MAX      16
#define HISTORY_MAX   16
#define PATH_MAX      256
#define DIRENT_MAX    128
#define DIRBUF_SIZE   4096
#define COPY_BUF_SIZE 4096
#define CP_DEPTH_MAX  32

/* keyboard control bytes (mirror include/drivers/keyboard.h; the
 * kernel header is not on this TU's include path) */
#define KEY_UP     1
#define KEY_DOWN   2
#define KEY_LEFT   3
#define KEY_RIGHT  4

static volatile sig_atomic_t got_sigint;

static void on_sigint(int sig) {
    (void)sig;
    got_sigint = 1;
}

/* ---- argument splitting / unescape (ported from shell.c) ---- */

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

/* ---- path utilities ---- */

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

int path_within(const char *src, const char *dst) {
    size_t slen = strlen(src);
    if (strncmp(dst, src, slen) != 0) return 0;
    return dst[slen] == '/' || dst[slen] == '\0';
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

/* ---- command history ---- */

static char history[HISTORY_MAX][LINE_MAX];
static int  history_count;
static char history_draft[LINE_MAX];

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

/* ---- console line editor (port of shell.c's editor) ---- */

static uint16_t con_cols = 80;
static uint16_t con_rows = 25;

static int fg_is_other(void) {
    pid_t fg = proc_foreground();
    return fg >= 0 && fg != getpid();
}

static void reclaim_foreground(void) {
    if (proc_foreground() < 0)
        proc_set_foreground(getpid());
}

/* Absolute cursor cell for `offset` chars after the prompt origin,
 * wrapping at the console width. */
static void cursor_to(int offset, uint16_t origin_row,
                      uint16_t origin_col, uint16_t *row, uint16_t *col) {
    uint32_t linear = (uint32_t)origin_col + (uint32_t)offset;
    *row = (uint16_t)(origin_row + linear / con_cols);
    *col = (uint16_t)(linear % con_cols);
}

/* Repaint the input line: erase `old_length` cells after the prompt,
 * print `line`/`length`, leave the cursor at insertion `cursor`. */
static void paint_line(const char *line, int length, int cursor,
                       int old_length, uint16_t origin_row,
                       uint16_t origin_col) {
    uint16_t row, col;
    con_cursor_set(origin_row, origin_col);
    for (int i = 0; i < old_length; ++i)
        putchar(' ');
    con_cursor_set(origin_row, origin_col);
    for (int i = 0; i < length; ++i)
        putchar(line[i]);
    cursor_to(cursor, origin_row, origin_col, &row, &col);
    con_cursor_set(row, col);
}

static void print_prompt(void) {
    char cwd[PATH_MAX];
    if (fs_getcwd(cwd, sizeof(cwd)) >= 0)
        printf("myos %s > ", cwd);
    else
        printf("myos3 > ");
}

static void discard_line(char *line, int *length, int *cursor,
                         int *history_index) {
    printf("^C\n");
    pid_t fg = proc_foreground();
    if (fg >= 0 && fg != getpid())
        kill(fg, SIGINT);
    *length = 0;
    *cursor = 0;
    *history_index = -1;
    line[0] = '\0';
}

static void read_line(char *line, int size) {
    int length = 0;
    int cursor = 0;
    int history_index = -1;
    uint16_t origin_row = 0;
    uint16_t origin_col = 0;
    int origin_valid = 0;

    for (;;) {
        int other_fg = fg_is_other();
        if (!other_fg) {
            if (!origin_valid && length == 0) {
                reclaim_foreground();
                print_prompt();
                if (con_cursor_get(&origin_row, &origin_col) == 0)
                    origin_valid = 1;
            }
        } else {
            origin_valid = 0;
        }

        if (other_fg) {
            /* A foreground child owns the console AND the keyboard.
             * Do not call con_getchar() here: every Ring 3 process
             * shares the kernel key buffer, so reading would drain
             * roughly half of the child's keystrokes into this silent
             * append path. Yield instead of sleeping — TIME_SLEEP
             * busy-waits in kernel mode where the timer cannot
             * preempt, which would starve the child. Buffered keys
             * stay for the child and become our type-ahead once it
             * exits (same policy as the kernel shell, which halts
             * without consuming keys). */
            if (got_sigint) got_sigint = 0;
            sched_yield();
            continue;
        }

        int c = con_getchar();
        /* con_getchar returns -1 both when a signal aborted the wait
         * and when the kernel restarted the syscall after a CPU yield;
         * only the SIGINT flag distinguishes a real Ctrl+C. */
        if (got_sigint) {
            got_sigint = 0;
            if (origin_valid || fg_is_other()) {
                if (!fg_is_other()) {
                    discard_line(line, &length, &cursor, &history_index);
                    origin_valid = 0;
                } else {
                    pid_t fg = proc_foreground();
                    if (fg >= 0) kill(fg, SIGINT);
                    length = 0;
                    cursor = 0;
                    history_index = -1;
                    line[0] = '\0';
                }
            }
            continue;
        }
        if (c < 0)
            continue;

        if (!origin_valid) {
            /* Foreground child owns the console: append silently and
             * ignore editing keys (output is suppressed anyway). */
            if (c == '\n' || c == '\r') {
                line[length] = '\0';
                return;
            }
            if (c == '\b') {
                if (length > 0) --length;
                continue;
            }
            if (c < KEY_UP || c > KEY_RIGHT) {
                if (length < size - 1) line[length++] = (char)c;
            }
            continue;
        }

        if (c == KEY_LEFT || c == KEY_RIGHT) {
            if (c == KEY_LEFT && cursor > 0) {
                --cursor;
            } else if (c == KEY_RIGHT && cursor < length) {
                ++cursor;
            } else {
                continue;
            }
            uint16_t row, col;
            cursor_to(cursor, origin_row, origin_col, &row, &col);
            con_cursor_set(row, col);
            continue;
        }
        if (c == KEY_UP || c == KEY_DOWN) {
            const char *text;
            int new_index = history_index;
            if (c == KEY_UP) {
                if (history_count == 0) continue;
                if (history_index == -1) {
                    int copy = length < (int)sizeof(history_draft) - 1
                               ? length : (int)sizeof(history_draft) - 1;
                    memcpy(history_draft, line, copy);
                    history_draft[copy] = '\0';
                    new_index = history_count - 1;
                } else if (history_index > 0) {
                    --new_index;
                } else {
                    continue;
                }
                text = history[new_index];
            } else {
                if (history_index == -1) continue;
                if (history_index < history_count - 1) {
                    ++new_index;
                    text = history[new_index];
                } else {
                    new_index = -1;
                    text = history_draft;
                }
            }
            history_index = new_index;
            int text_length = (int)strlen(text);
            int old_displayed = length;
            int copy = text_length < size - 1 ? text_length : size - 1;
            memcpy(line, text, copy);
            line[copy] = '\0';
            length = copy;
            cursor = length;
            paint_line(line, length, cursor, old_displayed,
                       origin_row, origin_col);
            continue;
        }
        if (c == '\n' || c == '\r') {
            line[length] = '\0';
            putchar('\n');
            return;
        }
        if (c == '\b') {
            if (cursor > 0) {
                memmove(line + cursor - 1, line + cursor,
                        (size_t)(length - cursor));
                --length;
                --cursor;
                line[length] = '\0';
                if (cursor == length) {
                    /* erasing the last char: cheap backspace sequence,
                     * no full-line repaint (slow under VBE) */
                    putchar('\b');
                    putchar(' ');
                    putchar('\b');
                } else {
                    paint_line(line, length, cursor, length + 1,
                               origin_row, origin_col);
                }
            }
            continue;
        }
        if ((unsigned char)c >= 0x20 && length < size - 1) {
            memmove(line + cursor + 1, line + cursor,
                    (size_t)(length - cursor));
            line[cursor] = (char)c;
            ++length;
            ++cursor;
            line[length] = '\0';
            if (cursor == length) {
                /* appended at end of line: the console cursor is
                 * already in place, echo the single char instead of
                 * repainting the whole line (O(n) putchars per key,
                 * which overflows the keyboard buffer under VBE) */
                putchar(c);
            } else {
                paint_line(line, length, cursor, length - 1,
                           origin_row, origin_col);
            }
        }
    }
}

/* ---- directory listing helpers ---- */

struct dirent {
    char name[256];
    int  is_dir;
};

/* fs_readdir emits "x name\n" records; collect them into a malloc'd
 * array. Returns count or <0. */
static int read_dirents(const char *path, struct dirent **out) {
    static char buf[DIRBUF_SIZE];
    int n = fs_readdir(path, buf, sizeof(buf));
    if (n < 0) return -1;
    struct dirent *list = malloc(sizeof(*list) * DIRENT_MAX);
    if (list == 0) return -1;
    int count = 0;
    char *p = buf;
    char *end = buf + n;
    while (p < end && count < DIRENT_MAX) {
        char *nl = p;
        while (nl < end && *nl != '\n') ++nl;
        if (nl - p >= 3) {
            list[count].is_dir = (p[0] == 'd');
            int len = (int)(nl - p - 2);
            if (len > 255) len = 255;
            memcpy(list[count].name, p + 2, (size_t)len);
            list[count].name[len] = '\0';
            ++count;
        }
        p = nl < end ? nl + 1 : end;
    }
    *out = list;
    return count;
}

/* When dst names an existing directory, the source goes inside it:
 * `cp file dir` -> dir/file, same for mv. */
static int resolve_destination(const char *src, const char *dst,
                               char *out, size_t size) {
    if (fs_stat(dst) != -2) {
        size_t len = strlen(dst);
        if (len + 1 > size) return -1;
        memcpy(out, dst, len + 1);
        return 0;
    }
    const char *base = path_basename(src);
    return path_join(dst, base, out, size);
}

static int copy_file(const char *src, const char *dst) {
    int s = fs_open(src, FS_O_RDONLY);
    if (s < 0) return -1;
    fs_unlink(dst);   /* overwrite: fresh file, ignore ENOENT */
    int d = fs_open(dst, FS_O_RDWR);
    if (d < 0) {
        fs_close(s);
        return -1;
    }
    static char buf[COPY_BUF_SIZE];
    int rc = 0;
    for (;;) {
        int n = fs_read(s, buf, sizeof(buf));
        if (n < 0) { rc = -1; break; }
        if (n == 0) break;
        int off = 0;
        while (off < n) {
            int w = fs_write(d, buf + off, (unsigned)(n - off));
            if (w <= 0) { rc = -1; break; }
            off += w;
        }
        if (rc != 0) break;
    }
    fs_close(s);
    fs_close(d);
    return rc;
}

static int cp_recursive(const char *src, const char *dst, int depth) {
    if (depth > CP_DEPTH_MAX) return -1;
    if (fs_stat(src) != -2)
        return copy_file(src, dst);
    if (fs_mkdir(dst) != 0 && fs_stat(dst) != -2) return -1;

    struct dirent *entries = 0;
    int count = read_dirents(src, &entries);
    if (count < 0) return -1;
    int rc = 0;
    for (int i = 0; i < count; ++i) {
        char cs[PATH_MAX], cd[PATH_MAX];
        if (path_join(src, entries[i].name, cs, sizeof(cs)) != 0 ||
            path_join(dst, entries[i].name, cd, sizeof(cd)) != 0 ||
            cp_recursive(cs, cd, depth + 1) != 0) {
            rc = -1;
            break;
        }
    }
    free(entries);
    return rc;
}

static int rm_recursive(const char *path, int depth) {
    if (depth > CP_DEPTH_MAX) return -1;
    if (fs_stat(path) != -2)
        return fs_unlink(path);
    struct dirent *entries = 0;
    int count = read_dirents(path, &entries);
    if (count < 0) return -1;
    int rc = 0;
    for (int i = 0; i < count; ++i) {
        char child[PATH_MAX];
        if (path_join(path, entries[i].name, child, sizeof(child)) != 0 ||
            rm_recursive(child, depth + 1) != 0) {
            rc = -1;
            break;
        }
    }
    free(entries);
    if (rc == 0) rc = fs_rmdir(path);
    return rc;
}

/* ---- commands ---- */

typedef int (*cmd_func)(int argc, char *argv[]);
typedef struct { const char *name; cmd_func func; const char *desc; } cmd_t;

static int cmd_time(int argc, char *argv[]) {
    int use_12h = 0;
    if (argc > 1) {
        if (strcmp(argv[1], "12") == 0) {
            use_12h = 1;
        } else if (strcmp(argv[1], "24") != 0) {
            printf("Usage: time [12|24]\n");
            return -1;
        }
    }
    time_t t = time(NUL);
    struct tm *lt = localtime(&t);
    if (lt == NUL) {
        printf("Unable to read time\n");
        return -1;
    }
    int hour = lt->tm_hour;
    const char *ampm = "";
    if (use_12h) {
        ampm = hour < 12 ? " AM" : " PM";
        hour = hour % 12;
        if (hour == 0) hour = 12;
    }
    printf("%04d-%02d-%02d %02d:%02d:%02d%s\n",
           lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
           hour, lt->tm_min, lt->tm_sec, ampm);
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
    fflush(stdout);
    power_reboot();
    printf("Reboot failed\n");
    return -1;
}

static int cmd_shutdown(int argc, char *argv[]) {
    (void)argc; (void)argv;
    printf("Shutting down...\n");
    fflush(stdout);
    power_shutdown();
    printf("Shutdown failed\n");
    return -1;
}

static cmd_t cmd_table[];

static int cmd_help(int argc, char *argv[]) {
    (void)argc; (void)argv;
    printf("Available commands:\n");
    for (int i = 0; cmd_table[i].name != 0; i++) {
        printf("  %s", cmd_table[i].name);
        int len = (int)strlen(cmd_table[i].name);
        for (int j = len; j < 10; j++) printf(" ");
        printf(" %s\n", cmd_table[i].desc);
    }
    return 0;
}

static int cmd_clear(int argc, char *argv[]) {
    (void)argc; (void)argv;
    con_clear();
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

static int cmd_ring3(int argc, char *argv[]) {
    (void)argc; (void)argv;
    if (proc_run_usercode() != 0) {
        printf("Unable to start a Ring 3 process (slots full or out of memory)\n");
        return -1;
    }
    printf("Started a Ring 3 process in foreground; use bg <pid> to hide it\n");
    return 0;
}

static int cmd_elf(int argc, char *argv[]) {
    (void)argc; (void)argv;
    if (proc_run_demo() != 0) {
        printf("Unable to run the built-in ELF32 demo\n");
        return -1;
    }
    return 0;
}

static const char *state_name(int state) {
    switch (state) {
        case PROC_ST_READY:   return "READY";
        case PROC_ST_RUNNING: return "RUNNING";
        case PROC_ST_DEAD:    return "DEAD";
        default:              return "UNUSED";
    }
}

static int cmd_ps(int argc, char *argv[]) {
    proc_info_t procs[16];
    (void)argc; (void)argv;
    int n = proc_list(procs, 16);
    if (n < 0) {
        printf("Unable to list processes\n");
        return -1;
    }
    printf("PID  STATE\n");
    pid_t fg = proc_foreground();
    for (int i = 0; i < n; i++) {
        printf("%d    %s%s\n", procs[i].pid, state_name(procs[i].state),
               procs[i].pid == fg ? " (FOREGROUND)" : "");
    }
    return 0;
}

static int parse_pid_arg(const char *s, uint32_t *out) {
    return parse_pid(s, out);
}

static int cmd_fg(int argc, char *argv[]) {
    uint32_t pid = 0;
    if (argc != 2 || parse_pid_arg(argv[1], &pid) != 0) {
        printf("Usage: fg <pid>\n");
        return -1;
    }
    if (proc_set_foreground((pid_t)pid) != 0) {
        printf("No running process with PID %d\n", pid);
        return -1;
    }
    printf("Process %d output is now visible\n", pid);
    return 0;
}

static int cmd_bg(int argc, char *argv[]) {
    uint32_t pid = 0;
    if (argc != 2 || parse_pid_arg(argv[1], &pid) != 0) {
        printf("Usage: bg <pid>\n");
        return -1;
    }
    /* The kernel has a single foreground slot: backgrounding pid means
     * giving the slot back to this shell when pid holds it. */
    if (proc_foreground() == (pid_t)pid)
        proc_set_foreground(getpid());
    printf("Process %d output is hidden\n", pid);
    return 0;
}

static int cmd_kill(int argc, char *argv[]) {
    uint32_t pid = 0;
    if (argc != 2 || parse_pid_arg(argv[1], &pid) != 0) {
        printf("Usage: kill <pid>\n");
        return -1;
    }
    if (kill((pid_t)pid, SIGKILL) != 0) {
        printf("No running process with PID %d\n", pid);
        return -1;
    }
    printf("Killed process %d\n", pid);
    return 0;
}

static int cmd_run(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: run <elf32-file>\n");
        return -1;
    }
    /* PROC_EXEC loads the ELF and schedules it in the foreground;
     * it returns once the process is queued, not when it exits. */
    if (execve(argv[1], 0, 0) != 0) {
        printf("Unable to load or run %s\n", argv[1]);
        return -1;
    }
    return 0;
}

static int cmd_ls(int argc, char *argv[]) {
    char cwd[PATH_MAX];
    const char *target = argc > 1 ? argv[1] : 0;
    if (target == 0) {
        if (fs_getcwd(cwd, sizeof(cwd)) < 0) return -1;
        target = cwd;
    }
    struct dirent *entries = 0;
    int count = read_dirents(target, &entries);
    if (count < 0) {
        printf("Unable to list %s\n", target);
        return -1;
    }
    for (int i = 0; i < count; ++i)
        printf("%s%s\n", entries[i].name,
               entries[i].is_dir ? "/" : "");
    free(entries);
    return 0;
}

static int cmd_cat(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: cat <file>\n");
        return -1;
    }
    int fd = fs_open(argv[1], FS_O_RDONLY);
    if (fd < 0) {
        printf("Unable to read %s\n", argv[1]);
        return -1;
    }
    static char buf[COPY_BUF_SIZE];
    for (;;) {
        int n = fs_read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        fwrite(buf, 1, (size_t)n, stdout);
    }
    fs_close(fd);
    printf("\n");
    return 0;
}

static int cmd_write(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: write <file> <text>\n");
        return -1;
    }
    char text[1024];
    size_t offset = 0;
    for (int i = 2; i < argc; i++) {
        if (i > 2 && offset + 1 < sizeof(text)) text[offset++] = ' ';
        size_t len = strlen(argv[i]);
        if (len > sizeof(text) - 1 - offset) len = sizeof(text) - 1 - offset;
        memcpy(text + offset, argv[i], len);
        offset += len;
    }
    text[offset] = '\0';
    /* Open first so a failure leaves the existing file untouched; the
     * fd stores the path, so unlinking afterwards still truncates. */
    int fd = fs_open(argv[1], FS_O_RDWR);
    if (fd < 0) {
        printf("Unable to write %s\n", argv[1]);
        return -1;
    }
    fs_unlink(argv[1]);   /* truncate semantics: start from a fresh file */
    size_t off = 0;
    while (off < offset) {
        int w = fs_write(fd, text + off, (unsigned)(offset - off));
        if (w <= 0) break;
        off += (size_t)w;
    }
    fs_close(fd);
    printf("Wrote %d bytes to %s\n", (int)offset, argv[1]);
    return 0;
}

static int cmd_cp(int argc, char *argv[]) {
    int recursive = 0;
    int first = 1;
    if (argc > 1 && strcmp(argv[1], "-r") == 0) {
        recursive = 1;
        first = 2;
    }
    if (argc - first != 2) {
        printf("Usage: cp [-r] <src> <dst>\n");
        return -1;
    }
    char dst_final[PATH_MAX];
    if (resolve_destination(argv[first], argv[first + 1],
                            dst_final, sizeof(dst_final)) != 0) {
        printf("Path is too long\n");
        return -1;
    }
    if (recursive && path_within(argv[first], dst_final)) {
        printf("Cannot copy %s into itself\n", argv[first]);
        return -1;
    }
    int rc = recursive ? cp_recursive(argv[first], dst_final, 0)
                       : copy_file(argv[first], dst_final);
    if (rc != 0) {
        printf("Unable to copy %s to %s\n", argv[first], dst_final);
        return -1;
    }
    printf("Copied %s to %s\n", argv[first], dst_final);
    return 0;
}

static int cmd_mv(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: mv <src> <dst>\n");
        return -1;
    }
    char dst_final[PATH_MAX];
    if (resolve_destination(argv[1], argv[2],
                            dst_final, sizeof(dst_final)) != 0) {
        printf("Path is too long\n");
        return -1;
    }
    if (fs_stat(argv[1]) == -2 && path_within(argv[1], dst_final)) {
        printf("Cannot move %s into itself\n", argv[1]);
        return -1;
    }
    if (fs_rename(argv[1], dst_final) != 0) {
        printf("Unable to move %s to %s\n", argv[1], dst_final);
        return -1;
    }
    printf("Moved %s to %s\n", argv[1], dst_final);
    return 0;
}

static int cmd_rm(int argc, char *argv[]) {
    int force = 0;
    int idx = 1;
    if (argc > 1 && strcmp(argv[1], "-f") == 0) {
        force = 1;
        idx = 2;
    }
    if (argc - idx != 1) {
        printf("Usage: rm [-f] <file-or-dir>\n");
        return -1;
    }
    int rc;
    if (fs_stat(argv[idx]) == -2) {
        if (!force) {
            printf("%s is a directory (use rm -f)\n", argv[idx]);
            return -1;
        }
        rc = rm_recursive(argv[idx], 0);
    } else {
        rc = fs_unlink(argv[idx]);
    }
    if (rc != 0) {
        printf("Unable to remove %s\n", argv[idx]);
        return -1;
    }
    printf("Removed %s\n", argv[idx]);
    return 0;
}

static int cmd_cd(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: cd <dir>\n");
        return -1;
    }
    if (fs_chdir(argv[1]) != 0) {
        printf("Unable to change to %s\n", argv[1]);
        return -1;
    }
    return 0;
}

static int cmd_mkdir(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: mkdir <dir>\n");
        return -1;
    }
    if (fs_mkdir(argv[1]) != 0) {
        printf("Unable to create %s\n", argv[1]);
        return -1;
    }
    printf("Created %s\n", argv[1]);
    return 0;
}

static int cmd_rmdir(int argc, char *argv[]) {
    int force = 0;
    int idx = 1;
    if (argc > 1 && strcmp(argv[1], "-f") == 0) {
        force = 1;
        idx = 2;
    }
    if (argc - idx != 1) {
        printf("Usage: rmdir [-f] <dir>\n");
        return -1;
    }
    int rc = force ? rm_recursive(argv[idx], 0) : fs_rmdir(argv[idx]);
    if (rc != 0) {
        printf("Unable to remove %s\n", argv[idx]);
        return -1;
    }
    printf("Removed %s\n", argv[idx]);
    return 0;
}

static int cmd_exit(int argc, char *argv[]) {
    (void)argc; (void)argv;
    printf("Leaving Ring 3 shell...\n");
    return 0;
}

static cmd_t cmd_table[] = {
    {"time",     cmd_time,     "Show current time"},
    {"echo",     cmd_echo,     "Print text"},
    {"reboot",   cmd_reboot,   "Reboot system"},
    {"shutdown", cmd_shutdown, "Shutdown system"},
    {"help",     cmd_help,     "Show this help"},
    {"clear",    cmd_clear,    "Clear screen"},
    {"hello",    cmd_hello,    "Print hello message"},
    {"ring3",    cmd_ring3,    "Start one Ring 3 process in background"},
    {"ps",       cmd_ps,       "List Ring 3 processes"},
    {"fg",       cmd_fg,       "Show a Ring 3 process in foreground"},
    {"bg",       cmd_bg,       "Hide a Ring 3 process output"},
    {"kill",     cmd_kill,     "Terminate a process by PID"},
    {"elf",      cmd_elf,      "Load the built-in ELF32 program"},
    {"ls",       cmd_ls,       "List a FAT32 directory"},
    {"cat",      cmd_cat,      "Print a FAT32 file"},
    {"write",    cmd_write,    "Write text to a FAT32 file"},
    {"cp",       cmd_cp,       "Copy a FAT32 file"},
    {"mv",       cmd_mv,       "Move or rename a FAT32 file or directory"},
    {"rm",       cmd_rm,       "Delete a FAT32 file"},
    {"cd",       cmd_cd,       "Change FAT32 directory"},
    {"mkdir",    cmd_mkdir,    "Create a FAT32 directory"},
    {"rmdir",    cmd_rmdir,    "Remove an empty FAT32 directory"},
    {"run",      cmd_run,      "Load and run an ELF32 file from disk"},
    {"exit",     cmd_exit,     "Return to the kernel shell"},
    {0, 0, 0}
};

int ring3_shell_main(void) {
    char line[LINE_MAX];
    char *argv[ARGV_MAX];
    uint16_t rows = 0, cols = 0;

    if (con_size(&rows, &cols) == 0 && rows != 0 && cols != 0) {
        con_rows = rows;
        con_cols = cols;
    }
    (void)con_rows;

    signal(SIGINT, on_sigint);
    reclaim_foreground();

    printf("MyOS Ring 3 Shell\n");
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
