#include "shell/shell.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "keyboard.h"
#include "memory.h"
#include "idt.h"
#include "isr.h"
#include "trap.h"

// 命令声明
static void cmd_help(int argc, char** argv);
static void cmd_clear(int argc, char** argv);
static void cmd_echo(int argc, char** argv);
static void cmd_uptime(int argc, char** argv);
static void cmd_meminfo(int argc, char** argv);
static void cmd_reboot(int argc, char** argv);
static void cmd_blue(int argc, char** argv);
static void cmd_sleep(int argc, char** argv);
static void cmd_version(int argc, char** argv);
static void cmd_shutdown(int argc, char** argv);
static void cmd_rand(int argc, char** argv);

// 命令列表
static struct command commands[] = {
    {"help",     cmd_help,     "Show available commands"},
    {"clear",    cmd_clear,    "Clear the screen"},
    {"echo",     cmd_echo,     "Echo text"},
    {"uptime",   cmd_uptime,   "Show system uptime"},
    {"meminfo",  cmd_meminfo,  "Show memory information"},
    {"reboot",   cmd_reboot,   "Reboot the system"},
    {"blue",     cmd_blue,     "Trigger blue screen (INT3)"},
    {"sleep",    cmd_sleep,    "Try the sleep"},
    {"version",  cmd_version,  "The OS version"},
    {"shutdown", cmd_shutdown, "Shutdown the system"},
    {"sand",     cmd_rand,     "print a sand value"},
    {NULL, NULL, NULL}  // 终止符
};

// ========== 命令实现 ==========

static void cmd_help(int argc, char** argv) {
    print_info("Available commands:\n");
    for (int i = 0; commands[i].name != NULL; i++) {
        printf("  %-10s - %s\n", commands[i].name, commands[i].help);
    }
}

static void cmd_clear(int argc, char** argv) {
    vga_clear();
}

static void cmd_echo(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        print_info(argv[i]);
        if (i < argc - 1) print_info(" ");
    }
    print_info("\n");
}

static void cmd_uptime(int argc, char** argv) {
    uint32_t sec = get_seconds();
    uint32_t ticks = get_ticks();
    printf("Uptime: %d seconds (%d ticks)\n", sec, ticks);
}

static void cmd_meminfo(int argc, char** argv) {
    uint32_t total, free, used;
    pmm_get_stats(&total, &free, &used);
    printf("Physical memory:\n");
    printf("  Total pages: %d\n", total);
    printf("  Free pages:  %d\n", free);
    printf("  Used pages:  %d\n", used);
    printf("  Total RAM:   %d MB\n", (total * 4096) / (1024 * 1024));
}

static void cmd_reboot(int argc, char** argv) {
    print_info("Rebooting...\n");
    __asm__ volatile (
        "cli\n"
        "movl $0x0000FFF0, %%eax\n"
        "jmp *%%eax\n"
        : : : "eax"
    );
}

static void cmd_blue(int argc, char** argv) {
    print_info("Triggering blue screen...\n");
    int3_breakpoint();
}

static void cmd_sleep(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: sleep <seconds>\n");
        return;
    }
    int sec = atoi(argv[1]);
    if (sec <= 0) {
        printf("Invalid time\n");
        return;
    }
    sleep(sec * 1000);
}

static void cmd_version(int argc, char** argv) {
    printf("Version: my-os (v0.0.5)");
}
static void cmd_shutdown(int argc, char** argv) {
    print_info("Shutting down...\n");

    __asm__ volatile (
        "cli\n"
        "movw $0x2000, %%ax\n"
        "movw $0x0604, %%dx\n"
        "outw %%ax, %%dx\n"
        : : : "ax", "dx"
    );

    while(1) __asm__ volatile ("hlt");
}

static void cmd_rand(int argc, char** argv) {
    printf("%d\n", rand());
    return;
}

// ========== Shell 核心 ==========

// 分割命令行
static int parse_command(char* line, char** argv) {
    int argc = 0;
    char* p = line;
    while (*p) {
        // 跳过空格
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        // 跳到下一个空格或结束
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
    return argc;
}

static void shell_execute(char* line) {
    char* argv[16];
    int argc = parse_command(line, argv);
    
    if (argc == 0) return;
    
    // 查找命令
    for (int i = 0; commands[i].name != NULL; i++) {
        if (strcmp(argv[0], commands[i].name) == 0) {
            commands[i].handler(argc, argv);
            return;
        }
    }
    
    printf("Unknown command: %s\n", argv[0]);
    printf("Type 'help' for available commands.\n");
}

void shell_init(void) {
    print_info("Shell initialized\n");
}

void shell_run(void) {
    char buffer[128];
    
    while (1) {
        print_info("\nmyos> ");
        scan(buffer, sizeof(buffer));
        shell_execute(buffer);
    }
}