/* SPDX-License-Identifier: MIT */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

static int fails;
#define CHK(c, n) do { if (c) printf("ok %s\n", n); \
    else { printf("FAIL %s\n", n); fails++; } } while (0)

int main(void) {
    CHK(isalpha('a') && isalpha('Z') && !isalpha('1') && !isalpha(' '), "isalpha");
    CHK(isdigit('0') && isdigit('9') && !isdigit('a'), "isdigit");
    CHK(isalnum('a') && isalnum('5') && !isalnum('-'), "isalnum");
    CHK(isspace(' ') && isspace('\t') && isspace('\n') && !isspace('x'), "isspace");
    CHK(isupper('A') && !isupper('a') && islower('a') && !islower('A'), "case");
    CHK(isxdigit('f') && isxdigit('F') && isxdigit('9') && !isxdigit('g'), "isxdigit");
    CHK(isprint('A') && !isprint('\n') && iscntrl('\n'), "print-cntrl");
    CHK(ispunct(',') && ispunct('!') && !ispunct('a'), "ispunct");
    CHK(isblank(' ') && isblank('\t') && !isblank('\n'), "isblank");
    CHK(isgraph('A') && !isgraph(' '), "isgraph");
    CHK(toupper('a') == 'A' && toupper('A') == 'A' && tolower('Z') == 'z' &&
        tolower('z') == 'z', "toupper-tolower");
    CHK(toupper('1') == '1' && tolower('!') == '!', "toupper-punct");

    printf("DONE-ctype fails=%d\n", fails);
    exit(fails);
}
