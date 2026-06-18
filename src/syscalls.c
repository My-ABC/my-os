#include "syscalls.h"
#include "stdio.h"
#include "stdlib.h"
#include "keyboard.h"
#include "string.h"
#include "isr.h"

// 系统调用处理函数
void syscall_handler(void) {
    // 从寄存器获取参数
    uint32_t syscall_num;
    uint32_t arg1, arg2, arg3;
    
    __asm__ volatile (
        "movl %%eax, %0\n"
        "movl %%ebx, %1\n"
        "movl %%ecx, %2\n"
        "movl %%edx, %3\n"
        : "=r"(syscall_num), "=r"(arg1), "=r"(arg2), "=r"(arg3)
    );
    
    int ret = 0;
    
    switch (syscall_num) {
        case SYS_PRINT: {
            const char* str = (const char*)arg1;
            print_info(str);
            ret = strlen(str);
            break;
        }
        case SYS_READ: {
            char* buf = (char*)arg1;
            int len = (int)arg2;
            // 简单实现：读取一行
            for (int i = 0; i < len - 1; i++) {
                char c = getchar();
                if (c == '\n') {
                    buf[i] = '\0';
                    break;
                }
                buf[i] = c;
                putchar(c);
            }
            ret = 0;
            break;
        }
        case SYS_EXIT: {
            print_info("System call: exit\n");
            while(1) __asm__ volatile ("hlt");
            break;
        }
        case SYS_GETPID: {
            ret = 1;  // 固定 PID = 1
            break;
        }
        case SYS_SLEEP: {
            uint32_t ms = arg1;
            sleep(ms);
            ret = 0;
            break;
        }
        case SYS_MALLOC: {
            size_t size = (size_t)arg1;
            ret = (int)malloc(size);
            break;
        }
        case SYS_FREE: {
            void* ptr = (void*)arg1;
            free(ptr);
            ret = 0;
            break;
        }
        default: {
            print_info("Unknown syscall: ");
            char buf[16];
            itoa(syscall_num, buf, 10);
            print_info(buf);
            print_info("\n");
            ret = -1;
            break;
        }
    }
    
    // 返回值存入 eax
    __asm__ volatile ("movl %0, %%eax" : : "r"(ret));
}