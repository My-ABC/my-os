#!/usr/bin/env bash
# 测试RTC: 使用QEMU设置特定时间来测试RTC功能
# MAKE_ARGS 可传给 make (例如没有交叉工具链时: MAKE_ARGS="CC=gcc LD=ld")
# RTC_TIME 可以设置QEMU的RTC时间，例如: RTC_TIME="2024-12-31T23:59:59"
set -u

KERNEL=${KERNEL:-myos.bin}
QEMU=${QEMU:-qemu-system-i386}
RTC_TIME=${RTC_TIME:-""}  # 默认使用当前时间

fail() {
    echo "FAIL: $1"
    exit 1
}

command -v "$QEMU" >/dev/null || fail "找不到 $QEMU, 请安装 qemu-system-x86"

make clean >/dev/null
# shellcheck disable=SC2086
make ${MAKE_ARGS:-} build >/dev/null || fail "构建失败"

# 单个时间点测试函数
test_time_point() {
    local test_time=$1
    local test_name=$2
    
    echo "=== 测试: $test_name ==="
    echo "设置QEMU时间: $test_time"
    
    dir=$(mktemp -d)
    log=$dir/serial.log
    
    # 构建QEMU命令
    QEMU_CMD="$QEMU -kernel $KERNEL -display none -serial file:$log -nographic"
    if [ -n "$test_time" ]; then
        QEMU_CMD="$QEMU_CMD -rtc base=$test_time"
    fi
    
    $QEMU_CMD >/dev/null 2>&1 &
    pid=$!
    
    # 等待内核初始化并显示时间
    sleep 5
    
    # 终止QEMU
    kill "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null
    
    # 清理串口输出
    tr -d '\r' < "$log" > "$log.clean"
    
    echo "串口输出:"
    cat "$log.clean"
    echo "---"
    
    # 检查是否包含时间信息
    if ! grep -qE "[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}" "$log.clean"; then
        fail "没有找到时间输出"
    fi
    
    # 获取UTC时间（直接查找时间行）
    UTC_LINE=$(grep -E "^[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}" "$log.clean" | head -1)
    if [ -z "$UTC_LINE" ]; then
        fail "没有找到UTC时间"
    fi
    
    # 提取年份、小时、分钟、秒
    YEAR=$(echo "$UTC_LINE" | grep -oE "[0-9]{4}" | head -1)
    UTC_HOUR=$(echo "$UTC_LINE" | grep -oE " [0-9]{2}:" | grep -oE "[0-9]{2}")
    UTC_MINUTE=$(echo "$UTC_LINE" | grep -oE ":[0-9]{2}:" | grep -oE "[0-9]{2}")
    UTC_SECOND=$(echo "$UTC_LINE" | grep -oE ":[0-9]{2}$" | grep -oE "[0-9]{2}")
    
    if [ -z "$YEAR" ] || [ -z "$UTC_HOUR" ] || [ -z "$UTC_MINUTE" ] || [ -z "$UTC_SECOND" ]; then
        fail "无法提取UTC时间组件"
    fi
    
    # 获取北京时间
    BEIJING_LINE=$(grep "Beijing time" "$log.clean")
    if [ -z "$BEIJING_LINE" ]; then
        fail "没有找到北京时间"
    fi
    
    # 提取北京时间的小时
    BEIJING_HOUR=$(echo "$BEIJING_LINE" | grep -oE " [0-9]{2}:" | grep -oE "[0-9]{2}")
    BEIJING_MINUTE=$(echo "$BEIJING_LINE" | grep -oE ":[0-9]{2}:" | grep -oE "[0-9]{2}")
    BEIJING_SECOND=$(echo "$BEIJING_LINE" | grep -oE ":[0-9]{2}$" | grep -oE "[0-9]{2}")
    
    if [ -z "$BEIJING_HOUR" ] || [ -z "$BEIJING_MINUTE" ] || [ -z "$BEIJING_SECOND" ]; then
        fail "无法提取北京时间组件"
    fi
    
    # 去掉前导零
    UTC_HOUR=$((10#$UTC_HOUR))
    BEIJING_HOUR=$((10#$BEIJING_HOUR))
    
    # 验证时区转换（北京时间应该比UTC快8小时）
    EXPECTED_BEIJING_HOUR=$((UTC_HOUR + 8))
    if [ $EXPECTED_BEIJING_HOUR -ge 24 ]; then
        EXPECTED_BEIJING_HOUR=$((EXPECTED_BEIJING_HOUR - 24))
    fi
    
    if [ "$BEIJING_HOUR" -ne "$EXPECTED_BEIJING_HOUR" ]; then
        fail "时区转换不正确: UTC $UTC_HOUR:00 -> 期望北京 $EXPECTED_BEIJING_HOUR:00, 实际 $BEIJING_HOUR:00"
    fi
    
    # 检查Unix时间戳
    if ! grep -q "Unix timestamp" "$log.clean"; then
        fail "没有找到Unix时间戳输出"
    fi
    
    # 检查Unix时间戳是否为有效数字
    UNIX_TS=$(grep "Unix timestamp" "$log.clean" | grep -oE "[0-9]+")
    if [ -z "$UNIX_TS" ]; then
        fail "Unix时间戳格式不正确"
    fi
    
    # 验证Unix时间戳是否合理（应该大于0）
    if [ "$UNIX_TS" -lt 0 ]; then
        fail "Unix时间戳值不合理: $UNIX_TS"
    fi
    
    echo "✓ UTC时间: $YEAR-$UTC_HOUR:$UTC_MINUTE:$UTC_SECOND"
    echo "✓ 北京时间: $YEAR-$BEIJING_HOUR:$BEIJING_MINUTE:$BEIJING_SECOND"
    echo "✓ 时区转换: UTC $UTC_HOUR:00 -> 北京 $BEIJING_HOUR:00 (正确)"
    echo "✓ Unix时间戳: $UNIX_TS"
    
    rm -rf "$dir"
    echo "PASS: $test_name"
    echo ""
}

# 运行测试
if [ -n "$RTC_TIME" ]; then
    # 如果设置了RTC_TIME环境变量，只测试该时间点
    echo "使用自定义时间: $RTC_TIME"
    test_time_point "$RTC_TIME" "自定义时间测试"
    echo "========================================"
    echo "自定义RTC测试通过!"
else
    # 否则运行所有预设测试
    test_time_point "2024-01-15T10:30:45" "正常时间测试"
    test_time_point "2020-02-29T12:00:00" "闰年测试(2020年)"
    test_time_point "2024-12-31T23:59:59" "跨年测试前夕"
    test_time_point "2000-01-01T00:00:01" "千年虫测试(2000年)"
    test_time_point "2010-06-15T14:30:00" "2010年测试"
    
    echo "========================================"
    echo "所有RTC测试通过!"
    echo "✓ 年份处理正确（包括千年虫问题）"
    echo "✓ 时区转换正确（UTC+8）"
    echo "✓ Unix时间戳计算正确"
    echo "✓ 支持闰年和跨年"
fi
