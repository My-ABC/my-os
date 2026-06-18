# ============================================
#  My OS - Makefile
# ============================================

CC       = i686-elf-gcc
AS       = nasm
LD       = i686-elf-ld
QEMU     = qemu-system-i386

SRC_DIR  = src
INC_DIR  = include
BOOT_DIR = boot
BUILD_DIR = build

# 所有 C 源文件（包括子目录）
SRC_DIRS = src src/shell
C_SRCS   = $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c))
# 将 .c 文件转为 .o 文件（放在 build 目录下）
C_OBJS   = $(patsubst %.c, $(BUILD_DIR)/%.o, $(notdir $(C_SRCS)))

# 汇编文件（boot 目录下的所有 .asm）
ASM_SRCS = $(wildcard $(BOOT_DIR)/*.asm)
ASM_OBJS = $(patsubst $(BOOT_DIR)/%.asm, $(BUILD_DIR)/%.o, $(ASM_SRCS))

OBJS     = $(ASM_OBJS) $(C_OBJS)

CFLAGS   = -m32 -ffreestanding -nostdlib -nostdinc -fno-builtin -fno-stack-protector -I$(INC_DIR)
ASFLAGS  = -f elf32
LDFLAGS  = -m elf_i386 -T linker.ld -nostdlib
QEMUFLAGS = -kernel

TARGET   = kernel.elf

.PHONY: all clean run debug

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) $^ -o $@

# 汇编文件编译
$(BUILD_DIR)/%.o: $(BOOT_DIR)/%.asm | $(BUILD_DIR)
	$(AS) $(ASFLAGS) $< -o $@

# src 目录下的 C 文件
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(INC_DIR)/*.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# shell 目录下的 C 文件
$(BUILD_DIR)/%.o: $(SRC_DIR)/shell/%.c $(INC_DIR)/*.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	$(QEMU) $(QEMUFLAGS) $(TARGET)

debug: $(TARGET)
	$(QEMU) -kernel $(TARGET) -s -S &
	@echo "QEMU 已启动，等待 GDB 连接..."
	@echo "运行: gdb-multiarch -ex 'target remote localhost:1234' kernel.elf"

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
	@echo "清理完成！"

help:
	@echo "命令: make | make run | make debug | make clean"