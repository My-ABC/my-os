#include "elf.h"
#include "paging.h"
#include "pmm.h"
#include "serial.h"
#include "string.h"
#include "ring3.h"

#define ELF_USER_STACK_TOP 0x00801000U

static int elf32_range_valid(uint32_t start, uint32_t length, uint32_t limit) {
    return start <= limit && length <= limit - start;
}

static void elf32_release_pages(uint32_t start, uint32_t end) {
    uint32_t address;
    for (address = start; address < end; address += PAGE_SIZE) {
        uint32_t physical = paging_get_page_physical(address);
        if (physical != 0U) {
            paging_unmap_page(address);
            pmm_free_page((void *)(physical & ~(PAGE_SIZE - 1U)));
        }
    }
}

int elf32_load(const void *image, uint32_t image_size,
               struct elf32_loaded_image *loaded) {
    const struct elf32_header *header = (const struct elf32_header *)image;
    uint32_t image_start = 0xFFFFFFFFU;
    uint32_t image_end = 0U;
    uint32_t entry_ok = 0U;
    uint32_t index;

    if (image == 0 || loaded == 0 || image_size < sizeof(struct elf32_header)) return -1;
    if (*(const uint32_t *)header->ident != ELF32_MAGIC ||
        header->ident[4] != ELF32_CLASS || header->ident[5] != ELF32_DATA_LSB ||
        header->ident[6] != ELF32_VERSION_CURRENT || header->type != ELF32_TYPE_EXEC ||
        header->machine != ELF32_MACHINE_I386 || header->version != ELF32_VERSION_CURRENT) return -2;
    if (header->ehsize != sizeof(struct elf32_header) ||
        header->phentsize != sizeof(struct elf32_program_header) || header->phnum == 0U ||
        header->phnum > 128U || header->phoff > image_size ||
        header->phnum > (image_size - header->phoff) / header->phentsize) return -3;

    for (index = 0U; index < header->phnum; ++index) {
        const struct elf32_program_header *program =
            (const struct elf32_program_header *)((const uint8_t *)image +
                                                  header->phoff + index * header->phentsize);
        uint32_t segment_end;
        if (program->type != ELF32_PT_LOAD || program->memsz == 0U) continue;
        if (program->filesz > program->memsz ||
            !elf32_range_valid(program->offset, program->filesz, image_size) ||
            program->vaddr >= USER_SPACE_END || program->memsz > USER_SPACE_END - program->vaddr) return -4;
        segment_end = program->vaddr + program->memsz;
        if ((program->flags & ELF32_PF_X) != 0U &&
            header->entry >= program->vaddr && header->entry < segment_end) entry_ok = 1U;
        if (program->vaddr < image_start) image_start = program->vaddr;
        if (segment_end > image_end) image_end = segment_end;
    }

    if (image_start == 0xFFFFFFFFU || !entry_ok || header->entry >= USER_SPACE_END) return -5;
    image_start &= ~(PAGE_SIZE - 1U);
    image_end = PAGE_ALIGN(image_end);

    for (uint32_t address = image_start; address < image_end; address += PAGE_SIZE) {
        void *physical;
        uint32_t flags = PAGE_PRESENT | PAGE_USER;
        if (paging_get_page_physical(address) != 0U) return -6;
        for (index = 0U; index < header->phnum; ++index) {
            const struct elf32_program_header *program =
                (const struct elf32_program_header *)((const uint8_t *)image +
                                                      header->phoff + index * header->phentsize);
            uint32_t segment_start = program->vaddr & ~(PAGE_SIZE - 1U);
            uint32_t segment_end = PAGE_ALIGN(program->vaddr + program->memsz);
            if (program->type == ELF32_PT_LOAD && (program->flags & ELF32_PF_W) != 0U &&
                address < segment_end && address + PAGE_SIZE > segment_start) {
                flags |= PAGE_WRITABLE;
                break;
            }
        }
        physical = pmm_alloc_page();
        if (physical == 0) {
            elf32_release_pages(image_start, address);
            return -7;
        }
        paging_map_page(address, (uint32_t)physical, flags);
        memset((void *)address, 0, PAGE_SIZE);
    }

    for (index = 0U; index < header->phnum; ++index) {
        const struct elf32_program_header *program =
            (const struct elf32_program_header *)((const uint8_t *)image +
                                                  header->phoff + index * header->phentsize);
        if (program->type == ELF32_PT_LOAD && program->filesz != 0U)
            memcpy((void *)program->vaddr, (const uint8_t *)image + program->offset, program->filesz);
    }

    loaded->entry = header->entry;
    loaded->image_start = image_start;
    loaded->image_end = image_end;
    serial_print("[ELF] Loaded ELF32 entry=");
    serial_print_hex(loaded->entry);
    serial_print(" range=");
    serial_print_hex(image_start);
    serial_print("-");
    serial_print_hex(image_end);
    serial_print("\n");
    return 0;
}

int elf_run(const void *image, uint32_t image_size) {
    struct elf32_loaded_image loaded;
    int result = elf32_load(image, image_size, &loaded);

    if (result != 0) {
        return result;
    }

    serial_print("[ELF] Entering Ring 3 entry=");
    serial_print_hex(loaded.entry);
    serial_print("\n");
    ring0_to_ring3((uint32_t *)ELF_USER_STACK_TOP,
                   (void (*)(void))loaded.entry);
    return 0;
}