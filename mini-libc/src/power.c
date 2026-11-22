/* SPDX-License-Identifier: MIT */
#include <power.h>
#include <sys/syscall.h>

int power_shutdown(void) {
    return syscall0(SYS_POWER, PWR_SHUTDOWN) == 0 ? 0 : -1;
}

int power_reboot(void) {
    return syscall0(SYS_POWER, PWR_REBOOT) == 0 ? 0 : -1;
}
