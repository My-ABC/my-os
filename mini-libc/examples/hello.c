/* SPDX-License-Identifier: MIT */
#include <stdio.h>

int main(int argc, char **argv) {
    printf("mini-libc hello from Ring 3\n");
    printf("argc=%d argv0=%s\n", argc, argv[0]);
    return 0;
}
