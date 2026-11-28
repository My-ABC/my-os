# 10 — 驱动

[English](../en/10-drivers.md) | 中文

所有驱动位于 `src/drivers/`，头文件在 `include/drivers/`。驱动或轮询
或走 IRQ，取决于硬件能力；目前还没有设备注册总线层。

| 驱动 | 文件 | 说明 |
|---|---|---|
| ATA PIO | `ata.c` | 主盘、LBA28、512 字节扇区；见 [09-filesystem.md](09-filesystem.md) |
| 键盘 | `keyboard.c` | PS/2 scancode→ASCII，IRQ1，可配置 scancode set（`SCANCODE_SET`）；方向键输出 `KEYBOARD_KEY_*` 控制字节（0x01–0x04） |
| PIC | `pic.c` | 8259 主/从重映射与逐线屏蔽辅助函数 |
| PIT | `pit.c` | 8253 通道 0，`PIT_FREQUENCY` = 100 Hz，tick 计数 |
| RTC | `rtc.c` | MC146818 CMOS 时间、BCD 解码、12/24 小时制处理（PM 位在 BCD 解码前提取；解码后小时 >12 时按 24 小时时间处理，因为部分 RTC 的模式位不可靠）、假设 RTC 为 UTC 的北京时间换算、Unix 时间戳 |
| 串口 | `serial.c` | COM1 轮询收发，用于内核诊断（`serial_printf` 系列，另有 `serial_flush()` 在可能断电的写操作前排空 UART）；COM3（`serial3_putchar`/`serial3_print`）镜像 Ring 3 `IO_WRITE` 输出 |
| VGA | `vga.c` | 80x25 文本模式，经 `vt_render_index()` 的每 vt 光标/颜色状态 |
| VBE | `vbe.c` | 来自 Multiboot/BIOS 信息的线性帧缓冲；每 vt 渲染目标、蓝屏演示 |
| VT | `vt.c` | 两个虚拟终端（F1/F2）：每 vt 屏幕备份、`vt_service()` 中的延迟切换与 shell 重启——见 [15-virtual-terminals.md](15-virtual-terminals.md) |
| ACPI | `acpi.c` | 经 ACPI PM 寄存器关机/重启；关机路径在每次可能断电的写操作前打印 `powering off via ...` 标记并 `serial_flush()` 排空 COM1，诊断信息不会被腰斩 |
| 字库 | `ascii_font.c` | VBE 文本渲染用位图字模数据 |

## 横切说明

- 大多数子系统把控制台输出同时写到 VGA 和串口，无头 QEMU 会话可完全
  通过串口 socket 驱动。
- 驱动初始化在 `kmain`（`src/kernel.c`）中按序进行；IRQ 解屏蔽分阶
  段，PIT/键盘在 IDT 就绪后才开启。
- ACPI 表解析目前只覆盖 PM 电源寄存器（MADT、HPET、AML 未实现）——
  见 `TODO.md` 跟踪。
- 能力盘点、缺失驱动清单、优先级和 QEMU 测试设备见
  [12-driver-gaps.md](12-driver-gaps.md)。
