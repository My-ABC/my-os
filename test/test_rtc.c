#include "rtc.h"
#include "test_support.h"

int main(void) {
    rtc_time_t time;

    time.year = 1970;
    time.month = 1;
    time.day = 1;
    time.hour = 0;
    time.minute = 0;
    time.second = 0;
    check(rtc_to_unix_timestamp(&time) == 0U, "Unix epoch");

    time.year = 2000;
    time.month = 3;
    time.day = 1;
    time.hour = 0;
    time.minute = 0;
    time.second = 0;
    check(rtc_to_unix_timestamp(&time) == 951868800U,
          "leap-century timestamp");

    time.year = 2024;
    time.month = 2;
    time.day = 29;
    time.hour = 12;
    time.minute = 34;
    time.second = 56;
    check(rtc_to_unix_timestamp(&time) == 1709210096U,
          "leap-day timestamp");

    time.year = 2023;
    time.month = 12;
    time.day = 31;
    time.hour = 23;
    time.minute = 30;
    time.second = 0;
    rtc_convert_timezone(&time, 3600);
    check(time.year == 2024 && time.month == 1 && time.day == 1 &&
          time.hour == 0 && time.minute == 30, "positive offset crosses year");

    time.year = 2024;
    time.month = 3;
    time.day = 1;
    time.hour = 0;
    time.minute = 30;
    time.second = 0;
    rtc_convert_timezone(&time, -3600);
    check(time.year == 2024 && time.month == 2 && time.day == 29 &&
          time.hour == 23 && time.minute == 30,
          "negative offset crosses leap day");

    return finish_test_suite("RTC tests");
}
