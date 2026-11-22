# `power.h`

Power control over `SYS_POWER`.

`int power_shutdown(void)` — ACPI S5 power off.
`int power_reboot(void)` — machine reset.

Both calls do not return on success: the kernel performs the ACPI
shutdown or reboot immediately. A negative return means the
operation was refused or is unavailable (e.g. no ACPI table).
