#include "string.h"
#include "test_support.h"

int main(void) {
    char buffer[32];
    char overlap[8] = "abcdef";
    unsigned char bytes[4];

    check(strcmp("same", "same") == 0, "strcmp equal");
    check(strcmp("abc", "abd") < 0 && strcmp("abd", "abc") > 0,
          "strcmp ordering");
    check(strlen("") == 0U && strlen("kernel") == 6U, "strlen");
    check(strchr("abc", 'b') != 0 && *strchr("abc", 'b') == 'b' &&
          strchr("abc", 'z') == 0 && strchr("abc", '\0') != 0, "strchr");
    check(strcpy(buffer, "My") == buffer && strcat(buffer, "OS") == buffer &&
          strcmp(buffer, "MyOS") == 0, "strcpy/strcat");
    check(memcmp("abc", "abc", 3U) == 0 && memcmp("abc", "abd", 3U) < 0,
          "memcmp");
    check(memcpy(bytes, "xyz", 4U) == bytes && bytes[0] == 'x' &&
          bytes[2] == 'z' && bytes[3] == '\0', "memcpy");
    check(memset(bytes, 0xA5, sizeof(bytes)) == bytes &&
          bytes[0] == 0xA5 && bytes[3] == 0xA5, "memset");
    check(memmove(overlap + 1, overlap, 6U) == overlap + 1 &&
          memcmp(overlap, "aabcdef", 7U) == 0, "memmove forward overlap");
    check(memmove(overlap, overlap + 1, 6U) == overlap &&
          memcmp(overlap, "abcdef", 6U) == 0, "memmove backward overlap");

    return finish_test_suite("String tests");
}
