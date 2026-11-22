#include "shell.h"
#include "stdio.h"
#include "string.h"
#include "rtc.h"
#include "acpi.h"
#include "pit.h"
#include "vga.h"
#include "keyboard.h"
#include "panic.h"
#include "serial.h"
#include "paging.h"
#include "ring3.h"
#include "signal.h"
#include "elf.h"
#include "vbe.h"
#include "ata.h"
#include "fat32.h"
#include "heap.h"

extern const uint8_t elf_demo_start[];
extern const uint8_t elf_demo_end[];

// 命令表
static cmd_t cmd_table[] = {
    {"time",     cmd_time,     "Show current time"},
    {"echo",     cmd_echo,     "Print text"},
    {"reboot",   cmd_reboot,   "Reboot system"},
    {"shutdown", cmd_shutdown, "Shutdown system"},
    {"help",     cmd_help,     "Show this help"},
    {"clear",    cmd_clear,    "Clear screen"},
    {"hello",    cmd_hello,    "Print hello message"},
    {"ring3",    cmd_ring3,    "Start one Ring 3 process in background"}, // 目前这个，需要实现fork syscall才能一只
    {"ps",       cmd_ps,       "List Ring 3 processes"},
    {"fg",       cmd_fg,       "Show a Ring 3 process in foreground"},
    {"bg",       cmd_bg,       "Hide a Ring 3 process output"},
    {"kill",     cmd_kill,     "Terminate a process by PID"},
    {"elf",      cmd_elf,      "Load the built-in ELF32 program"}, // 同
    {"disk",     cmd_disk,     "Initialize ATA/FAT32 disk"},
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
    {NULL, NULL, NULL}
};

// 拆分命令参数 (简单版本，不修改原字符串)
static char **split_args(const char *str, char *argv[], int max_args) {
    int argc = 0;
    const char *p = str;
    char *arg = (char *)p;  // 我们不会修改原始字符串，只读取

    while (*p && argc < max_args - 1) {
        // 跳过空格
        while (*p == ' ') p++;
        if (*p == '\0') break;
        argv[argc++] = (char *)p;
        // 跳过非空格字符
        while (*p && *p != ' ') p++;
        if (*p) {
            // 注意：我们不修改字符串，只是让指针指向下一个字符
            // 所以这里不做 '\0' 替换，而是记住位置
        }
    }
    argv[argc] = NULL;
    return (char **)argc;  // 返回 argc 通过参数传递，这里返回指针是为了方便
}

// 实际拆分函数，返回 argc
// 以双引号开头的参数会保留内部空格直到配对的闭引号（引号不计入参数）；
// 未闭合的引号则取到行尾。
static int split_line(char *line, char *argv[], int max_args) {
    int argc = 0;
    char *p = line;

    while (*p && argc < max_args - 1) {
        while (*p == ' ') p++;
        if (*p == '\0') break;
        if (*p == '"') {
            /* 找闭引号时跳过 \" 转义, 它不结束引号 */
            char *close = p + 1;
            while (*close != '\0') {
                if (*close == '\\' && close[1] != '\0') {
                    close += 2;
                    continue;
                }
                if (*close == '"') break;
                ++close;
            }
            /* 引号分组要求非空内容; "" 和未闭合的 " 都按字面量处理 */
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

// 展开参数中的转义序列（原地压缩）: \n -> 换行, \\ -> 反斜杠,
// \" -> 双引号; 其余 \x 保持原样并打印警告, 行尾单独的 \ 也按字面量处理。
static void shell_unescape_arg(char *arg) {
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
                serial_print("Warning: unknown escape '\\");
                serial_putchar(r[1]);
                serial_print("' kept literal\n");
            }
            *w++ = *r++;
        }
    }
    *w = '\0';
}

// 命令实现
int cmd_time(int argc, char *argv[]) {
    int use_12h = 0;
    if (argc > 1) {
        if (strcmp(argv[1], "12") == 0) {
            use_12h = 1;
        } else if (strcmp(argv[1], "24") != 0) {
            printf("Usage: time [12|24]\n");
            return -1;
        }
    }
    rtc_time_t now;
    rtc_read_time(&now);
    rtc_to_beijing_time(&now);
    uint32_t hour = now.hour;
    const char *ampm = "";
    if (use_12h) {
        ampm = hour < 12 ? " AM" : " PM";
        hour = hour % 12;
        if (hour == 0) hour = 12;
    }
    printf("%04d-%02d-%02d %02d:%02d:%02d%s\n",
           now.year, now.month, now.day,
           hour, now.minute, now.second, ampm);
    return 0;
}

int cmd_echo(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        printf("%s", argv[i]);
        serial_print(argv[i]);
        if (i < argc - 1) {
            printf(" ");
            serial_putchar(' ');
        }
    }
    printf("\n");
    serial_putchar('\n');
    return 0;
}

int cmd_reboot(int argc, char *argv[]) {
    printf("Rebooting...\n");
    acpi_reboot();
    return 0;
}

int cmd_shutdown(int argc, char *argv[]) {
    printf("Shutting down...\n");
    acpi_shutdown();
    return 0;
}

int cmd_help(int argc, char *argv[]) {
    printf("Available commands:\n");
    for (int i = 0; cmd_table[i].name != NULL; i++) {
        printf("  %s", cmd_table[i].name);
        // 手动补齐到 10 个字符
        int len = strlen(cmd_table[i].name);
        for (int j = len; j < 10; j++) {
            printf(" ");
        }
        printf(" %s\n", cmd_table[i].desc);
    }
    return 0;
}

int cmd_clear(int argc, char *argv[]) {
    vga_clear();
    return 0;
}

int cmd_hello(int argc, char *argv[]) {
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

int cmd_ring3(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    struct user_mm *mm = paging_mm_create();
    if (mm == 0) {
        printf("Unable to start a Ring 3 process (out of memory)\n");
        return -1;
    }
    paging_mm_map(mm, (uint32_t)user_entry, (uint32_t)user_entry,
                  PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER | PAGE_BORROWED);
    if (paging_mm_get_phys(mm, (uint32_t)user_entry) == 0U) {
        paging_mm_release(mm);
        printf("Unable to map Ring 3 code\n");
        return -1;
    }
    serial_print("[RING3] Scheduling user function at ");
    serial_print_hex((uint32_t)user_entry);
    serial_print("\n");
    if (ring3_schedule_mm(user_entry, mm, 0U, 0U, 0) != 0) {
        paging_mm_release(mm);
        printf("Unable to start a Ring 3 process (slots full or out of memory)\n");
        return -1;
    }
    printf("Started a Ring 3 process in background; use ps then fg <pid>\n");
    serial_print("[RING3] Started one background process\n");
    return 0;
}

int cmd_ps(int argc, char *argv[]) {
    uint32_t pids[RING3_PROCESS_COUNT];
    uint32_t states[RING3_PROCESS_COUNT];
    (void)argc;
    (void)argv;
    ring3_process_list(pids, states, RING3_PROCESS_COUNT);
    printf("PID  STATE\n");
    serial_print("[PS] PID STATE\n");
    for (uint32_t i = 0; i < RING3_PROCESS_COUNT; ++i) {
        if (pids[i] == 0U) continue;
        const char *state = "UNUSED";
        if (states[i] == RING3_PROCESS_READY) state = "READY";
        else if (states[i] == RING3_PROCESS_RUNNING) state = "RUNNING";
        else if (states[i] == RING3_PROCESS_DEAD) state = "DEAD";
        printf("%d    %s%s\n", pids[i], state,
               ring3_foreground_pid() == pids[i] ? " (FOREGROUND)" : "");
        serial_print("[PS] ");
        serial_print_dec(pids[i]);
        serial_putchar(' ');
        serial_print(state);
        if (ring3_foreground_pid() == pids[i]) {
            serial_print(" (FOREGROUND)");
        }
        serial_putchar('\n');
    }
    return 0;
}

int cmd_fg(int argc, char *argv[]) {
    uint32_t pid = 0U;
    if (argc != 2 || argv[1][0] == '\0') {
        printf("Usage: fg <pid>\n");
        return -1;
    }
    for (const char *p = argv[1]; *p; ++p) {
        if (*p < '0' || *p > '9' || pid > (0xFFFFFFFFU - (*p - '0')) / 10U) {
            printf("Invalid PID\n");
            return -1;
        }
        pid = pid * 10U + (*p - '0');
    }
    if (ring3_set_foreground(pid) != 0) {
        printf("No running process with PID %d\n", pid);
        return -1;
    }
    printf("Process %d output is now visible\n", pid);
    return 0;
}

int cmd_bg(int argc, char *argv[]) {
    uint32_t pid = 0U;
    if (argc != 2 || argv[1][0] == '\0') {
        printf("Usage: bg <pid>\n");
        return -1;
    }
    for (const char *p = argv[1]; *p; ++p) {
        if (*p < '0' || *p > '9' || pid > (0xFFFFFFFFU - (*p - '0')) / 10U) {
            printf("Invalid PID\n");
            return -1;
        }
        pid = pid * 10U + (*p - '0');
    }
    if (ring3_set_background(pid) != 0) {
        printf("No running process with PID %d\n", pid);
        return -1;
    }
    printf("Process %d output is hidden\n", pid);
    return 0;
}

int cmd_kill(int argc, char *argv[]) {
    uint32_t pid = 0U;
    if (argc != 2 || argv[1][0] == '\0') {
        printf("Usage: kill <pid>\n");
        return -1;
    }
    for (const char *p = argv[1]; *p; ++p) {
        if (*p < '0' || *p > '9' || pid > (0xFFFFFFFFU - (*p - '0')) / 10U) {
            printf("Invalid PID\n");
            return -1;
        }
        pid = pid * 10U + (*p - '0');
    }
    if (ring3_kill(pid) != 0) {
        printf("No running process with PID %d\n", pid);
        return -1;
    }
    printf("Killed process %d\n", pid);
    serial_print("[RING3] Killed PID ");
    serial_print_dec(pid);
    serial_print("\n");
    return 0;
}

/* Map elf_run/elf32_load result codes to distinct messages. */
static void shell_print_elf_error(int result) {
    const char *msg;
    switch (result) {
        case -1: msg = "not an ELF32 image"; break;
        case -2: msg = "unsupported ELF class, type, or machine"; break;
        case -3: msg = "malformed ELF headers"; break;
        case -4: msg = "invalid program segment"; break;
        case -5: msg = "no valid entry point"; break;
        case -6: msg = "image address range already in use"; break;
        case -7: msg = "out of memory while loading"; break;
        case -8: msg = "cannot start process (slots full or out of memory)"; break;
        default: msg = "unknown error"; break;
    }
    printf("ELF32 load failed: %s\n", msg);
}

int cmd_elf(int argc, char *argv[]) {
    uint32_t size = (uint32_t)(elf_demo_end - elf_demo_start);
    int result;
    (void)argc;
    (void)argv;

    result = elf_run(elf_demo_start, size);
    if (result != 0) {
        shell_print_elf_error(result);
        return result;
    }
    return 0;
}

int cmd_disk(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    if (!ata_init()) {
        printf("ATA primary master not found\n");
        return -1;
    }
    if (fat32_init() != 0) {
        printf("FAT32 filesystem not found\n");
        return -1;
    }
    printf("ATA primary master and FAT32 filesystem initialized\n");
    printf("Disk sectors: %d\n", ata_sector_count());
    serial_print("[DISK] ATA primary master and FAT32 filesystem initialized\n");
    serial_print("[DISK] Disk sectors: ");
    serial_print_dec(ata_sector_count());
    serial_putchar('\n');
    return 0;
}

static void fat32_print_entry(const char *name, uint32_t attr, uint32_t size) {
    if ((attr & 0x10U) != 0U) {
        printf("%s/   <DIR>\n", name);
        serial_print("[LS] ENTRY ");
        serial_print(name);
        serial_print("/ <DIR>\n");
    } else {
        printf("%s   %d bytes\n", name, size);
        serial_print("[LS] ENTRY ");
        serial_print(name);
        serial_print(" ");
        serial_print_dec(size);
        serial_print(" bytes\n");
    }
}

int cmd_ls(int argc, char *argv[]) {
    char path[128];
    char cwd[128];
    const char *target = argv[1];
    if (argc <= 1) {
        if (fat32_get_cwd(cwd, sizeof(cwd)) < 0) return -1;
        target = cwd;
    }
    if (fat32_resolve_path(target, path, sizeof(path)) != 0) {
        printf("Path is too long\n");
        return -1;
    }
    serial_print("[LS] Listing ");
    serial_print(path);
    serial_putchar('\n');
    if (fat32_list_directory(path, fat32_print_entry) != 0) {
        printf("Unable to list %s\n", path);
        serial_print("[LS] Unable to list ");
        serial_print(path);
        serial_putchar('\n');
        return -1;
    }
    return 0;
}

int cmd_cat(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: cat <file>\n");
        return -1;
    }
    char path[128];
    if (fat32_resolve_path(argv[1], path, sizeof(path)) != 0) {
        printf("Path is too long\n");
        return -1;
    }
    serial_print("[FAT32] Reading ");
    serial_print(path);
    serial_putchar('\n');
    static uint8_t buffer[65536];
    uint32_t size = 0U;
    if (fat32_read_file(path, buffer, sizeof(buffer), &size) != 0) {
        printf("Unable to read %s\n", path);
        serial_print("[FAT32] Unable to read ");
        serial_print(path);
        serial_putchar('\n');
        return -1;
    }
    for (uint32_t i = 0; i < size; ++i) {
        printf("%c", buffer[i]);
        serial_putchar((char)buffer[i]);
    }
    printf("\n");
    serial_putchar('\n');
    return 0;
}

int cmd_write(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: write <file> <text>\n");
        return -1;
    }
    char path[128];
    if (fat32_resolve_path(argv[1], path, sizeof(path)) != 0) {
        printf("Path is too long\n");
        return -1;
    }
    char text[1024];
    uint32_t offset = 0U;
    for (int i = 2; i < argc; ++i) {
        if (i > 2 && offset + 1U < sizeof(text)) text[offset++] = ' ';
        uint32_t len = strlen(argv[i]);
        if (len > sizeof(text) - 1U - offset) len = sizeof(text) - 1U - offset;
        memcpy(text + offset, argv[i], len);
        offset += len;
    }
    text[offset] = '\0';
    if (fat32_write_file(path, text, offset) != 0) {
        printf("Unable to write %s\n", path);
        return -1;
    }
    printf("Wrote %d bytes to %s\n", offset, path);
    serial_print("[FAT32] Wrote ");
    serial_print_dec(offset);
    serial_print(" bytes to ");
    serial_print(path);
    serial_putchar('\n');
    return 0;
}

static int cp_join_path(const char *base, const char *name,
                        char *out, uint32_t size) {
    uint32_t base_length = strlen(base);
    uint32_t name_length = strlen(name);
    uint32_t separator = base_length > 1U ? 1U : 0U;
    if (base_length + separator + name_length + 1U > size) return -1;
    memcpy(out, base, base_length);
    if (separator != 0U) out[base_length++] = '/';
    memcpy(out + base_length, name, name_length + 1U);
    return 0;
}

struct cp_dirent {
    char name[256];
};

/* fat32_list_directory iterates a shared static cluster buffer, so the
 * callback must not perform file I/O. Collect entries first, then copy. */
static uint32_t cp_count_entries;
static struct cp_dirent *cp_fill_entries;
static uint32_t cp_fill_remaining;

static void cp_count_callback(const char *name, uint32_t attr, uint32_t size) {
    (void)name;
    (void)attr;
    (void)size;
    ++cp_count_entries;
}

static void cp_fill_callback(const char *name, uint32_t attr, uint32_t size) {
    (void)attr;
    (void)size;
    if (cp_fill_remaining == 0U) return;
    --cp_fill_remaining;
    strncpy(cp_fill_entries->name, name,
            sizeof(cp_fill_entries->name) - 1U);
    cp_fill_entries->name[sizeof(cp_fill_entries->name) - 1U] = '\0';
    ++cp_fill_entries;
}

static int cp_recursive_copy(const char *src, const char *dst, uint32_t depth) {
    if (depth > 32U || src == 0 || dst == 0) return -1;
    int is_dir = fat32_is_directory(src);
    if (is_dir < 0) return -1;
    if (is_dir == 0) return fat32_copy_file(src, dst);
    if (fat32_make_directory(dst) != 0) return -1;

    cp_count_entries = 0U;
    if (fat32_list_directory(src, cp_count_callback) != 0) return -1;
    uint32_t total = cp_count_entries;
    if (total == 0U) return 0;
    struct cp_dirent *entries =
        (struct cp_dirent *)kmalloc(total * sizeof(*entries));
    if (entries == 0) return -1;

    cp_fill_entries = entries;
    cp_fill_remaining = total;
    int result = fat32_list_directory(src, cp_fill_callback);
    uint32_t filled = total - cp_fill_remaining;
    cp_fill_entries = 0;
    if (result != 0 || filled != total) {
        kfree(entries);
        return -1;
    }

    for (uint32_t i = 0U; i < total; ++i) {
        char child_src[256], child_dst[256];
        if (cp_join_path(src, entries[i].name, child_src,
                         sizeof(child_src)) != 0 ||
            cp_join_path(dst, entries[i].name, child_dst,
                         sizeof(child_dst)) != 0 ||
            cp_recursive_copy(child_src, child_dst, depth + 1U) != 0) {
            result = -1;
            break;
        }
    }
    kfree(entries);
    return result;
}

static const char *disk_path_basename(const char *path) {
    const char *base = path;
    for (const char *p = path; *p != '\0'; ++p) {
        if (*p == '/') base = p + 1;
    }
    return base;
}

/* When dst names an existing directory, the source goes inside it:
 * `cp file dir` -> dir/file, `cp -r dir1 dir2` -> dir2/dir1, same for mv. */
static int disk_resolve_destination(const char *src, const char *dst,
                                    char *out, uint32_t size) {
    if (fat32_is_directory(dst) != 1) {
        uint32_t length = strlen(dst);
        if (length + 1U > size) return -1;
        memcpy(out, dst, length + 1U);
        return 0;
    }
    const char *base = disk_path_basename(src);
    uint32_t dst_length = strlen(dst);
    uint32_t base_length = strlen(base);
    uint32_t separator = dst_length > 1U ? 1U : 0U;
    if (dst_length + separator + base_length + 1U > size) return -1;
    memcpy(out, dst, dst_length);
    uint32_t position = dst_length;
    if (separator != 0U) out[position++] = '/';
    memcpy(out + position, base, base_length + 1U);
    return 0;
}

int cmd_cp(int argc, char *argv[]) {
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
    char src[128], dst[128], dst_final[256];
    if (fat32_resolve_path(argv[first], src, sizeof(src)) != 0 ||
        fat32_resolve_path(argv[first + 1], dst, sizeof(dst)) != 0 ||
        disk_resolve_destination(src, dst, dst_final,
                                 sizeof(dst_final)) != 0) {
        printf("Path is too long\n");
        return -1;
    }
    if (recursive && fat32_path_within(src, dst_final) == 1) {
        printf("Cannot copy %s into itself\n", src);
        serial_print("[CP] Refused to copy ");
        serial_print(src);
        serial_print(" into itself\n");
        return -1;
    }
    int result = recursive ? cp_recursive_copy(src, dst_final, 0U)
                           : fat32_copy_file(src, dst_final);
    if (result != 0) {
        printf("Unable to copy %s to %s\n", src, dst_final);
        serial_print("[CP] Failed to copy ");
        serial_print(src);
        serial_print(" to ");
        serial_print(dst_final);
        serial_putchar('\n');
        return -1;
    }
    printf("Copied %s to %s\n", src, dst_final);
    serial_print("[CP] Copied ");
    serial_print(src);
    serial_print(" to ");
    serial_print(dst_final);
    serial_putchar('\n');
    return 0;
}

int cmd_mv(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: mv <src> <dst>\n");
        return -1;
    }
    char src[128], dst[128], dst_final[256];
    if (fat32_resolve_path(argv[1], src, sizeof(src)) != 0 ||
        fat32_resolve_path(argv[2], dst, sizeof(dst)) != 0 ||
        disk_resolve_destination(src, dst, dst_final,
                                 sizeof(dst_final)) != 0) {
        printf("Path is too long\n");
        return -1;
    }
    if (fat32_move(src, dst_final) != 0) {
        printf("Unable to move %s to %s\n", src, dst_final);
        serial_print("[MV] Failed to move ");
        serial_print(src);
        serial_print(" to ");
        serial_print(dst_final);
        serial_putchar('\n');
        return -1;
    }
    printf("Moved %s to %s\n", src, dst_final);
    serial_print("[MV] Moved ");
    serial_print(src);
    serial_print(" to ");
    serial_print(dst_final);
    serial_putchar('\n');
    return 0;
}

int cmd_rm(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: rm <file>\n");
        return -1;
    }
    char path[128];
    if (fat32_resolve_path(argv[1], path, sizeof(path)) != 0) {
        printf("Path is too long\n");
        return -1;
    }
    if (fat32_delete_file(path) != 0) {
        printf("Unable to delete %s\n", path);
        return -1;
    }
    printf("Deleted %s\n", path);
    return 0;
}

int cmd_cd(int argc, char *argv[]) {
    if (argc > 2) {
        printf("Usage: cd [directory]\n");
        return -1;
    }
    char path[128];
    if (fat32_resolve_path(argc == 2 ? argv[1] : "/", path, sizeof(path)) != 0) {
        printf("Path is too long\n");
        return -1;
    }
    if (!fat32_mounted()) {
        printf("No FAT32 filesystem mounted\n");
        return -1;
    }
    /* FAT32 listing validates that the directory exists. */
    if (fat32_list_directory(path, 0) != 0) {
        printf("Directory not found: %s\n", path);
        serial_print("[SHELL] Directory not found: ");
        serial_print(path);
        serial_putchar('\n');
        return -1;
    }
    if (fat32_set_cwd(path) != 0) return -1;
    char cwd[128];
    fat32_get_cwd(cwd, sizeof(cwd));
    printf("Current directory: %s\n", cwd);
    serial_print("[SHELL] CWD ");
    serial_print(cwd);
    serial_putchar('\n');
    return 0;
}

int cmd_mkdir(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: mkdir <directory>\n");
        return -1;
    }
    char path[128];
    if (fat32_resolve_path(argv[1], path, sizeof(path)) != 0) {
        printf("Path is too long\n");
        return -1;
    }
    if (fat32_make_directory(path) != 0) {
        printf("Unable to create %s\n", path);
        return -1;
    }
    printf("Created %s\n", path);
    return 0;
}

static int disk_path_contains_current(const char *path) {
    char cwd[128];
    if (fat32_get_cwd(cwd, sizeof(cwd)) < 0) return 0;
    uint32_t path_length = strlen(path);
    uint32_t cwd_length = strlen(cwd);
    if (path_length == 1U && path[0] == '/') return 1;
    if (path_length > cwd_length ||
        memcmp(path, cwd, path_length) != 0) {
        return 0;
    }
    return path_length == cwd_length || cwd[path_length] == '/';
}

int cmd_rmdir(int argc, char *argv[]) {
    int force = 0;
    const char *target;
    if (argc == 2) {
        target = argv[1];
    } else if (argc == 3 && strcmp(argv[1], "-f") == 0) {
        force = 1;
        target = argv[2];
    } else {
        printf("Usage: rmdir [-f] <directory>\n");
        return -1;
    }

    char path[128];
    if (fat32_resolve_path(target, path, sizeof(path)) != 0) {
        printf("Path is too long\n");
        return -1;
    }
    if (disk_path_contains_current(path)) {
        printf("Cannot remove the current directory or one of its parents\n");
        return -1;
    }

    int result = force ? fat32_remove_directory_recursive(path)
                       : fat32_remove_directory(path);
    if (result != 0) {
        printf("Unable to remove %s%s\n", path,
               force ? "" : " (use -f to remove a non-empty directory)");
        serial_print("[RMDIR] Failed to remove ");
        serial_print(path);
        serial_putchar('\n');
        return -1;
    }
    printf("Removed %s%s\n", path, force ? " recursively" : "");
    serial_print("[RMDIR] Removed ");
    if (force) serial_print("recursively ");
    serial_print(path);
    serial_putchar('\n');
    return 0;
}

int cmd_run(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: run <elf32-file>\n");
        return -1;
    }
    char path[128];
    if (fat32_resolve_path(argv[1], path, sizeof(path)) != 0) {
        printf("Path is too long\n");
        return -1;
    }
    uint8_t *image = (uint8_t *)kmalloc(FAT32_MAX_FILE_SIZE);
    if (image == 0) {
        printf("Out of memory\n");
        return -1;
    }
    uint32_t size = 0U;
    int result = fat32_read_file(path, image, FAT32_MAX_FILE_SIZE, &size);
    if (result != 0) {
        printf("Failed to read file %s\n", path);
    } else {
        result = elf_run(image, size);
        if (result != 0) {
            shell_print_elf_error(result);
        }
    }
    kfree(image);
    return result;
}

static void shell_print_prompt(void) {
    char cwd[128];
    if (fat32_mounted() && fat32_get_cwd(cwd, sizeof(cwd)) >= 0) {
        printf("myos %s > ", cwd);
    } else {
        printf("myos > ");
    }
}

#define SHELL_HISTORY_MAX 16

static char shell_history[SHELL_HISTORY_MAX][128];
static int shell_history_count = 0;
static char shell_history_draft[128];

static void shell_history_push(const char *line) {
    uint32_t length = strlen(line);
    int blank = 1;
    for (uint32_t i = 0; i < length; ++i) {
        if (line[i] != ' ') {
            blank = 0;
            break;
        }
    }
    if (blank) return;
    if (shell_history_count > 0 &&
        strcmp(shell_history[shell_history_count - 1], line) == 0) {
        return;
    }
    if (shell_history_count == SHELL_HISTORY_MAX) {
        memmove(&shell_history[0], &shell_history[1],
                (SHELL_HISTORY_MAX - 1) * sizeof(shell_history[0]));
        --shell_history_count;
    }
    strncpy(shell_history[shell_history_count], line,
            sizeof(shell_history[0]) - 1U);
    shell_history[shell_history_count][sizeof(shell_history[0]) - 1U] = '\0';
    ++shell_history_count;
}

/* Absolute cursor cell for `offset` characters after the prompt origin,
 * wrapping at the console width. */
static void shell_cursor_to(int offset, uint16_t origin_row,
                            uint16_t origin_col, uint16_t *row, uint16_t *col) {
    uint32_t columns = vga_columns();
    uint32_t linear = (uint32_t)origin_col + (uint32_t)offset;
    *row = (uint16_t)(origin_row + linear / columns);
    *col = (uint16_t)(linear % columns);
}

/* Repaint the input line: erase `old_length` cells after the prompt,
 * print `line`/`length`, then leave the cursor at insertion offset
 * `cursor`. `origin` is captured right after the prompt prints. */
static void shell_paint_line(const char *line, int length, int cursor,
                             int old_length, uint16_t origin_row,
                             uint16_t origin_col) {
    uint16_t row, col;
    vga_set_cursor(origin_row, origin_col);
    for (int i = 0; i < old_length; ++i) {
        putchar(' ');
    }
    vga_set_cursor(origin_row, origin_col);
    for (int i = 0; i < length; ++i) {
        putchar(line[i]);
    }
    shell_cursor_to(cursor, origin_row, origin_col, &row, &col);
    vga_set_cursor(row, col);
}

static void shell_read_line(char *line, int size) {
    int length = 0;
    int cursor = 0;  /* insertion point within line[] */
    int prompt_visible = 0;
    int history_index = -1;  /* -1: editing a fresh line, draft holds it */
    uint16_t origin_row = 0;
    uint16_t origin_col = 0;
    int origin_valid = 0;

    for (;;) {
        if (ring3_foreground_pid() == 0U) {
            if (!prompt_visible && length == 0) {
                shell_print_prompt();
                prompt_visible = 1;
                vga_get_cursor(&origin_row, &origin_col);
                origin_valid = 1;
            }
        } else {
            prompt_visible = 0;
        }

        if (!keyboard_hit()) {
            __asm__ volatile ("sti; hlt" : : : "memory");
            continue;
        }

        char c = keyboard_getchar();
        if (c == KEYBOARD_KEY_SIGINT) {
            printf("^C\n");
            serial_print("^C\n");
            uint32_t fg = ring3_foreground_pid();
            if (fg != 0U) {
                ring3_signal_send(fg, SIGINT);
                serial_print("[SHELL] Sent SIGINT to PID ");
                serial_print_dec(fg);
                serial_putchar('\n');
            }
            /* Discard the line being edited; the loop redraws the
             * prompt once no foreground process remains. */
            length = 0;
            cursor = 0;
            history_index = -1;
            line[0] = '\0';
            prompt_visible = 0;
            origin_valid = 0;
            continue;
        }
        if (!origin_valid) {
            /* No prompt origin (foreground Ring 3 task): keep the old
             * append-only echo behavior and ignore editing keys. */
            if (c == '\n' || c == '\r') {
                line[length] = '\0';
                putchar('\n');
                return;
            }
            if (c == '\b') {
                if (length > 0) {
                    --length;
                    cursor = length;
                    putchar('\b');
                    putchar(' ');
                    putchar('\b');
                }
                continue;
            }
            if ((c < KEYBOARD_KEY_UP || c > KEYBOARD_KEY_RIGHT) &&
                length < size - 1) {
                line[length++] = c;
                cursor = length;
                putchar(c);
            }
            continue;
        }
        if (c == KEYBOARD_KEY_LEFT || c == KEYBOARD_KEY_RIGHT) {
            if (c == KEYBOARD_KEY_LEFT && cursor > 0) {
                --cursor;
            } else if (c == KEYBOARD_KEY_RIGHT && cursor < length) {
                ++cursor;
            } else {
                continue;
            }
            uint16_t row, col;
            shell_cursor_to(cursor, origin_row, origin_col, &row, &col);
            vga_set_cursor(row, col);
            continue;
        }
        if (c == KEYBOARD_KEY_UP || c == KEYBOARD_KEY_DOWN) {
            const char *text;
            int text_length;
            int new_index = history_index;

            if (c == KEYBOARD_KEY_UP) {
                if (shell_history_count == 0) continue;
                if (history_index == -1) {
                    int copy = length < (int)sizeof(shell_history_draft) - 1
                               ? length : (int)sizeof(shell_history_draft) - 1;
                    memcpy(shell_history_draft, line, copy);
                    shell_history_draft[copy] = '\0';
                    new_index = shell_history_count - 1;
                } else if (history_index > 0) {
                    --new_index;
                } else {
                    continue;
                }
                text = shell_history[new_index];
            } else {
                if (history_index == -1) continue;
                if (history_index < shell_history_count - 1) {
                    ++new_index;
                    text = shell_history[new_index];
                } else {
                    new_index = -1;
                    text = shell_history_draft;
                }
            }
            history_index = new_index;
            text_length = (int)strlen(text);
            int old_displayed = length;
            int copy = text_length < size - 1 ? text_length : size - 1;
            memcpy(line, text, copy);
            line[copy] = '\0';
            length = copy;
            cursor = length;
            shell_paint_line(line, length, cursor, old_displayed,
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
                        (uint32_t)(length - cursor));
                --length;
                --cursor;
                line[length] = '\0';
                shell_paint_line(line, length, cursor, length + 1,
                                 origin_row, origin_col);
            }
            continue;
        }
        if (length < size - 1) {
            memmove(line + cursor + 1, line + cursor,
                    (uint32_t)(length - cursor));
            line[cursor] = c;
            ++length;
            ++cursor;
            line[length] = '\0';
            shell_paint_line(line, length, cursor, length - 1,
                             origin_row, origin_col);
        }
    }
}

void start_shell(void) {
    char line[128];
    char *argv[16];
    int argc;

    vga_clear();
    printf("MyOS Shell\n");
    printf("Type 'help' for available commands.\n");

    while (1) {
        shell_read_line(line, sizeof(line));
        shell_history_push(line);
        argc = split_line(line, argv, 16);
        for (int i = 0; i < argc; ++i) {
            shell_unescape_arg(argv[i]);
        }
        if (argc == 0) continue;
        int found = 0;
        for (int i = 0; cmd_table[i].name != NULL; i++) {
            if (strcmp(argv[0], cmd_table[i].name) == 0) {
                cmd_table[i].func(argc, argv);
                found = 1;
                break;
            }
        }
        if (!found) {
            printf("Unknown command: %s\n", argv[0]);
        }
    }
}