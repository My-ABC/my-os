#ifndef _ELF_H
#define _ELF_H

#include "stdint.h"

#define ELF32_IDENT_SIZE 16
#define ELF32_MAGIC 0x464C457FU
#define ELF32_CLASS 1
#define ELF32_DATA_LSB 1
#define ELF32_VERSION_CURRENT 1
#define ELF32_MACHINE_I386 3
#define ELF32_TYPE_EXEC 2
#define ELF32_PT_LOAD 1
#define ELF32_PF_X 1
#define ELF32_PF_W 2

struct elf32_header {
    uint8_t  ident[ELF32_IDENT_SIZE];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
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

struct elf32_program_header {
    uint32_t type;
    uint32_t offset;
    uint32_t vaddr;
    uint32_t paddr;
    uint32_t filesz;
    uint32_t memsz;
    uint32_t flags;
    uint32_t align;
} __attribute__((packed));

struct elf32_loaded_image {
    uint32_t entry;
    uint32_t image_start;
    uint32_t image_end;
};

int elf32_load(const void *image, uint32_t image_size,
               struct elf32_loaded_image *loaded);
int elf_run(const void *image, uint32_t image_size);

#endif