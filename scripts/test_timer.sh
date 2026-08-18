#!/usr/bin/env bash
# 无图形界面测试: 运行内核 5 秒, 校验 COM1 每秒输出一个递增计数
set -u

KERNEL=${KERNEL:-myos.bin}
DURATION=${DURATION:-5}
LOG=$(mktemp)

qemu-system-i386 -kernel "$KERNEL" -display none -serial file:"$LOG" -no-reboot &
QEMU_PID=$!
sleep "$DURATION"
kill "$QEMU_PID" 2>/dev/null
wait "$QEMU_PID" 2>/dev/null

echo "--- COM1 output ---"
cat "$LOG"
echo "-------------------"

# 串口输出为 CRLF, 去掉 \r 后再校验
tr -d '\r' < "$LOG" > "$LOG.clean"

TICKS=$(grep -c '^[0-9]\+$' "$LOG.clean")
EXPECTED=$((DURATION - 1))

if [ "$TICKS" -lt "$EXPECTED" ]; then
    echo "FAIL: 期望至少 $EXPECTED 个秒计数, 实际 $TICKS"
    rm -f "$LOG" "$LOG.clean"
    exit 1
fi

SEQ=$(grep '^[0-9]\+$' "$LOG.clean" | head -n "$EXPECTED" | tr '\n' ' ')
WANT=$(seq 1 "$EXPECTED" | tr '\n' ' ')
if [ "$SEQ" != "$WANT" ]; then
    echo "FAIL: 计数序列不是递增的 (期望 '$WANT', 实际 '$SEQ')"
    rm -f "$LOG" "$LOG.clean"
    exit 1
fi

echo "PASS: COM1 每秒输出一个递增计数 ($TICKS 个)"
rm -f "$LOG" "$LOG.clean"
