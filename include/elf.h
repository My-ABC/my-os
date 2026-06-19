#ifndef _ELF_H
#define _ELF_H

#include "stdint.h"

// ELF 头
struct elf_header {
    uint32_t magic;
    uint8_t  elf_class;
    uint8_t  data;
    uint8_t  version;
    uint8_t  os_abi;
    uint8_t  padding[8];
    uint16_t type;
    uint16_t machine;
    uint32_t version2;
    uint32_t entry;
    uint32_t phoff;
    uint32_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} __attribute__((packed));

// 程序头
struct elf_program_header {
    uint32_t type;
    uint32_t offset;
    uint32_t vaddr;
    uint32_t paddr;
    uint32_t filesz;
    uint32_t memsz;
    uint32_t flags;
    uint32_t align;
} __attribute__((packed));

#define ELF_MAGIC 0x464C457F

// 加载 ELF 文件并创建进程
int elf_load(void* data, const char* name);

#endif