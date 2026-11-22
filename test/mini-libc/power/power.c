/* SPDX-License-Identifier: MIT */
/* power.h smoke test: power_reboot() must reset the machine.
 * Runs in a dedicated QEMU session — the reboot ends it. */
#include <stdio.h>
#include <power.h>

int main(void) {
    printf("power-reboot-start\n");
    fflush(stdout);
    int rc = power_reboot();
    printf("power-reboot-failed rc=%d\n", rc);
    return 1;
}
