# 最简 Makefile - 自动查找文件
CC = i686-elf-gcc
AS = nasm
LD = i686-elf-ld

CFLAGS = -m32 -ffreestanding -fno-pie -fno-stack-protector -O2 -g -Iinclude
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

# 清理
clean:
	rm -rf build $(KERNEL)

.PHONY: build run clean