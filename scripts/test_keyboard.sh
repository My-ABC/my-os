#!/usr/bin/env bash
# 无图形界面测试键盘 IRQ1: 通过 QEMU monitor sendkey 注入按键
#   1) 按 'b' -> 进入蓝屏 panic
#   2) 按 'a' -> 打印 Halted 后卡死 (不蓝屏)
#   3) 按小键盘 5 -> 识别为 '5' 后卡死
# MAKE_ARGS 可传给 make (例如没有交叉工具链时: MAKE_ARGS="CC=gcc LD=ld")
# SCREENSHOT_DIR=<dir> 时通过 QEMU monitor 抓 VGA 截图
set -u

KERNEL=${KERNEL:-myos.bin}
SCREENSHOT_DIR=${SCREENSHOT_DIR:-}

make clean >/dev/null
# shellcheck disable=SC2086
make ${MAKE_ARGS:-} build >/dev/null || { echo "FAIL: 构建失败"; exit 1; }

fail() {
    echo "FAIL: $1"
    exit 1
}

# 启动内核, 等它进入等待按键状态后注入一个键, 返回清理过的 COM1 日志路径
run_with_key() {
    local key=$1 duration=$2
    local log monitor
    log=$(mktemp)
    monitor=$(mktemp -u)

    qemu-system-i386 -kernel "$KERNEL" -display none -serial file:"$log" \
        -monitor "unix:$monitor,server,nowait" &
    local pid=$!

    sleep 3  # 等内核初始化完并开始等待按键
    if [ -n "$SCREENSHOT_DIR" ]; then
        printf 'screendump %s\n' "$SCREENSHOT_DIR/prompt.ppm" |
            timeout 5 socat - "UNIX-CONNECT:$monitor" >/dev/null 2>&1
    fi
    printf 'sendkey %s\n' "$key" | timeout 5 socat - "UNIX-CONNECT:$monitor" >/dev/null 2>&1

    if [ -n "$SCREENSHOT_DIR" ]; then
        sleep 2
        printf 'screendump %s\n' "$SCREENSHOT_DIR/key-$key.ppm" |
            timeout 5 socat - "UNIX-CONNECT:$monitor" >/dev/null 2>&1
    fi

    sleep "$duration"
    kill "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null

    tr -d '\r' < "$log" > "$log.clean"
    rm -f "$log" "$monitor"
    echo "$log.clean"
}

echo "=== 按 'b': 期望蓝屏 ==="
B_LOG=$(run_with_key b 10)
cat "$B_LOG"
echo "======================="
grep -q '^Key pressed: b$' "$B_LOG" || fail "IRQ1 没有收到按键 'b'"
grep -q 'STOP: KERNEL PANIC' "$B_LOG" || fail "按 'b' 没有进入蓝屏"
grep -q '^EIP=0x' "$B_LOG" || fail "蓝屏没有 dump 寄存器"
rm -f "$B_LOG"

echo "=== 按 'a': 期望卡死 ==="
A_LOG=$(run_with_key a 4)
cat "$A_LOG"
echo "======================="
grep -q '^Key pressed: a$' "$A_LOG" || fail "IRQ1 没有收到按键 'a'"
grep -q '^Halted$' "$A_LOG" || fail "按 'a' 之后没有进入 halt 分支"
grep -q 'STOP: KERNEL PANIC' "$A_LOG" && fail "按 'a' 不应该蓝屏"
rm -f "$A_LOG"

echo "=== 小键盘 5: 期望识别为 '5' ==="
KP_LOG=$(run_with_key kp_5 4)
cat "$KP_LOG"
echo "==============================="
grep -q '^Key pressed: 5$' "$KP_LOG" || fail "小键盘 5 没有被识别"
grep -q '^Halted$' "$KP_LOG" || fail "小键盘按键之后没有进入 halt 分支"
rm -f "$KP_LOG"

echo "PASS: IRQ1 键盘可用, 'b' 触发蓝屏, 其他键 (含小键盘) 卡死"
