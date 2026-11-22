/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <string.h>

int main(void) {
    char buf[128];
    int n = 0x2a;

    /* printf family: flags, width, precision */
    printf("dec=%5d hex=%#06x oct=%#o neg=%-6d|\n", n, n, n, -7);
    printf("str=[%-8s] ptr=%p char=%c pct=%%\n", "ab", (void *)buf, 'Q');
    sprintf(buf, "sprintf-%03d", 42);
    puts(buf);
    snprintf(buf, sizeof buf, "%s!", "snprintf-check");
    printf("%s\n", buf);

    /* sscanf */
    int a = 0, b = 0;
    char word[16];
    sscanf("  -12 0x1f hello", "%d %x %7s", &a, &b, word);
    printf("sscanf: %d %d %s\n", a, b, word);

    puts("stdio-demo-done");
    return 0;
}
