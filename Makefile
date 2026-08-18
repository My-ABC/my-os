# 最简 Makefile - 自动查找文件
CC = i686-elf-gcc
AS = nasm
LD = i686-elf-ld

CFLAGS = -m32 -ffreestanding -fno-pie -fno-stack-protector -O2 -g -Iinclude

# PANIC_DEMO=1 时内核启动后主动触发 INT3, 用于演示/测试蓝屏
ifdef PANIC_DEMO
CFLAGS += -DPANIC_DEMO
endif
ASFLAGS = -f elf32
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib

# 自动查找所有 .c 和 .asm 文件
C_SRCS = $(wildcard src/*.c)
ASM_SRCS = $(wildcard boot/*.asm)

# 自动生成目标文件列表
C_OBJS = $(patsubst src/%.c, build/%.o, $(C_SRCS))
ASM_OBJS = $(patsubst boot/%.asm, build/%.o, $(ASM_SRCS))
OBJS = $(ASM_OBJS) $(C_OBJS)

KERNEL = myos.bin

# 构建
build: $(OBJS)
	$(LD) $(LDFLAGS) $^ -o $(KERNEL)

# 编译 C 文件
build/%.o: src/%.c
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

# 编译汇编文件
build/%.o: boot/%.asm
	mkdir -p build
	$(AS) $(ASFLAGS) $< -o $@

# 运行
run: build
	qemu-system-i386 -kernel $(KERNEL)

# 无图形界面运行, 串口输出到终端
run-nographic: build
	qemu-system-i386 -kernel $(KERNEL) -nographic

# 测试时钟中断: 校验 COM1 每秒输出一个递增计数
test: build
	./scripts/test_timer.sh

# 测试 INT3 蓝屏: 校验寄存器 dump 与 ACPI 自动重启
test-panic:
	./scripts/test_panic.sh

# 清理
clean:
	rm -rf build $(KERNEL)

.PHONY: build run run-nographic test test-panic clean