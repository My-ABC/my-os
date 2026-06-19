#include "elf.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "memory.h"
#include "process.h"

int elf_load(void* data, const char* name) {
    struct elf_header* eh = (struct elf_header*)data;
    
    if (eh->magic != ELF_MAGIC) {
        print_error("Not ELF file\n");
        return -1;
    }
    
    if (eh->type != 2) {
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
    
    struct elf_program_header* ph = (struct elf_program_header*)((uint32_t)data + eh->phoff);
    
    for (int i = 0; i < eh->phnum; i++) {
        if (ph[i].type == 1) {
            uint32_t* dest = (uint32_t*)ph[i].vaddr;
            uint32_t src = (uint32_t)data + ph[i].offset;
            
            memcpy(dest, (void*)src, ph[i].filesz);
            
            if (ph[i].memsz > ph[i].filesz) {
                memset((uint8_t*)dest + ph[i].filesz, 0, ph[i].memsz - ph[i].filesz);
            }
        }
    }
    
    int pid = process_create((void (*)(void))eh->entry, name);
    if (pid < 0) {
        print_error("Failed to create process\n");
        return -1;
    }
    
    printf("ELF loaded: %s (PID=%d, entry=0x%x)\n", name, pid, eh->entry);
    return pid;
}