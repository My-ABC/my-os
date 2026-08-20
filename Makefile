# 最简 Makefile - 自动查找文件
# 检测编译器
CC := $(shell if command -v i686-elf-gcc >/dev/null 2>&1; then echo i686-elf-gcc; else echo gcc; fi)
LD := $(shell if command -v i686-elf-ld >/dev/null 2>&1; then echo i686-elf-ld; else echo ld; fi)

# 检测 nasm
AS := $(shell if command -v nasm >/dev/null 2>&1; then echo nasm; else echo "nasm not found"; exit 1; fi)


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

ISO = myos.iso
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

iso: build
	@mkdir -p iso/boot/grub
	@cp $(KERNEL) iso/boot/
	@echo "set timeout=5" > iso/boot/grub/grub.cfg
	@echo "set default=0" >> iso/boot/grub/grub.cfg
	@echo "menuentry \"MyOS\" {" >> iso/boot/grub/grub.cfg
	@echo "    multiboot /boot/$(KERNEL)" >> iso/boot/grub/grub.cfg
	@echo "    boot" >> iso/boot/grub/grub.cfg
	@echo "}" >> iso/boot/grub/grub.cfg
	@grub-mkrescue -o myos.iso iso/ --modules="multiboot biosdisk iso9660"
	@rm -rf iso
	@echo "ISO generated: myos.iso"

debug: build
	@echo "=== QEMU GDB server started on localhost:1234 ==="
	@echo "=== In another terminal: i686-elf-gdb myos.bin ==="
	@echo "=== (gdb) target remote localhost:1234 ==="
	@echo "=== (gdb) b kmain  # set breakpoint ==="
	qemu-system-i386 -kernel $(KERNEL) -serial stdio -S -s -no-reboot -no-shutdown

gdb:
	i686-elf-gdb -ex "target remote localhost:1234" $(KERNEL)

run-iso: iso
	qemu-system-i386 -cdrom myos.iso -serial stdio

# 清理
clean:
	rm -rf build $(KERNEL) $(ISO)

.PHONY: build run run-nographic run-serial run-q35 run-q35-nographic iso run-iso debug clean