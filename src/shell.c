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

// 命令表
static cmd_t cmd_table[] = {
    {"time",     cmd_time,     "Show current time"},
    {"panic",    cmd_panic,    "Trigger blue screen"},
    {"echo",     cmd_echo,     "Print text"},
    {"reboot",   cmd_reboot,   "Reboot system"},
    {"secho",    cmd_secho,    "Print text to serial"},
    {"tick",     cmd_tick,     "Show PIT tick count"},
    {"help",     cmd_help,     "Show this help"},
    {"clear",    cmd_clear,    "Clear screen"},
    {"hello",    cmd_hello,    "Print hello message"},
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

int cmd_secho(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        serial_print(argv[i]);
        if (i < argc - 1) serial_putchar(' ');
    }
    serial_putchar('\n');
    return 0;
}

int cmd_tick(int argc, char *argv[]) {
    printf("Tick count: %u\n", pit_ticks());
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