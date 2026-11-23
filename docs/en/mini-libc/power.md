# `power.h` — power control

Over `SYS_POWER`. Both calls do not return on success — the kernel
performs the ACPI shutdown (S5) or reboot immediately. A negative
return means the operation was refused or is unavailable (e.g. no
ACPI).

## `int power_shutdown(void)`

- **Parameters**: none.
- **Returns**: does not return on success; `< 0` on failure.
- **Details**: Powers the machine off via ACPI S5
  (`PWR_SHUTDOWN`).

## `int power_reboot(void)`

- **Parameters**: none.
- **Returns**: does not return on success; `< 0` on failure.
- **Details**: Resets the machine (`PWR_REBOOT`).
