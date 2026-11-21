#include "test_support.h"

#include "../../src/fs/fat32.c"
#include "../../src/lib/string.c"

int main(void) {
    const char *filename = "help_aaaaa.1234";
    uint8_t encoded[64];
    char decoded[32];
    int length = ascii_to_utf16LE(filename, encoded, sizeof(encoded));
    check(length == 15, "ASCII filename converts to UTF-16LE code units");
    check(encoded[0] == 'h' && encoded[1] == 0U &&
              encoded[28] == '4' && encoded[29] == 0U &&
              encoded[30] == 0U && encoded[31] == 0U,
          "UTF-16LE conversion stores little-endian units and terminator");
    check(utf16LE_to_ascii(encoded, 32U, decoded, sizeof(decoded)) == length &&
              strcmp(decoded, filename) == 0,
          "UTF-16LE filename converts back to ASCII");

    const uint8_t unicode_name[] = {'A', 0U, 0x2DU, 0x4EU, 0U, 0U};
    check(utf16LE_to_ascii(unicode_name, sizeof(unicode_name), decoded,
                           sizeof(decoded)) == 2 &&
              strcmp(decoded, "A?") == 0,
          "non-ASCII UTF-16 code units use the ASCII replacement character");
    check(ascii_to_utf16LE("\xC3\xA9", encoded, sizeof(encoded)) == -1,
          "ASCII-to-UTF-16LE rejects non-ASCII input bytes");
    check(utf16LE_to_ascii(encoded, 1U, decoded, sizeof(decoded)) == -1,
          "UTF-16LE-to-ASCII rejects an odd input byte count");
    check(utf16LE_to_ascii(encoded, 32U, decoded, 4U) == -1,
          "UTF-16LE-to-ASCII rejects an undersized output buffer");

    return finish_test_suite("FAT32 LFN conversion tests");
}
