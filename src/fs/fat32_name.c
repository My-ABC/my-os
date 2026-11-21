#include "fat32_int.h"
#include "string.h"


int utf16LE_to_ascii(const uint8_t *utf16le, uint32_t input_size_bytes,
                     char *ascii, uint32_t output_size) {
    if (utf16le == 0 || ascii == 0 || output_size == 0U ||
        (input_size_bytes & 1U) != 0U) {
        return -1;
    }

    uint32_t output_length = 0U;
    for (uint32_t offset = 0U; offset < input_size_bytes; offset += 2U) {
        uint16_t code_unit = le16(utf16le + offset);
        if (code_unit == 0U || code_unit == 0xFFFFU) break;
        if (output_length + 1U >= output_size) {
            ascii[output_length] = '\0';
            return -1;
        }
        ascii[output_length++] =
            code_unit <= 0x7FU ? (char)code_unit : '?';
    }
    ascii[output_length] = '\0';
    return (int)output_length;
}


int ascii_to_utf16LE(const char *ascii, uint8_t *utf16le,
                     uint32_t output_size_bytes) {
    if (ascii == 0 || utf16le == 0) return -1;
    uint32_t length = strlen(ascii);
    if (length > FAT32_MAX_LFN_UNITS ||
        (length + 1U) * 2U > output_size_bytes) {
        return -1;
    }
    for (uint32_t i = 0U; i < length; ++i) {
        uint8_t value = (uint8_t)ascii[i];
        if (value > 0x7FU) return -1;
        utf16le[i * 2U] = value;
        utf16le[i * 2U + 1U] = 0U;
    }
    utf16le[length * 2U] = 0U;
    utf16le[length * 2U + 1U] = 0U;
    return (int)length;
}


void format_83(const uint8_t entry[11], char out[13]) {
    int pos = 0;
    for (int i = 0; i < 8; ++i) {
        if (entry[i] == ' ') break;
        out[pos++] = (char)entry[i];
    }
    int extension = 0;
    for (int i = 8; i < 11; ++i) {
        if (entry[i] != ' ') { extension = 1; break; }
    }
    if (extension) {
        out[pos++] = '.';
        for (int i = 8; i < 11; ++i) {
            if (entry[i] == ' ') break;
            out[pos++] = (char)entry[i];
        }
    }
    out[pos] = '\0';
}


static int short_name_character(char c) {
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return 1;
    return strchr("!#$%&'()-@^_`{}~", c) != 0;
}


int name_to_83(const char *name, uint8_t out[11]) {
    if (name == 0 || name[0] == '\0') return -1;
    uint32_t length = strlen(name);
    const char *dot = 0;
    for (uint32_t i = 0U; i < length; ++i) {
        if (name[i] == '.') {
            if (dot != 0) return -1;
            dot = name + i;
        }
    }
    memset(out, ' ', 11U);
    uint32_t base_len = dot == 0 ? strlen(name) : (uint32_t)(dot - name);
    if (base_len == 0U || base_len > 8U) return -1;
    for (uint32_t i = 0; i < base_len; ++i) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (!short_name_character(c)) return -1;
        out[i] = (uint8_t)c;
    }
    if (dot != 0) {
        uint32_t ext_len = strlen(dot + 1);
        if (ext_len == 0U || ext_len > 3U) return -1;
        for (uint32_t i = 0; i < ext_len; ++i) {
            char c = dot[1 + i];
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            if (!short_name_character(c)) return -1;
            out[8U + i] = (uint8_t)c;
        }
    }
    return 0;
}


int valid_lfn_name(const char *name) {
    if (name == 0 || name[0] == '\0') return 0;
    uint32_t length = strlen(name);
    if (length > FAT32_MAX_LFN_UNITS ||
        (length == 1U && name[0] == '.') ||
        (length == 2U && name[0] == '.' && name[1] == '.') ||
        name[length - 1U] == '.' || name[length - 1U] == ' ') {
        return 0;
    }
    for (uint32_t i = 0U; i < length; ++i) {
        uint8_t c = (uint8_t)name[i];
        if (c < 0x20U || c >= 0x7FU ||
            strchr("\"*/:<>?\\|", c) != 0) {
            return 0;
        }
    }
    return 1;
}


uint8_t lfn_checksum(const uint8_t short_name[11]) {
    uint8_t checksum = 0U;
    for (uint32_t i = 0U; i < 11U; ++i) {
        checksum = (uint8_t)(((checksum & 1U) != 0U ? 0x80U : 0U) +
                             (checksum >> 1) + short_name[i]);
    }
    return checksum;
}


void lfn_reset(struct lfn_sequence *sequence) {
    memset(sequence, 0, sizeof(*sequence));
}


void lfn_location_reset(struct lfn_location_sequence *sequence) {
    memset(sequence, 0, sizeof(*sequence));
}


void lfn_add_entry(struct lfn_sequence *sequence,
                          const uint8_t entry[32],
                          const struct dir_entry_location *location) {
    uint8_t order = entry[0] & 0x1FU;
    uint8_t is_last = entry[0] & 0x40U;
    if (is_last != 0U) {
        lfn_reset(sequence);
        if (order == 0U || order > FAT32_MAX_LFN_ENTRIES) return;
        sequence->valid = 1U;
        sequence->total_entries = order;
        sequence->expected_order = order;
        sequence->checksum = entry[13];
    }

    if (sequence->valid == 0U || order != sequence->expected_order ||
        (entry[0] & 0xA0U) != 0U || entry[11] != 0x0FU ||
        entry[12] != 0U || le16(entry + 26) != 0U ||
        entry[13] != sequence->checksum) {
        lfn_reset(sequence);
        return;
    }

    static const uint8_t unit_offsets[FAT32_LFN_UNITS_PER_ENTRY] = {
        1U, 3U, 5U, 7U, 9U, 14U, 16U, 18U, 20U, 22U, 24U, 28U, 30U
    };
    uint32_t base = (uint32_t)(order - 1U) * FAT32_LFN_UNITS_PER_ENTRY;
    for (uint32_t i = 0U; i < FAT32_LFN_UNITS_PER_ENTRY; ++i) {
        sequence->utf16le[(base + i) * 2U] = entry[unit_offsets[i]];
        sequence->utf16le[(base + i) * 2U + 1U] =
            entry[unit_offsets[i] + 1U];
    }
    sequence->locations[order - 1U] = *location;
    --sequence->expected_order;
}


void lfn_location_add_entry(
    struct lfn_location_sequence *sequence, const uint8_t entry[32],
    const struct dir_entry_location *location) {
    uint8_t order = entry[0] & 0x1FU;
    if ((entry[0] & 0x40U) != 0U) {
        lfn_location_reset(sequence);
        if (order == 0U || order > FAT32_MAX_LFN_ENTRIES) return;
        sequence->valid = 1U;
        sequence->total_entries = order;
        sequence->expected_order = order;
        sequence->checksum = entry[13];
    }

    if (sequence->valid == 0U || order != sequence->expected_order ||
        (entry[0] & 0xA0U) != 0U || entry[11] != 0x0FU ||
        entry[12] != 0U || le16(entry + 26) != 0U ||
        entry[13] != sequence->checksum) {
        lfn_location_reset(sequence);
        return;
    }
    sequence->locations[order - 1U] = *location;
    --sequence->expected_order;
}


int lfn_locations_match(const struct lfn_location_sequence *sequence,
                               const uint8_t short_name[11]) {
    return sequence->valid != 0U && sequence->expected_order == 0U &&
           sequence->checksum == lfn_checksum(short_name);
}


int lfn_decode(const struct lfn_sequence *sequence,
                      const uint8_t short_name[11],
                      char *name, uint32_t name_size) {
    if (sequence->valid == 0U || sequence->expected_order != 0U ||
        sequence->checksum != lfn_checksum(short_name)) {
        return 0;
    }
    int length = utf16LE_to_ascii(
        sequence->utf16le,
        (uint32_t)sequence->total_entries * FAT32_LFN_UNITS_PER_ENTRY * 2U,
        name, name_size);
    return length > 0 ? 1 : 0;
}


int prepare_name(const char *name, struct prepared_name *prepared) {
    if (!valid_lfn_name(name)) return -1;
    memset(prepared, 0, sizeof(*prepared));
    int length = ascii_to_utf16LE(name, prepared->utf16le,
                                  sizeof(prepared->utf16le));
    if (length <= 0) return -1;
    prepared->utf16_length = (uint32_t)length;
    prepared->needs_alias = name_to_83(name, prepared->short_name) != 0;
    prepared->lfn_entries =
        (prepared->utf16_length + FAT32_LFN_UNITS_PER_ENTRY - 1U) /
        FAT32_LFN_UNITS_PER_ENTRY;
    return 0;
}


void make_lfn_entry(const struct prepared_name *name, uint32_t order,
                           uint8_t checksum, uint8_t entry[32]) {
    static const uint8_t unit_offsets[FAT32_LFN_UNITS_PER_ENTRY] = {
        1U, 3U, 5U, 7U, 9U, 14U, 16U, 18U, 20U, 22U, 24U, 28U, 30U
    };
    memset(entry, 0xFF, 32U);
    entry[0] = (uint8_t)order;
    if (order == name->lfn_entries) entry[0] |= 0x40U;
    entry[11] = 0x0FU;
    entry[12] = 0U;
    entry[13] = checksum;
    entry[26] = 0U;
    entry[27] = 0U;

    uint32_t base = (order - 1U) * FAT32_LFN_UNITS_PER_ENTRY;
    for (uint32_t i = 0U; i < FAT32_LFN_UNITS_PER_ENTRY; ++i) {
        uint32_t position = base + i;
        uint8_t low = 0xFFU;
        uint8_t high = 0xFFU;
        if (position < name->utf16_length) {
            low = name->utf16le[position * 2U];
            high = name->utf16le[position * 2U + 1U];
        } else if (position == name->utf16_length) {
            low = 0U;
            high = 0U;
        }
        entry[unit_offsets[i]] = low;
        entry[unit_offsets[i] + 1U] = high;
    }
}


int make_short_alias(uint32_t dir_cluster, const char *name,
                            uint8_t short_name[11]) {
    char base[256];
    uint32_t base_length = 0U;
    uint8_t extension[3];
    uint32_t extension_length = 0U;
    const char *last_dot = 0;
    uint32_t name_length = strlen(name);
    for (uint32_t i = 0U; i < name_length; ++i) {
        if (name[i] == '.') last_dot = name + i;
    }
    uint32_t base_end = last_dot != 0
        ? (uint32_t)(last_dot - name) : name_length;
    for (uint32_t i = 0U; i < base_end; ++i) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (short_name_character(c)) base[base_length++] = c;
    }
    if (base_length == 0U) base[base_length++] = '_';
    if (last_dot != 0) {
        for (const char *p = last_dot + 1; *p != '\0' &&
             extension_length < sizeof(extension); ++p) {
            char c = *p;
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            if (short_name_character(c)) {
                extension[extension_length++] = (uint8_t)c;
            }
        }
    }

    for (uint32_t suffix = 1U; suffix < 1000000U; ++suffix) {
        char digits[6];
        uint32_t digit_count = 0U;
        uint32_t value = suffix;
        do {
            digits[digit_count++] = (char)('0' + value % 10U);
            value /= 10U;
        } while (value != 0U);
        uint32_t prefix_length = 8U - digit_count - 1U;
        if (prefix_length > base_length) prefix_length = base_length;

        memset(short_name, ' ', 11U);
        memcpy(short_name, base, prefix_length);
        short_name[prefix_length] = '~';
        for (uint32_t i = 0U; i < digit_count; ++i) {
            short_name[prefix_length + 1U + i] =
                (uint8_t)digits[digit_count - i - 1U];
        }
        memcpy(short_name + 8U, extension, extension_length);
        int found = find_entry_in_cluster_chain(dir_cluster, short_name, 0, 0);
        if (found < 0) return -1;
        if (found == 0) return 0;
    }
    return -1;
}

