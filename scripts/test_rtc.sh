#!/usr/bin/env bash
# 测试RTC: 读取并显示当前时间，验证年份处理是否正确
# MAKE_ARGS 可传给 make (例如没有交叉工具链时: MAKE_ARGS="CC=gcc LD=ld")
set -u

KERNEL=${KERNEL:-myos.bin}
QEMU=${QEMU:-qemu-system-i386}

fail() {
    echo "FAIL: $1"
    exit 1
}

command -v "$QEMU" >/dev/null || fail "找不到 $QEMU, 请安装 qemu-system-x86"

make clean >/dev/null
# shellcheck disable=SC2086
make ${MAKE_ARGS:-} build >/dev/null || fail "构建失败"

# 启动内核，等待初始化完成后读取串口输出
dir=$(mktemp -d)
log=$dir/serial.log

"$QEMU" -kernel "$KERNEL" -display none -serial file:"$log" \
    -nographic >/dev/null 2>&1 &
pid=$!

# 等待内核初始化并显示时间
sleep 5

# 终止QEMU
kill "$pid" 2>/dev/null
wait "$pid" 2>/dev/null

# 清理串口输出
tr -d '\r' < "$log" > "$log.clean"

echo "=== 串口输出 ==="
cat "$log.clean"
echo "================"

# 检查是否包含时间信息（查找日期时间格式）
if ! grep -qE "[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}" "$log.clean"; then
    fail "没有找到时间输出"
fi

# 获取包含时间的行
TIME_LINE=$(grep -E "[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}" "$log.clean")
if [ -z "$TIME_LINE" ]; then
    fail "无法找到时间行"
fi

# 检查年份格式（应该是4位数）
if ! echo "$TIME_LINE" | grep -qE "20[0-9]{2}-[0-9]{2}-[0-9]{2}"; then
    fail "年份格式不正确，期望格式: YYYY-MM-DD"
fi

# 检查时间格式
if ! echo "$TIME_LINE" | grep -qE "[0-9]{2}:[0-9]{2}:[0-9]{2}"; then
    fail "时间格式不正确，期望格式: HH:MM:SS"
fi

# 检查是否避免了千年虫问题（年份不应该出现在2000以下）
YEAR=$(echo "$TIME_LINE" | grep -oE "[0-9]{4}" | head -1)
if [ -z "$YEAR" ] || [ "$YEAR" -lt 2000 ]; then
    fail "年份处理可能存在千年虫问题，年份: $YEAR"
fi

# 检查是否包含北京时间
if ! grep -q "Beijing time" "$log.clean"; then
    fail "没有找到北京时间输出"
fi

# 检查是否包含Unix时间戳
if ! grep -q "Unix timestamp" "$log.clean"; then
    fail "没有找到Unix时间戳输出"
fi

# 检查Unix时间戳是否为有效数字
UNIX_TS=$(grep "Unix timestamp" "$log.clean" | grep -oE "[0-9]+")
if [ -z "$UNIX_TS" ]; then
    fail "Unix时间戳格式不正确"
fi

# 验证Unix时间戳是否合理（应该大于1000000000，即2001年以后）
if [ "$UNIX_TS" -lt 1000000000 ]; then
    fail "Unix时间戳值不合理: $UNIX_TS"
fi

echo "PASS: RTC功能正常，时间格式正确，年份为4位数，北京时间和Unix时间戳显示正常"
rm -rf "$dir"
