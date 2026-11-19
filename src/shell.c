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
#include "elf.h"
#include "vbe.h"

extern const uint8_t elf_demo_start[];
extern const uint8_t elf_demo_end[];

// 命令表
static cmd_t cmd_table[] = {
    {"time",     cmd_time,     "Show current time"},
    {"panic",    cmd_panic,    "Trigger blue screen"},
    {"svga_blue", cmd_svga_blue, "Show a blue screen"},
    {"echo",     cmd_echo,     "Print text"},
    {"reboot",   cmd_reboot,   "Reboot system"},
    {"shutdown", cmd_shutdown, "Shutdown system"},
    {"secho",    cmd_secho,    "Print text to serial"},
    {"tick",     cmd_tick,     "Show PIT tick count"},
    {"help",     cmd_help,     "Show this help"},
    {"clear",    cmd_clear,    "Clear screen"},
    {"hello",    cmd_hello,    "Print hello message"},
    {"ring3",    cmd_ring3,    "Start one Ring 3 process in background"},
    {"ps",       cmd_ps,       "List Ring 3 processes"},
    {"fg",       cmd_fg,       "Show a Ring 3 process in foreground"},
    {"bg",       cmd_bg,       "Hide a Ring 3 process output"},
    {"kill",     cmd_kill,     "Terminate a process by PID"},
    {"elf",      cmd_elf,      "Load the built-in ELF32 program"},
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
static int split_line(char *line, char *argv[], int max_args) {
    int argc = 0;
    char *p = line;

    while (*p && argc < max_args - 1) {
        while (*p == ' ') p++;
        if (*p == '\0') break;
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

// 命令实现
int cmd_time(int argc, char *argv[]) {
    rtc_time_t now;
    rtc_read_time(&now);
    rtc_to_beijing_time(&now);
    printf("%04d-%02d-%02d %02d:%02d:%02d\n",
           now.year, now.month, now.day,
           now.hour, now.minute, now.second);
    return 0;
}

int cmd_panic(int argc, char *argv[]) {
    printf("Triggering panic...\n");
    __asm__ volatile ("int $0x03");
    return 0;
}

int cmd_svga_blue(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    if (vbe_blue_screen() != 0) {
        vga_set_color(VGA_COLOR_BLUE, VGA_COLOR_BLUE);
        vga_clear();
    }
    return 0;
}

int cmd_echo(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        printf("%s", argv[i]);
        if (i < argc - 1) printf(" ");
    }
    printf("\n");
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

int cmd_secho(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        serial_print(argv[i]);
        if (i < argc - 1) serial_putchar(' ');
    }
    serial_putchar('\n');
    return 0;
}

int cmd_tick(int argc, char *argv[]) {
    printf("Tick count: %d\n", pit_ticks());
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
    paging_map_page((uint32_t)user_entry, (uint32_t)user_entry,
                    PAGE_PRESENT | PAGE_USER);
    if (paging_get_page_physical((uint32_t)user_entry) == 0U) {
        printf("Unable to map Ring 3 code\n");
        return -1;
    }
    serial_print("[RING3] Scheduling user function at ");
    serial_print_hex((uint32_t)user_entry);
    serial_print("\n");
    if (ring3_schedule(user_entry) != 0) {
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

int cmd_elf(int argc, char *argv[]) {
    uint32_t size = (uint32_t)(elf_demo_end - elf_demo_start);
    int result;
    (void)argc;
    (void)argv;

    result = elf_run(elf_demo_start, size);
    if (result != 0) {
        printf("ELF32 load failed\n");
        return result;
    }
    return 0;
}

void start_shell(void) {
    char line[128];
    char *argv[16];
    int argc;

    vga_clear();
    printf("MyOS Shell\n");
    printf("Type 'help' for available commands.\n");

    while (1) {
        printf("myos$ ");
        gets(line, sizeof(line));
        argc = split_line(line, argv, 16);
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