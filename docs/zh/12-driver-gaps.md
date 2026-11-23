# 11 — 驱动生态盘点与缺口

[English](../en/12-driver-gaps.md) | 中文

本文盘点 `src/drivers/` 中现有驱动的能力与限制，按总线/设备类别列出
缺失驱动、优先级和对应的 QEMU 测试设备。它对应 `TODO.md` 中"梳理并
枚举驱动生态缺口"条目；"基于实际需求逐项选择和实现驱动"一项保持
未勾选，因为驱动按需逐个引入。

## 现有驱动

| 驱动 | 能力 | 限制 |
|---|---|---|
| ATA PIO（`ata.c`） | 主通道一号盘，LBA28，轮询 PIO 单次读写 ≤255 扇区 | 无 LBA48、无 DMA/UDMA、无写缓存刷新、无 IRQ14、单通道（无从盘/副通道）、无 ATAPI、错误细节不向上传递 |
| 键盘（`keyboard.c`） | IRQ1，扫描码集 1–2，每 vt 256 字节环形缓冲（`VT_COUNT` 槽），ASCII + Shift/Caps/Num，F1/F2 被消费用于 vt 切换，Ctrl+C → SIGINT | F1/F2 之外的方向键/功能键/扩展键未映射，无转义序列，无 UTF-8，Alt/Ctrl 大多未使用，缓冲溢出静默丢键 |
| PIC（`pic.c`） | 8259A 主/从重映射到 IRQ32+，按线屏蔽，EOI | 无伪中断处理，无优先级/IRR 检查，无中断共享模型 |
| PIT（`pit.c`） | 8253 通道 0，`PIT_FREQUENCY`（100 Hz），tick 计数，驱动调度器和 VBE 光标闪烁 | 仅通道 0，无回读/延迟测量，时间源精度粗 |
| RTC（`rtc.c`） | MC146818 CMOS 时间，BCD/12–24 小时制解码，世纪推断，北京时间与 Unix 时间戳辅助 | 无 IRQ8 闹钟/更新结束中断，世纪按范围猜测，无 HPET 兜底 |
| 串口（`serial.c`） | COM1 + COM3 38400 8N1，两口均为有上限的轮询发送（约 100 万次 LSR 轮询后丢弃，COM3 超时打一次性警告），COM1 有非阻塞接收 `serial_getchar` | `serial_getchar` 未被 Shell 使用（键盘是唯一控制台输入），无 IRQ4，COM3 仅作发送镜像，无流控 |
| VGA（`vga.c`） | 80×25 文本模式，16 色，硬件光标，滚屏 | 仅文本模式；有帧缓冲时被 VBE 取代 |
| VBE（`vbe.c`） | 使用 Multiboot/BIOS 帧缓冲信息（非 VBE BIOS 调用），映射到 `0xE0000000`，支持 8/15/16/24/32 bpp 及各字段掩码，软件 8×8 文本渲染，光标闪烁 | 不设置模式（沿用 GRUB 模式），无双缓冲，无加速，无指针/输入 |
| ACPI（`acpi.c`） | RSDP 扫描（EBDA + BIOS 区），RSDT/XSDT 遍历找 FADT，FADT 复位寄存器重启，PM1 S5 关机（`_S5_` AML 字节模式 hack），三级兜底（0xCF9、KBC 复位、三重错误） | 只消费 FADT；无 MADT/HPET/MCFG，无通用表注册，无 SDT 长度/校验和校验，无真正的 AML 解释器 |
| 字体（`ascii_font.c`） | VBE 文本渲染用 8×8 点阵字形 | 纯数据 |
| 虚拟终端（`vt.c`） | 两个屏幕 tty（F1/F2），VGA cell/VBE 帧缓冲私有备份，每 vt 前台槽，`vt_service()` 延迟切换与 shell 重启 | `VT_COUNT` 固定为 2，无滚动回溯，VBE 备份分配失败仅串口日志、切换失效 |

## 按总线/设备列出的缺失驱动

### ISA / 传统设备

| 驱动 | 原因 | QEMU 测试设备 |
|---|---|---|
| 串口 RX 中断 + 控制台输入 | 无头 Shell 目前无法接收输入；轮询辅助已存在但未用 | 测试已用 `-serial tcp:...`；补输入路径即可 |
| PS/2 鼠标（i8042 AUX，IRQ12） | 任何指针驱动 UI 的前提 | 默认 `-machine pc` 自带 PS/2 鼠标；monitor `sendkey mouse_move` |
| i8042 控制器加固 | 自检、AUX 使能、命令超时 | 与键盘同一 i8042 |

### PCI（目前完全没有枚举）

没有 PCI 配置空间访问（`0xCF8`/`0xCFC`），所有 PCI 设备都不可达。
最小总线层（配置读写、BAR 分配、INTx 路由）是以下全部设备的基础：

| 驱动 | 原因 | QEMU 测试设备 |
|---|---|---|
| PCI 总线枚举 | 网卡、存储、USB、GPU 的基础 | `-machine pc`/`q35` 上任意 `-device` |
| e1000 或 virtio-net 网卡 | 首个网络驱动；TODO 列有网络规划 | `-device e1000` / `-netdev user` |
| AHCI SATA | 用 DMA + NCQ + 热插拔取代 PIO ATA | q35 上 `-device ich9-ahci` |
| xHCI/EHCI USB | USB 键盘/鼠标/存储 | `-device qemu-xhci -device usb-kbd` |
| Bochs/VirtIO GPU | 真模式设置 + 加速，优于沿用 VBE 模式 | `-device bochs-display` / `-device virtio-gpu` |
| MSI/MSI-X + IOAPIC 路由 | PCI 中断超出 ISA 范围后必需 | 随 PCI 层一起 |

### 存储

| 驱动 | 原因 | QEMU 测试设备 |
|---|---|---|
| ATA LBA48 + flush + DMA | 大容量磁盘上的正确性/持久性 | 内置 `-drive if=ide` |
| ATA 副通道 / ATAPI | 第二块盘、CD-ROM | `-drive if=ide,index=1` |
| AHCI（见 PCI） | 现代路径，取代 ATA PIO | q35 + `ich9-ahci` |

### 中断 / 定时器 / 电源

| 驱动 | 原因 | QEMU 测试设备 |
|---|---|---|
| ACPI MADT 解析 | CPU 数量、IOAPIC 位置、IRQ 重映射——SMP 前提 | 始终存在 |
| IOAPIC + 本地 APIC | PCI IRQ 路由，SMP 前提 | q35/`-machine pc` |
| HPET | 高精度定时器（与 PIT 对比评估） | `-machine q35` 提供 HPET |
| RTC IRQ8 闹钟 | 唤醒/定时事件 | CMOS 模拟 |
| AML/DSDT 解释器 | 完整电源管理、温控、电池——工作量大，对教学内核价值低 | 不适用 |

## 建议引入顺序

1. **设备子系统骨架**（TODO：注册/探测）——先于新总线，让每个驱动
   落在统一的形态里。
2. **PCI 枚举**——解锁网卡/存储/USB/GPU，杠杆最大。
3. **串口控制台输入**——成本低，对无头使用立即有益。
4. **PS/2 鼠标**——与未来 GUI 工作配套。
5. **ATA 加固**（LBA48、flush、DMA）——增量改进，让 FAT32 路径
   在真实磁盘上保持可靠。
6. **ACPI MADT + IOAPIC/APIC**——SMP（TODO 的 SMP 部分）之前必需。
7. **网卡（e1000/virtio）**——等 PCI 和设备层就位后。
8. 以后/低优先级：HPET、USB HID、AML 解释器、NVMe。

每项只在有具体功能需要时才引入；本文记录候选，不构成承诺。
