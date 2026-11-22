# `power.h`

基于 `SYS_POWER` 的电源控制。

`int power_shutdown(void)` —— ACPI S5 关机。
`int power_reboot(void)` —— 系统复位。

成功时这两个调用都不会返回：内核会立即执行 ACPI
关机或复位。返回负值表示操作被拒绝或不可用
（例如没有 ACPI 表）。
