#ifndef _SHELL_H
#define _SHELL_H

// 命令列表：
// time     获取时间
// panic    触发INT 3
// echo     打印后面的文字
// reboot   ACPI重启
// shutdown ACPI关机
// secho    串口打印文字
// tick     获取PIT中的tick
// help     显示帮助
// clear    清屏
// hello    打印hello消息

typedef struct {
    const char *name;
    int (*func)(int argc, char *argv[]);
    const char *desc;
} cmd_t;

int cmd_time(int argc, char *argv[]);
int cmd_panic(int argc, char *argv[]);
int cmd_echo(int argc, char *argv[]);
int cmd_reboot(int argc, char *argv[]);
int cmd_secho(int argc, char *argv[]);
int cmd_tick(int argc, char *argv[]);
int cmd_help(int argc, char *argv[]);
int cmd_clear(int argc, char *argv[]);
int cmd_hello(int argc, char *argv[]);
int cmd_shutdown(int argc, char *argv[]);

void start_shell(void);

#endif