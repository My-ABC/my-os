# `power.h` — 电源控制

基于 `SYS_POWER`。两个调用成功时都不返回——内核立即执行 ACPI
关机（S5）或重启。返回负数表示操作被拒绝或不可用（例如没有
ACPI）。

## `int power_shutdown(void)`

- **参数**：无。
- **返回值**：成功不返回；失败 `< 0`。
- **详细说明**：经 ACPI S5 关机（`PWR_SHUTDOWN`）。

## `int power_reboot(void)`

- **参数**：无。
- **返回值**：成功不返回；失败 `< 0`。
- **详细说明**：重启机器（`PWR_REBOOT`）。
