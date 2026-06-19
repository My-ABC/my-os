#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "memory.h"
#include "process.h"
#include "elf.h"

int elf_load(void* data, int (*entry)(void)) {
    struct elf_header* eh = (struct elf_header*)data;
    
    // 检查魔数
    if (eh->magic != ELF_MAGIC) {
        print_error("Not ELF file\n");
        return -1;
    }
    
    // 检查类型
    if (eh->type != 2) {  // ET_EXEC
        print_error("Not executable ELF\n");
        return -1;
    }
    
    print_info("ELF: entry=0x");
    char buf[16];
    uint32_t addr = eh->entry;
    for (int i = 7; i >= 0; i--) {
        buf[i] = "0123456789ABCDEF"[addr & 0xF];
        addr >>= 4;
    }
    buf[8] = '\0';
    print_info(buf);
    print_info("\n");
    
    // 加载程序段
    struct elf_program_header* ph = (struct elf_program_header*)((uint32_t)data + eh->phoff);
    
    for (int i = 0; i < eh->phnum; i++) {
        if (ph[i].type == 1) {  // PT_LOAD
            print_info("Loading segment: vaddr=0x");
            uint32_t vaddr = ph[i].vaddr;
            for (int j = 7; j >= 0; j--) {
                buf[j] = "0123456789ABCDEF"[vaddr & 0xF];
                vaddr >>= 4;
            }
            buf[8] = '\0';
            print_info(buf);
            print_info(" size=");
            int n = ph[i].memsz;
            if (n < 10) {
                buf[0] = '0' + n;
                buf[1] = '\0';
            } else if (n < 100) {
                buf[0] = '0' + (n / 10);
                buf[1] = '0' + (n % 10);
                buf[2] = '\0';
            } else if (n < 1000) {
                buf[0] = '0' + (n / 100);
                buf[1] = '0' + ((n / 10) % 10);
                buf[2] = '0' + (n % 10);
                buf[3] = '\0';
            } else {
                buf[0] = '0' + (n / 1000);
                buf[1] = '0' + ((n / 100) % 10);
                buf[2] = '0' + ((n / 10) % 10);
                buf[3] = '0' + (n % 10);
                buf[4] = '\0';
            }
            print_info(buf);
            print_info("\n");
            
            // 分配内存
            uint32_t* dest = (uint32_t*)ph[i].vaddr;
            uint32_t src = (uint32_t)data + ph[i].offset;
            memcpy(dest, (void*)src, ph[i].filesz);
            
            // 清零剩余部分
            if (ph[i].memsz > ph[i].filesz) {
                memset((uint8_t*)dest + ph[i].filesz, 0, ph[i].memsz - ph[i].filesz);
            }
        }
    }
    
    // 跳转到入口
    entry = (int (*)(void))eh->entry;
    return 0;
}