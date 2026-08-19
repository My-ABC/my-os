#include "rtc.h"
#include "io.h"
#include "vga.h"
#include "serial.h"

// 从CMOS读取一个字节
static uint8_t rtc_read_register(uint8_t reg) {
    outb(RTC_PORT, reg);
    return inb(RTC_DATA);
}

// 等待RTC准备好接受下一个读取
static void rtc_wait_ready(void) {
    outb(RTC_PORT, 0x0A);  // 状态寄存器A
    while (inb(RTC_DATA) & 0x80) {
        // UIP位（更新进行中）为1时等待
    }
}

// BCD码转十进制
static uint8_t bcd_to_dec(uint8_t bcd) {
    return (bcd >> 4) * 10 + (bcd & 0x0F);
}

// 初始化RTC
void rtc_init(void) {
    // 这里可以添加RTC初始化代码
    // 现代CMOS RTC通常不需要特殊初始化
}

// 读取当前时间
void rtc_read_time(rtc_time_t *time) {
    uint8_t century = 0;
    uint8_t status_b;
    
    // 读取状态寄存器B以确定数据格式
    outb(RTC_PORT, 0x0B);
    status_b = inb(RTC_DATA);
    
    // 检查是否使用BCD格式（位2=0表示BCD格式）
    int is_bcd = !(status_b & 0x04);
    
    // 检查是否使用24小时制（位1=0表示24小时制）
    int is_24h = !(status_b & 0x02);
    
    // 尝试读取世纪寄存器（如果存在）
    century = rtc_read_register(RTC_CENTURY);
    
    // 读取时间寄存器（多次读取以确保一致性）
    do {
        rtc_wait_ready();
        
        time->second = rtc_read_register(RTC_SECONDS);
        time->minute = rtc_read_register(RTC_MINUTES);
        time->hour = rtc_read_register(RTC_HOURS);
        time->day = rtc_read_register(RTC_DAY);
        time->month = rtc_read_register(RTC_MONTH);
        time->year = rtc_read_register(RTC_YEAR);
        
        // 再次读取秒，如果相等则说明读取是稳定的
    } while (time->second != rtc_read_register(RTC_SECONDS));
    
    // 转换BCD码为十进制
    if (is_bcd) {
        time->second = bcd_to_dec(time->second);
        time->minute = bcd_to_dec(time->minute);
        time->hour = bcd_to_dec(time->hour);
        time->day = bcd_to_dec(time->day);
        time->month = bcd_to_dec(time->month);
        time->year = bcd_to_dec(time->year);
        if (century) {
            century = bcd_to_dec(century);
        }
    }
    
    // 处理12小时制
    if (!is_24h) {
        // 如果是12小时制，需要处理AM/PM
        if (time->hour & 0x80) {
            // PM位
            time->hour = ((time->hour & 0x7F) + 12) % 24;
        } else {
            time->hour = time->hour % 12;
        }
    }
    
    // 构建完整年份，避免千年虫问题
    if (century) {
        // 如果有世纪寄存器，使用它
        time->year = century * 100 + time->year;
    } else {
        // 如果没有世纪寄存器，需要推断世纪
        // 这里使用一个启发式方法：假设年份 >= 90 则为1900年代，否则为2000年代
        // 注意：这不是完美的解决方案，但避免了简单的千年虫问题
        if (time->year >= 90) {
            time->year = 1900 + time->year;
        } else {
            time->year = 2000 + time->year;
        }
    }
    
    // 确保年份在合理范围内
    if (time->year < 2000) {
        time->year = 2000 + (time->year % 100);
    }
}

// 打印日期时间
void rtc_print_datetime(void) {
    rtc_time_t time;
    rtc_read_time(&time);
    
    // 打印日期: YYYY-MM-DD
    vga_print_dec(time.year);
    vga_putchar('-');
    if (time.month < 10) vga_putchar('0');
    vga_print_dec(time.month);
    vga_putchar('-');
    if (time.day < 10) vga_putchar('0');
    vga_print_dec(time.day);
    vga_putchar(' ');
    
    // 打印时间: HH:MM:SS
    if (time.hour < 10) vga_putchar('0');
    vga_print_dec(time.hour);
    vga_putchar(':');
    if (time.minute < 10) vga_putchar('0');
    vga_print_dec(time.minute);
    vga_putchar(':');
    if (time.second < 10) vga_putchar('0');
    vga_print_dec(time.second);
    vga_putchar('\n');
    
    // 同样输出到串口
    serial_print_dec(time.year);
    serial_putchar('-');
    if (time.month < 10) serial_putchar('0');
    serial_print_dec(time.month);
    serial_putchar('-');
    if (time.day < 10) serial_putchar('0');
    serial_print_dec(time.day);
    serial_putchar(' ');
    
    if (time.hour < 10) serial_putchar('0');
    serial_print_dec(time.hour);
    serial_putchar(':');
    if (time.minute < 10) serial_putchar('0');
    serial_print_dec(time.minute);
    serial_putchar(':');
    if (time.second < 10) serial_putchar('0');
    serial_print_dec(time.second);
    serial_putchar('\n');
}

// 判断是否为闰年
static int is_leap_year(uint16_t year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

// 获取某个月的天数
static int days_in_month(uint16_t year, uint8_t month) {
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    return days[month - 1];
}

// 添加天数到日期（处理跨月、跨年）
static void add_days(rtc_time_t *time, int days) {
    while (days > 0) {
        int dim = days_in_month(time->year, time->month);
        int remaining = dim - time->day + 1;
        
        if (days >= remaining) {
            days -= remaining;
            time->day = 1;
            time->month++;
            if (time->month > 12) {
                time->month = 1;
                time->year++;
            }
        } else {
            time->day += days;
            days = 0;
        }
    }
}

// 时区转换：将时间转换为指定时区
void rtc_convert_timezone(rtc_time_t *time, int offset_seconds) {
    // 计算偏移的小时和分钟
    int offset_hours = offset_seconds / 3600;
    int offset_minutes = (offset_seconds % 3600) / 60;
    
    // 应用偏移
    time->hour += offset_hours;
    time->minute += offset_minutes;
    
    // 处理分钟溢出
    while (time->minute >= 60) {
        time->minute -= 60;
        time->hour++;
    }
    while (time->minute < 0) {
        time->minute += 60;
        time->hour--;
    }
    
    // 处理小时溢出
    while (time->hour >= 24) {
        time->hour -= 24;
        add_days(time, 1);
    }
    while (time->hour < 0) {
        time->hour += 24;
        // 处理日期减1（这里简化处理，假设不会跨月）
        time->day--;
        if (time->day < 1) {
            time->month--;
            if (time->month < 1) {
                time->month = 12;
                time->year--;
            }
            time->day = days_in_month(time->year, time->month);
        }
    }
}

// 转换为北京时间（UTC+8）
void rtc_to_beijing_time(rtc_time_t *time) {
    rtc_convert_timezone(time, TIMEZONE_UTC_OFFSET_BEIJING);
}

// 计算Unix时间戳（距离1970-01-01 00:00:00 UTC的秒数）
uint32_t rtc_to_unix_timestamp(const rtc_time_t *time) {
    uint32_t timestamp = 0;
    uint16_t year;
    uint8_t month;
    
    // 计算从1970年到time->year的秒数
    for (year = 1970; year < time->year; year++) {
        timestamp += is_leap_year(year) ? 366 * 24 * 3600 : 365 * 24 * 3600;
    }
    
    // 计算当年1月到time->month的秒数
    for (month = 1; month < time->month; month++) {
        timestamp += days_in_month(time->year, month) * 24 * 3600;
    }
    
    // 计算当月1日到time->day的秒数
    timestamp += (time->day - 1) * 24 * 3600;
    
    // 计算当天的秒数
    timestamp += time->hour * 3600;
    timestamp += time->minute * 60;
    timestamp += time->second;
    
    return timestamp;
}

// 打印Unix时间戳
void rtc_print_unix_timestamp(void) {
    rtc_time_t time;
    rtc_read_time(&time);
    
    uint32_t timestamp = rtc_to_unix_timestamp(&time);
    
    vga_print("Unix timestamp: ");
    vga_print_dec(timestamp);
    vga_putchar('\n');
    
    serial_print("Unix timestamp: ");
    serial_print_dec(timestamp);
    serial_putchar('\n');
}