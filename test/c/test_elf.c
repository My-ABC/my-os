#include "elf.h"
#include "paging.h"
#include "pmm.h"
#include "ring3.h"
#include "string.h"
#include "test_support.h"

static void make_valid_elf(unsigned char *image, unsigned int image_size) {
    struct elf32_header *header = (struct elf32_header *)image;
    struct elf32_program_header *program;

    memset(image, 0, image_size);
    header->ident[0] = 0x7F;
    header->ident[1] = 'E';
    header->ident[2] = 'L';
    header->ident[3] = 'F';
    header->ident[4] = ELF32_CLASS;
    header->ident[5] = ELF32_DATA_LSB;
    header->ident[6] = ELF32_VERSION_CURRENT;
    header->type = ELF32_TYPE_EXEC;
    header->machine = ELF32_MACHINE_I386;
    header->version = ELF32_VERSION_CURRENT;
    header->entry = 0x1000U;
    header->phoff = sizeof(*header);
    header->ehsize = sizeof(*header);
    header->phentsize = sizeof(struct elf32_program_header);
    header->phnum = 1;

    program = (struct elf32_program_header *)(image + header->phoff);
    program->type = ELF32_PT_LOAD;
    program->offset = sizeof(*header) + sizeof(*program);
    program->vaddr = 0x1000U;
    program->filesz = 1U;
    program->memsz = 1U;
    program->flags = ELF32_PF_X;
}

int main(void) {
    unsigned char image[128];
    struct elf32_header *header = (struct elf32_header *)image;
    struct elf32_program_header *program;
    struct elf32_loaded_image loaded;
    unsigned int image_size = sizeof(struct elf32_header) +
                              sizeof(struct elf32_program_header) + 1U;

    make_valid_elf(image, sizeof(image));
    check(elf32_load(0, image_size, &loaded) == -1, "rejects null image");
    check(elf32_load(image, sizeof(struct elf32_header) - 1U, &loaded) == -1,
          "rejects truncated header");

    header->ident[0] = 0;
    check(elf32_load(image, image_size, &loaded) == -2,
          "rejects invalid identification");
    make_valid_elf(image, sizeof(image));

    header->phoff = sizeof(*header) + 8U;
    check(elf32_load(image, image_size, &loaded) == -3,
          "rejects truncated program-header table");
    make_valid_elf(image, sizeof(image));

    program = (struct elf32_program_header *)(image + header->phoff);
    program->filesz = 2U;
    program->memsz = 1U;
    check(elf32_load(image, image_size, &loaded) == -4,
          "rejects filesz larger than memsz");
    make_valid_elf(image, sizeof(image));

    program = (struct elf32_program_header *)(image + header->phoff);
    program->offset = image_size;
    program->filesz = 2U;
    program->memsz = 2U;
    check(elf32_load(image, image_size, &loaded) == -4,
          "rejects segment outside image");
    make_valid_elf(image, sizeof(image));

    program = (struct elf32_program_header *)(image + header->phoff);
    program->flags = 0;
    check(elf32_load(image, image_size, &loaded) == -5,
          "requires entry in executable segment");

    return finish_test_suite("ELF tests");
}

uint32_t paging_get_page_physical(uint32_t address) {
    (void)address;
    return 0;
}

void paging_unmap_page(uint32_t address) {
    (void)address;
}

void paging_map_page(uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags) {
    (void)virtual_addr;
    (void)physical_addr;
    (void)flags;
}

void *pmm_alloc_page(void) {
    return 0;
}

void pmm_free_page(void *address) {
    (void)address;
}

void serial_print(const char *text) {
    (void)text;
}

void serial_print_hex(uint32_t value) {
    (void)value;
}

void ring0_to_ring3(uint32_t *stack, void (*function)(void)) {
    (void)stack;
    (void)function;
}
