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
	qemu-system-i386 -kernel $(KERNEL) -serial stdio

# 无图形界面运行, 串口输出到终端
run-nographic: build
	qemu-system-i386 -kernel $(KERNEL) -nographic -serial stdio

# 使用 q35 机型运行（更好的 ACPI 支持）
run-q35: build
	qemu-system-i386 -machine q35 -kernel $(KERNEL) -serial stdio

# 使用 q35 机型运行，串口输出到终端
run-q35-nographic: build
	qemu-system-i386 -machine q35 -kernel $(KERNEL) -nographic

test: test-timer test-panic test-keyboard test-scancode-set2 test-rtc
	echo ""
	echo "All test ok!"

# 测试时钟中断: 校验 COM1 每秒输出一个递增计数
test-timer: build
	./scripts/test_timer.sh

# 测试 INT3 蓝屏: 校验寄存器 dump 与 ACPI 自动重启
test-panic:
	./scripts/test_panic.sh

# 测试键盘中断: 注入按键, 'b' 蓝屏, 其他键卡死
test-keyboard:
	./scripts/test_keyboard.sh

# 测试扫描码集2: 切换到扫描码集2后测试键盘输入
test-scancode-set2:
	./scripts/test_scancode_set2.sh

# 测试RTC: 读取并显示当前时间，验证年份处理是否正确
test-rtc:
	./scripts/test_rtc.sh

# 测试RTC: 使用指定时间测试，例如: make test-rtc-time RTC_TIME="2000-01-01T00:00:01"
test-rtc-time:
	RTC_TIME=$(RTC_TIME) ./scripts/test_rtc.sh

# 清理
clean:
	rm -rf build $(KERNEL)

.PHONY: build run run-nographic run-q35 run-q35-nographic test test-timer test-panic test-keyboard test-scancode-set2 test-rtc clean