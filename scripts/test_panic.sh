#!/usr/bin/env bash
# 无图形界面测试 INT3 蓝屏: 校验寄存器 dump 输出到 COM1, 并在倒计时后 ACPI 自动重启
# MAKE_ARGS 可传给 make (例如没有交叉工具链时: MAKE_ARGS="CC=gcc LD=ld")
# SCREENSHOT=<path> 时通过 QEMU monitor 抓一张蓝屏截图
set -u

KERNEL=${KERNEL:-myos.bin}
DURATION=${DURATION:-14}
SCREENSHOT=${SCREENSHOT:-}
LOG=$(mktemp)
MONITOR=$(mktemp -u)

make clean >/dev/null
# shellcheck disable=SC2086
make PANIC_DEMO=1 ${MAKE_ARGS:-} build >/dev/null || { echo "FAIL: 构建失败"; exit 1; }

qemu-system-i386 -kernel "$KERNEL" -display none -serial file:"$LOG" \
    -monitor "unix:$MONITOR,server,nowait" &
QEMU_PID=$!

if [ -n "$SCREENSHOT" ]; then
    sleep 3  # 蓝屏倒计时期间抓图
    printf 'screendump %s\n' "$SCREENSHOT" | timeout 5 socat - "UNIX-CONNECT:$MONITOR" >/dev/null 2>&1
fi

sleep "$DURATION"
kill "$QEMU_PID" 2>/dev/null
wait "$QEMU_PID" 2>/dev/null

tr -d '\r' < "$LOG" > "$LOG.clean"
echo "--- COM1 output ---"
cat "$LOG.clean"
echo "-------------------"

fail() {
    echo "FAIL: $1"
    rm -f "$LOG" "$LOG.clean" "$MONITOR"
    exit 1
}

grep -q 'STOP: KERNEL PANIC' "$LOG.clean" || fail "没有看到蓝屏 panic 信息"
grep -q '^EIP=0x' "$LOG.clean" || fail "没有看到 EIP 寄存器输出"
for REG in EAX EBX ECX EDX ESI EDI EBP ESP CS DS EFL; do
    grep -q "^$REG *=0x" "$LOG.clean" || fail "缺少寄存器 $REG 的输出"
done

BOOTS=$(grep -c 'MyOS v0.1 serial console' "$LOG.clean")
[ "$BOOTS" -ge 2 ] || fail "内核没有自动重启 (只启动了 $BOOTS 次)"

# q35 的固件提供 ACPI 2.0+ FADT, 校验确实走的是 reset register 而不是兜底路径
# (默认 pc 机型的 SeaBIOS 只有 ACPI 1.0 FADT, 没有 reset register)
Q35_LOG=$(mktemp)
timeout 15 qemu-system-i386 -machine q35 -kernel "$KERNEL" -display none \
    -serial file:"$Q35_LOG" -no-reboot >/dev/null 2>&1
tr -d '\r' < "$Q35_LOG" > "$Q35_LOG.clean"
echo "--- q35 ACPI 复位路径 ---"
grep '\[ACPI\]' "$Q35_LOG.clean"
echo "----------------------"
grep -q '\[ACPI\] reset via FADT' "$Q35_LOG.clean" || fail "q35 下没有使用 FADT reset register"
grep -q 'falling back' "$Q35_LOG.clean" && fail "q35 下 ACPI 复位无效, 退到了兜底路径"
rm -f "$Q35_LOG" "$Q35_LOG.clean"

echo "PASS: INT3 触发蓝屏并 dump 寄存器, 复位后重新启动 ($BOOTS 次启动), q35 下走 ACPI reset register"
rm -f "$LOG" "$LOG.clean" "$MONITOR"
