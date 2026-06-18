#ifndef _SHELL_H
#define _SHELL_H

#include "stdint.h"

// 命令结构
struct command {
    const char* name;
    void (*handler)(int argc, char** argv);
    const char* help;
};

// Shell 主函数
void shell_init(void);
void shell_run(void);

#endif