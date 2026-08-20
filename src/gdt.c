#include "gdt.h"
#include "serial.h"

// GDT 数组（6 个条目：空描述符、内核代码段、内核数据段、用户代码段、用户数据段、TSS）
static struct gdt_entry gdt[6];
static struct gdt_ptr gdt_ptr;

// 设置 GDT 条目
void gdt_set_entry(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t granularity) {
    gdt[index].limit_low   = (limit & 0xFFFF);
    gdt[index].base_low    = (base & 0xFFFF);
    gdt[index].base_middle = (base >> 16) & 0xFF;
    gdt[index].access      = access;
    gdt[index].granularity = ((limit >> 16) & 0x0F) | (granularity & 0xF0);  // 高 4 位来自 granularity
    gdt[index].base_high   = (base >> 24) & 0xFF;
}

// 加载 GDT（汇编接口）
extern void gdt_load(struct gdt_ptr* ptr);

// 初始化 GDT
void gdt_init(void) {
    static int initialized = 0;
    if (initialized) {
        serial_print("[GDT] Already initialized, skipping\n");
        return;
    }
    initialized = 1;
    
    serial_print("[GDT] Initializing GDT\n");

    // 设置 GDT 指针
    gdt_ptr.limit = (sizeof(struct gdt_entry) * 5) - 1;
    gdt_ptr.base  = (uint32_t)&gdt;

    serial_print("[GDT] gdt_limit = ");
    serial_print_hex(gdt_ptr.limit);
    serial_print("\n");

    serial_print("[GDT] gdt_base = ");
    serial_print_hex(gdt_ptr.base);
    serial_print("\n");

    // 设置 GDT 条目
    
    // 0: 空描述符（必须存在）
    gdt_set_entry(0, 0, 0, 0, 0);

    // 1: 内核代码段（基址 0，限制 4GB，特权级 0，可执行）
    gdt_set_entry(1, 0, 0xFFFFFFFF, 
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_CODE_DATA | GDT_ACCESS_CODE_READABLE,
                  GDT_GRANULARITY_4KB | GDT_GRANULARITY_32BIT);

    // 2: 内核数据段（基址 0，限制 4GB，特权级 0，可读写）
    gdt_set_entry(2, 0, 0xFFFFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_CODE_DATA | GDT_ACCESS_DATA_WRITABLE,
                  GDT_GRANULARITY_4KB | GDT_GRANULARITY_32BIT);

    // 3: 用户代码段（基址 0，限制 4GB，特权级 3，可执行）
    gdt_set_entry(3, 0, 0xFFFFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_CODE_DATA | GDT_ACCESS_CODE_READABLE,
                  GDT_GRANULARITY_4KB | GDT_GRANULARITY_32BIT);

    // 4: 用户数据段（基址 0，限制 4GB，特权级 3，可读写）
    gdt_set_entry(4, 0, 0xFFFFFFFF,
                  GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_CODE_DATA | GDT_ACCESS_DATA_WRITABLE,
                  GDT_GRANULARITY_4KB | GDT_GRANULARITY_32BIT);

    serial_print("[GDT] GDT entries set up\n");

    serial_print("[GDT] GDT table contents:\n");
    for (int i = 0; i < 5; i++) {
        serial_print("  entry ");
        serial_print_dec(i);
        serial_print(": ");
        serial_print_hex(*(uint32_t*)&gdt[i]);
        serial_print(" ");
        serial_print_hex(*(uint32_t*)((char*)&gdt[i] + 4));
        serial_print("\n");
    }

    // 加载 GDT
    gdt_load(&gdt_ptr);

    serial_print("[GDT] GDT loaded successfully\n");
}