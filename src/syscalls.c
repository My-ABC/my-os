#include "syscalls.h"
#include "stdio.h"
#include "process.h"

// 系统调用处理（由 isr80 调用）
void syscall_handler(void) {
    uint32_t num;
    
    // 获取系统调用号
    __asm__ volatile ("movl %%eax, %0" : "=r"(num));
    
    int ret = -1;
    
    switch (num) {
        case SYS_PRINT: {  // SYS_PRINT
            const char* str;
            __asm__ volatile ("movl %%ebx, %0" : "=r"(str));
            print_info(str);
            ret = 0;
            break;
        }
        case SYS_GETPID: {  // SYS_GETPID
            struct process* p = process_get_current();
            ret = p ? p->pid : -1;
            break;
        }
        default:
            ret = -1;
            break;
    }
    
    // 返回值放回 eax
    __asm__ volatile ("movl %0, %%eax" : : "r"(ret));
}

// 用户调用：打印字符串
int syscall_print(const char* str) {
    int ret;
    __asm__ volatile (
        "movl $1, %%eax\n"
        "movl %1, %%ebx\n"
        "int $0x80\n"
        "movl %%eax, %0\n"
        : "=r"(ret)
        : "r"(str)
        : "eax", "ebx"
    );
    return ret;
}

// 用户调用：获取 PID
int syscall_getpid(void) {
    int ret;
    __asm__ volatile (
        "movl $4, %%eax\n"
        "int $0x80\n"
        "movl %%eax, %0\n"
        : "=r"(ret)
        : : "eax"
    );
    return ret;
}