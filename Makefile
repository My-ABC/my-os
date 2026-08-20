# 最简 Makefile - 自动查找文件
CC = i686-elf-gcc
AS = nasm
LD = i686-elf-ld

CFLAGS = -m32 -ffreestanding -fno-pie -fno-stack-protector -O2 -g -Iinclude

# PANIC_DEMO=1 时内核启动后主动触发 INT3, 用于演示/测试蓝屏
ifdef PANIC_DEMO
CFLAGS += -DPANIC_DEMO
endif

ifdef KEYBOARD_DEMO
CFLAGS += -DKEYBOARD_DEMO
endif

# SCANCODE_SET=1 或 2 时设置键盘扫描码集
ifdef SCANCODE_SET
CFLAGS += -DSCANCODE_SET=$(SCANCODE_SET)
endif
ASFLAGS = -f elf32
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib

# 自动查找所有 .c 和 .asm 文件
C_SRCS = $(wildcard src/*.c)
ASM_SRCS = $(wildcard boot/*.asm)

# 自动生成目标文件列表
C_OBJS = $(patsubst src/%.c, build/%.o, $(C_SRCS))
ASM_OBJS = $(patsubst boot/%.asm, build/%.o, $(ASM_SRCS))
ASM_OBJS += build/gdt_asm.o
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

# 单独编译 gdt.asm
build/gdt_asm.o: boot/gdt.asm
	mkdir -p build
	$(AS) $(ASFLAGS) $< -o $@

# 运行
run: build
	qemu-system-i386 -kernel $(KERNEL) -serial stdio

# 无图形界面运行, 串口输出到终端
run-nographic: build
	qemu-system-i386 -kernel $(KERNEL) -nographic -serial stdio

run-serial: build
	qemu-system-i386 -kernel $(KERNEL) -serial stdio

# 使用 q35 机型运行（更好的 ACPI 支持）
run-q35: build
	qemu-system-i386 -machine q35 -kernel $(KERNEL) -serial stdio

# 使用 q35 机型运行，串口输出到终端
run-q35-nographic: build
	qemu-system-i386 -machine q35 -kernel $(KERNEL) -nographic

debug: build
	@echo "=== QEMU GDB server started on localhost:1234 ==="
	@echo "=== In another terminal: i686-elf-gdb myos.bin ==="
	@echo "=== (gdb) target remote localhost:1234 ==="
	@echo "=== (gdb) b kmain  # set breakpoint ==="
	qemu-system-i386 -kernel $(KERNEL) -serial stdio -S -s -no-reboot -no-shutdown on gdb"

gdb:
	i686-elf-gdb -ex "target remote localhost:1234" $(KERNEL)

# 清理
clean:
	rm -rf build $(KERNEL)

.PHONY: build run run-nographic run-serial run-q35 run-q35-nographic debug clean