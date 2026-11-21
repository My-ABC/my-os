# 最简 Makefile - 自动查找文件
# 检测编译器
CC := $(shell if command -v i686-elf-gcc >/dev/null 2>&1; then echo i686-elf-gcc; else echo gcc; fi)
LD := $(shell if command -v i686-elf-ld >/dev/null 2>&1; then echo i686-elf-ld; else echo ld; fi)

# 检测 nasm
AS := $(shell if command -v nasm >/dev/null 2>&1; then echo nasm; else echo "nasm not found"; exit 1; fi)


INCLUDE_DIRS = include include/arch/x86 include/drivers include/fs include/lib \
	include/mm include/os include/shell
CFLAGS = -m32 -ffreestanding -fno-pie -fno-stack-protector -O2 -g \
	$(addprefix -I,$(INCLUDE_DIRS))
CFLAGS += -MMD -MP
BUILD_DIR ?= build

# PANIC_DEMO=1 时内核启动后主动触发 INT3, 用于演示/测试蓝屏
ifdef PANIC_DEMO
CFLAGS += -DPANIC_DEMO
endif

ifdef KEYBOARD_DEMO
CFLAGS += -DKEYBOARD_DEMO
endif

ifdef ELF_DEMO
CFLAGS += -DELF_DEMO
endif

# SCANCODE_SET=1 或 2 时设置键盘扫描码集
ifdef SCANCODE_SET
CFLAGS += -DSCANCODE_SET=$(SCANCODE_SET)
endif

ASFLAGS = -f elf32
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib

# 自动查找所有 .c 和 .asm 文件
C_SRCS = $(shell find src -type f -name '*.c' -print)
ASM_SRCS = $(filter-out boot/gdt.asm boot/paging.asm,$(wildcard boot/*.asm))

# 自动生成目标文件列表
C_OBJS = $(patsubst src/%.c, $(BUILD_DIR)/%.o, $(C_SRCS))
DEPFILES = $(C_OBJS:.o=.d)
ASM_OBJS = $(patsubst boot/%.asm, $(BUILD_DIR)/%.o, $(ASM_SRCS))
ASM_OBJS += $(BUILD_DIR)/gdt_asm.o
ASM_OBJS += $(BUILD_DIR)/paging_asm.o
OBJS = $(ASM_OBJS) $(C_OBJS)

ISO = myos.iso
KERNEL = myos.bin
QEMU ?= qemu-system-i386
DISK ?= disk.img
DISK_SIZE ?= 64M

ifdef HIGHMEM_QEMU_TEST
CFLAGS += -DPMM_QEMU_HIGHMEM_TEST
BUILD_DIR := build/highmem-test
KERNEL := build/highmem-test/myos.bin
endif

-include $(DEPFILES)

# 构建
build: $(OBJS)
	mkdir -p $(dir $(KERNEL))
	$(LD) $(LDFLAGS) $^ -o $(KERNEL)

test:
	python3 -m unittest discover -s test/python -p 'test_*.py' -v

test-qemu: build
	python3 test/integration/run_qemu_integration.py --qemu "$(QEMU)" --kernel "$(KERNEL)"

test-qemu-highmem:
	$(MAKE) HIGHMEM_QEMU_TEST=1 build
	python3 test/integration/run_qemu_highmem.py --qemu "$(QEMU)" \
		--kernel build/highmem-test/myos.bin

# 编译 C 文件
$(BUILD_DIR)/%.o: src/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# 编译汇编文件
$(BUILD_DIR)/%.o: boot/%.asm
	mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

# 单独编译 gdt.asm
$(BUILD_DIR)/gdt_asm.o: boot/gdt.asm
	mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

# 单独编译 paging.asm
$(BUILD_DIR)/paging_asm.o: boot/paging.asm
	mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

# 运行：通过 GRUB ISO 启动；仅在已有镜像时附加 FAT32 IDE 磁盘
run: iso
	@if test -f "$(DISK)"; then \
		echo "Attaching disk image: $(DISK)"; \
		qemu-system-i386 -boot order=d -cdrom $(ISO) -drive file="$(DISK)",format=raw,if=ide -serial stdio; \
	else \
		echo "No disk image found at $(DISK); booting without an IDE disk"; \
		qemu-system-i386 -boot order=d -cdrom $(ISO) -serial stdio; \
	fi

# 直接 -kernel 启动，不经过 GRUB，仅用于启动链调试
run-kernel: build
	qemu-system-i386 -kernel $(KERNEL) -serial stdio

# 无图形界面运行, 串口输出到终端
run-nographic: build
	qemu-system-i386 -kernel $(KERNEL) -nographic -serial stdio

run-serial: build
	qemu-system-i386 -kernel $(KERNEL) -serial stdio

disk:
	@if test -f "$(DISK)"; then \
		echo "Disk image already exists: $(DISK)"; \
	else \
		command -v mkfs.fat >/dev/null 2>&1 || { echo "Error: mkfs.fat is required"; exit 1; }; \
		truncate -s $(DISK_SIZE) "$(DISK)"; \
		mkfs.fat -F 32 -n MYOS "$(DISK)"; \
		echo "Created FAT32 disk image: $(DISK) ($(DISK_SIZE))"; \
	fi

run-disk: build disk
	qemu-system-i386 -kernel $(KERNEL) -drive file=$(DISK),format=raw,if=ide -serial stdio

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
	@echo "set gfxmode=640x480x32" >> iso/boot/grub/grub.cfg
	@echo "set gfxpayload=keep" >> iso/boot/grub/grub.cfg
	@echo "menuentry \"MyOS\" {" >> iso/boot/grub/grub.cfg
	@echo "    multiboot /boot/$(KERNEL)" >> iso/boot/grub/grub.cfg
	@echo "    boot" >> iso/boot/grub/grub.cfg
	@echo "}" >> iso/boot/grub/grub.cfg
	@grub-mkrescue -d /usr/lib/grub/i386-pc/ -o myos.iso iso/ --modules="multiboot biosdisk iso9660"
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
	qemu-system-i386 -machine q35 -cdrom $(ISO) -serial stdio

# 清理
clean:
	rm -rf build $(KERNEL) $(ISO) test/__pycache__ \
		test/python/__pycache__ test/integration/__pycache__

.PHONY: build test test-qemu test-qemu-highmem disk run run-kernel run-nographic run-serial run-disk run-q35 run-q35-nographic iso run-iso debug clean