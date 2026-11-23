# 03 — 中断与陷阱

[English](../en/03-interrupts.md) | 中文

## 桩层（`boot/interrupt.asm`）

NASM 汇编为异常、IRQ 和 `int 0x80` 系统调用门提供统一桩。每个桩把被中
断上下文压成 `struct irq_frame`（布局见 `include/arch/x86/regs.h`）后
尾调用 C 处理器。`isr14` 桩是特例：`isr14_handler` 返回非零内核
`irq_frame *` 时，它切换到该帧的栈并重放 IRQ0 收尾序列，放弃出错上下
文（见 [02-memory.md](02-memory.md)）。

## IDT（`src/arch/x86/idt.c`）

`idt_init()` 填充 IDT：CPU 异常用陷阱/中断门，PIC 各线用 IRQ 门，
`int 0x80` 用 DPL=3 门使 Ring 3 可调用。门描述符引用
`boot/interrupt.asm` 导出的桩地址。

## PIC（`src/drivers/pic.c`）

主/从 8259 重映射（`pic_remap()`），避免 IRQ0–15 与 CPU 异常冲突。
`pic_set_mask`/`pic_clear_mask` 控制各线屏蔽；`kmain` 在初始化后才解
除 IRQ0（PIT）和 IRQ1（键盘）的屏蔽，其余保持屏蔽。

## PIT（`src/drivers/pit.c`）

8253 PIT 通道 0，频率 `PIT_FREQUENCY` = 100 Hz。IRQ0 递增 tick 计数
（`pit_ticks()`），供 `tick` 命令、调度和测试使用。

## 键盘（`src/drivers/keyboard.c`）

IRQ1 上的 PS/2 键盘驱动：scancode→ASCII 转换，支持可配置 scancode
set（`SCANCODE_SET` 编译宏），按键事件供 Shell 的 `gets()`，
`keyboard_wait_key()` 供演示路径使用。

## RTC（`src/drivers/rtc.c`）

通过端口 I/O 读取 MC146818 RTC；`rtc_read_time()` +
`rtc_to_beijing_time()` 支撑 `time` 命令和启动横幅。CMOS/BCD 处理和
Unix 时间戳辅助函数也在此。

## 串口（`src/drivers/serial.c`）

COM1 轮询收发（`serial_print`、`serial_print_hex`、`serial_printf`）
以及 `serial_getchar()` 输入；COM3（`serial3_putchar`/`serial3_print`）
是额外的仅发送端口，镜像 Ring 3 `IO_WRITE` 的 shell 输出。大多数内核
诊断同时写 VGA 和 COM1，无头 QEMU 会话可通过串口 socket 驱动。
