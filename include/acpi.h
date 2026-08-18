#ifndef _ACPI_H
#define _ACPI_H

// 通过 ACPI FADT 的 reset register 复位; 失败时回退到 0xCF9 / 键盘控制器 / 三重错误
void acpi_reboot(void);

#endif
