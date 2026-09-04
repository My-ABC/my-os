// src/tss.c
#include "tss.h"
#include "gdt.h"
#include "serial.h"
#include "string.h"

// 明确放入 .bss 段，加载时自动清零
struct tss_t tss __attribute__((section(".bss")));

// 声明汇编中的内核栈顶地址
extern char stack_top[];
extern void double_fault_task(void);

static struct tss_t double_fault_tss __attribute__((section(".bss")));
static uint8_t double_fault_stack[4096]
    __attribute__((section(".bss"), aligned(16)));

void tss_init(void) {
    serial_print("[TSS] Initializing...\n");

    // 双重保险：手动清零，保证所有保留字段为0
    memset(&tss, 0, sizeof(tss));

    // 设置关键字段
    tss.esp0 = (uint32_t)stack_top;
    tss.ss0 = 0x10;  // 内核数据段
    tss.io_map_base = sizeof(struct tss_t);

    serial_print("[TSS] esp0 = 0x");
    serial_print_hex(tss.esp0);
    serial_print("\n");
    serial_print("[TSS] ss0 = 0x");
    serial_print_hex(tss.ss0);
    serial_print("\n");

    // 3. 在GDT中设置TSS描述符（索引5，选择子 0x28）
    uint32_t base = (uint32_t)&tss;
    uint32_t limit = sizeof(tss) - 1;
    gdt_set_entry(5, base, limit, 0x89, 0x00);
    serial_print("[TSS] GDT entry set for TSS.\n");

    memset(&double_fault_tss, 0, sizeof(double_fault_tss));
    double_fault_tss.esp = (uint32_t)(double_fault_stack + sizeof(double_fault_stack));
    double_fault_tss.cr3 = 0;
    double_fault_tss.eip = (uint32_t)double_fault_task;
    double_fault_tss.eflags = 0x202;
    double_fault_tss.cs = 0x08;
    double_fault_tss.ss = 0x10;
    double_fault_tss.ds = 0x10;
    double_fault_tss.es = 0x10;
    double_fault_tss.fs = 0x10;
    double_fault_tss.gs = 0x10;
    double_fault_tss.io_map_base = sizeof(double_fault_tss);

    gdt_set_entry(6, (uint32_t)&double_fault_tss,
                  sizeof(double_fault_tss) - 1, 0x89, 0x00);
    serial_print("[TSS] Double-fault task TSS configured.\n");

    // 4. 执行 ltr 指令加载 TSS 到 TR 寄存器
    __asm__ volatile("ltr %w0" : : "r" ((uint16_t)0x28));
    serial_print("[TSS] ltr instruction executed.\n");

    // 5. 验证 TR 寄存器
    uint16_t tr;
    __asm__ volatile("str %w0" : "=r" (tr));
    serial_print("[TSS] TR register = 0x");
    serial_print_hex(tr);
    serial_print("\n");

    if (tr == 0x28) {
        serial_print("[TSS] Initialization successful!\n");
    } else {
        serial_print("[TSS] ERROR: TR register mismatch!\n");
    }
}

void tss_update_cr3(uint32_t page_directory) {
    double_fault_tss.cr3 = page_directory;
}