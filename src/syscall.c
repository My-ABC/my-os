#include "syscall.h"
#include "rtc.h"
#include "stdio.h"
#include "vga.h"
#include "acpi.h"

uint32_t syscall_handler() {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile(
        "mov %%eax, %0\n"
        "mov %%ebx, %1\n"
        "mov %%ecx, %2\n"
        "mov %%edx, %3\n"
        : "=r"(eax), "=r"(ebx), "=r"(ecx), "=r"(edx)
        :
        : "memory"
    );

    switch (eax) {
        case SYS_WRITE: { // 参数: ebx: const char *str
            vga_print(ebx);
            return 0;
        }
        case SYS_READ: { // 参数: ebx: char *str, ecx: size_t size
            gets(ebx, ecx);
            return 0;
        }
        case SYS_GETCHAR: {
            int c = getchar();
            return c;
        }
        case SYS_CLEAR: {
            vga_clear();
            return 0;
        }
        case SYS_REBOOT: {
            acpi_reboot();
            return 0;
        }
        case SYS_TIME: {
            rtc_time_t time;
            rtc_read_time(&time);
            uint32_t *t = rtc_to_unix_timestamp(&time);
            return t;
        }

        default:
            return -1;
    }
}