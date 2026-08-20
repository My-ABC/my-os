#ifndef _RTC_H
#define _RTC_H

#include "stdint.h"

// RTC 寄存器地址
#define RTC_SECONDS    0x00
#define RTC_MINUTES    0x02
#define RTC_HOURS      0x04
#define RTC_DAY        0x07
#define RTC_MONTH      0x08
#define RTC_YEAR       0x09
#define RTC_CENTURY    0x32  // 某些RTC支持世纪寄存器

// RTC 端口
#define RTC_PORT       0x70
#define RTC_DATA       0x71

// 时区偏移（秒）
#define TIMEZONE_UTC_OFFSET_BEIJING 8 * 3600  // UTC+8

typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;  // 完整年份，避免千年虫问题
} rtc_time_t;

// 初始化RTC
void rtc_init(void);

// 读取当前时间
void rtc_read_time(rtc_time_t *time);

// 打印日期时间
void rtc_print_datetime(void);

// 时区转换：将时间转换为指定时区
void rtc_convert_timezone(rtc_time_t *time, int offset_seconds);

// 转换为北京时间（UTC+8）
void rtc_to_beijing_time(rtc_time_t *time);

// 计算Unix时间戳（距离1970-01-01 00:00:00 UTC的秒数）
uint32_t rtc_to_unix_timestamp(const rtc_time_t *time);

// 打印Unix时间戳
void rtc_print_unix_timestamp(void);

#endif