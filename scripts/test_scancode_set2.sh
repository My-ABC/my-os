#!/usr/bin/env bash
# 测试扫描码集2: 切换到扫描码集2后测试键盘输入
#   1) 切换到扫描码集2
#   2) 按 'a' -> 打印 Halted 后卡死
#   3) 按 'b' -> 进入蓝屏 panic
# monitor 用 -monitor pipe: 命名管道驱动, 不依赖 socat 等额外工具
# MAKE_ARGS 可传给 make (例如没有交叉工具链时: MAKE_ARGS="CC=gcc LD=ld")
set -u

KERNEL=${KERNEL:-myos.bin}
SCREENSHOT_DIR=${SCREENSHOT_DIR:-}
QEMU=${QEMU:-qemu-system-i386}

fail() {
    echo "FAIL: $1"
    exit 1
}

command -v "$QEMU" >/dev/null || fail "找不到 $QEMU, 请安装 qemu-system-x86"

make clean >/dev/null
# shellcheck disable=SC2086
make ${MAKE_ARGS:-} SCANCODE_SET=2 build >/dev/null || fail "构建失败"

# 启动内核, 等它进入等待按键状态后注入一个键, 返回清理过的 COM1 日志路径
run_with_key() {
    local key=$1 duration=$2
    local dir log mon pid
    dir=$(mktemp -d)
    log=$dir/serial.log
    mon=$dir/monitor

    mkfifo "$mon.in" "$mon.out"

    "$QEMU" -kernel "$KERNEL" -display none -serial file:"$log" \
        -monitor pipe:"$mon" >/dev/null 2>&1 &
    pid=$!

    exec 3>"$mon.in"          # 写端保持打开, 否则 qemu 会读到 EOF 关掉 monitor
    cat "$mon.out" >/dev/null &  # 排空 monitor 回显, 防止 qemu 写阻塞
    local drain=$!

    sleep 3  # 等内核初始化完并开始等待按键
    if [ -n "$SCREENSHOT_DIR" ]; then
        printf 'screendump %s\n' "$SCREENSHOT_DIR/prompt.ppm" >&3
    fi

    printf 'sendkey %s\n' "$key" >&3

    if [ -n "$SCREENSHOT_DIR" ]; then
        sleep 2
        printf 'screendump %s\n' "$SCREENSHOT_DIR/key-$key.ppm" >&3
    fi

    sleep "$duration"
    exec 3>&-
    kill "$pid" "$drain" 2>/dev/null
    wait "$pid" 2>/dev/null

    tr -d '\r' < "$log" > "$log.clean"
    echo "$log.clean"
}

echo "=== 扫描码集2: 按 'a': 期望卡死 ==="
A_LOG=$(run_with_key a 4)
cat "$A_LOG"
echo "======================="
grep -q 'Scancode set: 2' "$A_LOG" || fail "没有设置扫描码集2"
grep -q '^Key pressed: a$' "$A_LOG" || fail "IRQ1 没有收到按键 'a'"
grep -q '^Halted$' "$A_LOG" || fail "按 'a' 之后没有进入 halt 分支"
grep -q 'STOP: KERNEL PANIC' "$A_LOG" && fail "按 'a' 不应该蓝屏"

echo "=== 扫描码集2: 按 'b': 期望蓝屏 ==="
B_LOG=$(run_with_key b 10)
cat "$B_LOG"
echo "======================="
grep -q 'Scancode set: 2' "$B_LOG" || fail "没有设置扫描码集2"
grep -q '^Key pressed: b$' "$B_LOG" || fail "IRQ1 没有收到按键 'b'"
grep -q 'STOP: KERNEL PANIC' "$B_LOG" || fail "按 'b' 没有进入蓝屏"
grep -q '^EIP=0x' "$B_LOG" || fail "蓝屏没有 dump 寄存器"

echo "=== 扫描码集2: 小键盘 5: 期望识别为 '5' ==="
KP_LOG=$(run_with_key kp_5 4)
cat "$KP_LOG"
echo "==============================="
grep -q 'Scancode set: 2' "$KP_LOG" || fail "没有设置扫描码集2"
grep -q '^Key pressed: 5$' "$KP_LOG" || fail "小键盘 5 没有被识别"
grep -q '^Halted$' "$KP_LOG" || fail "小键盘按键之后没有进入 halt 分支"

echo "PASS: 扫描码集2键盘可用, 'b' 触发蓝屏, 其他键 (含小键盘) 卡死"
