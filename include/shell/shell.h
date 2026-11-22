#ifndef _SHELL_H
#define _SHELL_H

typedef struct {
    const char *name;
    int (*func)(int argc, char *argv[]);
    const char *desc;
} cmd_t;

int cmd_time(int argc, char *argv[]);
int cmd_echo(int argc, char *argv[]);
int cmd_reboot(int argc, char *argv[]);
int cmd_help(int argc, char *argv[]);
int cmd_clear(int argc, char *argv[]);
int cmd_hello(int argc, char *argv[]);
int cmd_elf(int argc, char *argv[]);
int cmd_run(int argc, char *argv[]);
int cmd_disk(int argc, char *argv[]);
int cmd_ls(int argc, char *argv[]);
int cmd_cat(int argc, char *argv[]);
int cmd_write(int argc, char *argv[]);
int cmd_cp(int argc, char *argv[]);
int cmd_mv(int argc, char *argv[]);
int cmd_rm(int argc, char *argv[]);
int cmd_cd(int argc, char *argv[]);
int cmd_mkdir(int argc, char *argv[]);
int cmd_rmdir(int argc, char *argv[]);
int cmd_shutdown(int argc, char *argv[]);
int cmd_ring3(int argc, char *argv[]);
int cmd_ps(int argc, char *argv[]);
int cmd_fg(int argc, char *argv[]);
int cmd_bg(int argc, char *argv[]);
int cmd_kill(int argc, char *argv[]);

void start_shell(void);

#endif