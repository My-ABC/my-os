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
`pic_set_mask`/`pic_clear_mask` 控制各线屏蔽；`kmain` 结束时只有
IRQ0（PIT）和 IRQ1（键盘）处于解除屏蔽状态，其余全部保持屏蔽。

## PIT（`src/drivers/pit.c`）

8253 PIT 通道 0，频率 `PIT_FREQUENCY` = 100 Hz。IRQ0 递增 tick 计数
（`pit_ticks()`），供 `tick` 命令、调度和测试使用。

## 键盘（`src/drivers/keyboard.c`）

IRQ1 上的 PS/2 键盘驱动：scancode→ASCII 转换，支持可配置 scancode
set（`SCANCODE_SET` 编译宏）。按键进入**每虚拟终端独立**的 256 字节
环形缓冲（`keyboard_getchar_vt(vt)`）；ISR 只为*当前可见* vt 入队，
隐藏终端收不到按键。F1/F2 make code（scan set 1 的 `0x3B`/`0x3C`）由
ISR 消费并转为延迟的 `vt_request_switch()`，由 `vt_service()` 之后
执行——不进入输入流。Ctrl+C 向当前 vt 的前台进程投递 SIGINT。
`keyboard_wait_key()` 保留给演示路径。

## 虚拟终端（`src/drivers/vt.c`）

F1 选 vt 0，F2 选 vt 1。每个 vt 拥有私有屏幕备份（VGA 文本模式为
80x25 cell 拷贝，VBE 模式为离屏帧缓冲 stride）和独立的前台 PID 槽。
`vt_service()` 由内核空闲循环轮询（`shell3_supervisor` 或
`shell_read_line`，两者互斥），执行延迟切换并重启死亡的
`/shell3.elf`。详见 [15-virtual-terminals.md](15-virtual-terminals.md)。

## RTC（`src/drivers/rtc.c`）

通过端口 I/O 读取 MC146818 RTC；`rtc_read_time()` +
`rtc_to_beijing_time()` 支撑 `time` 命令和启动横幅。CMOS/BCD 处理和
Unix 时间戳辅助函数也在此。

## 串口（`src/drivers/serial.c`）

COM1 轮询输出（`serial_print`、`serial_print_hex`、`serial_printf`）
承载内核侧输出——诊断信息及内核 shell 命令（如 `echo`）——另有
`serial_getchar()` 输入。COM3（`serial3_putchar`/`serial3_print`）是
仅发送端口，镜像两个虚拟终端的 Ring 3 `IO_WRITE` 输出（相互交错）。
两条 TX 路径的 LSR 等待都有上限（约 100 万次轮询），缺失 UART 不会
挂死内核；COM3 超时会在 COM1 上打一条一次性警告。大多数内核诊断同
时写 VGA 和 COM1，无头 QEMU 会话可通过串口 socket 驱动。
