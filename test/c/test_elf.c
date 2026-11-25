#include "elf.h"
#include "paging.h"
#include "pmm.h"
#include "ring3.h"
#include "string.h"
#include "test_support.h"

static struct user_mm fake_mm;

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
    check(elf32_load(0, image_size, &fake_mm, &loaded) == -1, "rejects null image");
    check(elf32_load(image, sizeof(struct elf32_header) - 1U, &fake_mm, &loaded) == -1,
          "rejects truncated header");

    header->ident[0] = 0;
    check(elf32_load(image, image_size, &fake_mm, &loaded) == -2,
          "rejects invalid identification");
    make_valid_elf(image, sizeof(image));

    header->phoff = sizeof(*header) + 8U;
    check(elf32_load(image, image_size, &fake_mm, &loaded) == -3,
          "rejects truncated program-header table");
    make_valid_elf(image, sizeof(image));

    program = (struct elf32_program_header *)(image + header->phoff);
    program->filesz = 2U;
    program->memsz = 1U;
    check(elf32_load(image, image_size, &fake_mm, &loaded) == -4,
          "rejects filesz larger than memsz");
    make_valid_elf(image, sizeof(image));

    program = (struct elf32_program_header *)(image + header->phoff);
    program->offset = image_size;
    program->filesz = 2U;
    program->memsz = 2U;
    check(elf32_load(image, image_size, &fake_mm, &loaded) == -4,
          "rejects segment outside image");
    make_valid_elf(image, sizeof(image));

    program = (struct elf32_program_header *)(image + header->phoff);
    program->flags = 0;
    check(elf32_load(image, image_size, &fake_mm, &loaded) == -5,
          "requires entry in executable segment");

    return finish_test_suite("ELF tests");
}

uint64_t paging_get_page_physical(uint32_t address) {
    (void)address;
    return 0;
}

void paging_unmap_page(uint32_t address) {
    (void)address;
}

void paging_map_page(uint32_t virtual_addr, uint64_t physical_addr, uint32_t flags) {
    (void)virtual_addr;
    (void)physical_addr;
    (void)flags;
}

uint64_t pmm_alloc_page(void) {
    return 0U;
}

void pmm_free_page(uint64_t address) {
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

uint64_t paging_mm_get_phys(struct user_mm *mm, uint32_t address) {
    (void)mm;
    (void)address;
    return 0U;
}

void paging_mm_map(struct user_mm *mm, uint32_t virtual_addr,
                   uint64_t physical_addr, uint32_t flags) {
    (void)mm;
    (void)virtual_addr;
    (void)physical_addr;
    (void)flags;
}

void paging_mm_release(struct user_mm *mm) {
    (void)mm;
}

struct user_mm *paging_mm_create(void) {
    return &fake_mm;
}

uint32_t paging_switch_cr3(uint32_t phys) {
    (void)phys;
    return 0U;
}

int ring3_schedule_mm(void (*func)(void), struct user_mm *mm,
                      uint32_t image_start, uint32_t image_end,
                      int foreground, int vt) {
    (void)func;
    (void)mm;
    (void)image_start;
    (void)image_end;
    (void)foreground;
    (void)vt;
    return 0;
}
