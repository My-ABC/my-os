#include "fat32.h"
#include "ata.h"
#include "serial.h"
#include "string.h"
#include "heap.h"

#define FAT32_BAD_CLUSTER 0x0FFFFFF7U
#define FAT32_END_CLUSTER 0x0FFFFFF8U
#define FAT32_CLUSTER_FREE 0x00000000U
#define FAT32_ATTR_DIRECTORY 0x10U
#define FAT32_ATTR_VOLUME_ID 0x08U
#define FAT32_MAX_DELETE_DEPTH 32U
#define FAT32_MAX_LFN_UNITS 255U
#define FAT32_LFN_UNITS_PER_ENTRY 13U
#define FAT32_MAX_LFN_ENTRIES 20U
#define FAT32_LFN_BUFFER_UNITS (FAT32_MAX_LFN_ENTRIES * FAT32_LFN_UNITS_PER_ENTRY)
#define FAT32_LFN_BUFFER_BYTES (FAT32_LFN_BUFFER_UNITS * 2U)
#define FAT32_MAX_DIRECTORY_SLOTS (FAT32_MAX_LFN_ENTRIES + 1U)

static uint8_t g_sector[512];
static uint8_t g_cluster[32768];
static struct {
    uint32_t partition_lba;
    uint32_t fat_start_lba;
    uint32_t first_data_lba;
    uint32_t sectors_per_fat;
    uint32_t sectors_per_cluster;
    uint32_t cluster_size;
    uint32_t root_cluster;
    uint32_t total_sectors;
    uint8_t mounted;
} g_fs;

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

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

struct dir_entry_location {
    uint32_t cluster;
    uint32_t index;
};

struct lfn_sequence {
    uint8_t utf16le[FAT32_LFN_BUFFER_BYTES];
    struct dir_entry_location locations[FAT32_MAX_LFN_ENTRIES];
    uint8_t checksum;
    uint8_t total_entries;
    uint8_t expected_order;
    uint8_t valid;
};

struct lfn_location_sequence {
    struct dir_entry_location locations[FAT32_MAX_LFN_ENTRIES];
    uint8_t checksum;
    uint8_t total_entries;
    uint8_t expected_order;
    uint8_t valid;
};

static void lfn_reset(struct lfn_sequence *sequence) {
    memset(sequence, 0, sizeof(*sequence));
}

static void lfn_location_reset(struct lfn_location_sequence *sequence) {
    memset(sequence, 0, sizeof(*sequence));
}

static uint8_t lfn_checksum(const uint8_t short_name[11]) {
    uint8_t checksum = 0U;
    for (uint32_t i = 0U; i < 11U; ++i) {
        checksum = (uint8_t)(((checksum & 1U) != 0U ? 0x80U : 0U) +
                             (checksum >> 1) + short_name[i]);
    }
    return checksum;
}

static void lfn_add_entry(struct lfn_sequence *sequence,
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

static void lfn_location_add_entry(
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

static int lfn_locations_match(const struct lfn_location_sequence *sequence,
                               const uint8_t short_name[11]) {
    return sequence->valid != 0U && sequence->expected_order == 0U &&
           sequence->checksum == lfn_checksum(short_name);
}

static int lfn_decode(const struct lfn_sequence *sequence,
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

static uint32_t fat32_cluster_limit(void) {
    if (g_fs.sectors_per_cluster == 0U ||
        g_fs.first_data_lba < g_fs.partition_lba) {
        return 2U;
    }

    uint32_t data_start = g_fs.first_data_lba - g_fs.partition_lba;
    if (g_fs.total_sectors <= data_start) return 2U;
    uint32_t cluster_count =
        (g_fs.total_sectors - data_start) / g_fs.sectors_per_cluster;
    return cluster_count > 0xFFFFFFFDU ? 0xFFFFFFFFU : cluster_count + 2U;
}

static int read_sector(uint32_t lba) {
    return ata_read_sectors(lba, 1U, g_sector);
}

static int read_cluster(uint32_t cluster) {
    if (cluster < 2U || cluster >= fat32_cluster_limit()) return -1;
    uint32_t lba = g_fs.first_data_lba +
                   (cluster - 2U) * g_fs.sectors_per_cluster;
    return ata_read_sectors(lba, g_fs.sectors_per_cluster, g_cluster);
}

static int write_cluster(uint32_t cluster) {
    if (cluster < 2U || cluster >= fat32_cluster_limit()) return -1;
    uint32_t lba = g_fs.first_data_lba +
                   (cluster - 2U) * g_fs.sectors_per_cluster;
    return ata_write_sectors(lba, g_fs.sectors_per_cluster, g_cluster);
}

static uint32_t fat_next_cluster(uint32_t cluster) {
    if (cluster < 2U || cluster >= fat32_cluster_limit()) return 0U;
    uint32_t fat_offset = cluster * 4U;
    uint32_t sector = g_fs.fat_start_lba + (fat_offset / 512U);
    if (read_sector(sector) != 0) return 0U;
    return le32(g_sector + (fat_offset % 512U)) & 0x0FFFFFFFU;
}

static int fat_set_cluster(uint32_t cluster, uint32_t value) {
    if (cluster < 2U || cluster >= fat32_cluster_limit()) return -1;
    uint32_t fat_offset = cluster * 4U;
    uint32_t sector = g_fs.fat_start_lba + (fat_offset / 512U);
    if (read_sector(sector) != 0) return -1;
    uint32_t offset = fat_offset % 512U;
    g_sector[offset] = (uint8_t)value;
    g_sector[offset + 1U] = (uint8_t)(value >> 8);
    g_sector[offset + 2U] = (uint8_t)(value >> 16);
    g_sector[offset + 3U] = (uint8_t)(value >> 24);
    if (ata_write_sectors(sector, 1U, g_sector) != 0) return -1;

    uint32_t backup = sector + g_fs.sectors_per_fat;
    if (backup < g_fs.partition_lba + g_fs.total_sectors) {
        ata_write_sectors(backup, 1U, g_sector);
    }
    return 0;
}

static uint32_t allocate_cluster(void) {
    uint32_t cluster_count = fat32_cluster_limit();
    for (uint32_t cluster = 2U; cluster < cluster_count; ++cluster) {
        if (fat_next_cluster(cluster) == FAT32_CLUSTER_FREE) {
            if (fat_set_cluster(cluster, 0x0FFFFFFFU) == 0) {
                uint32_t lba = g_fs.first_data_lba +
                               (cluster - 2U) * g_fs.sectors_per_cluster;
                memset(g_cluster, 0, g_fs.cluster_size);
                if (ata_write_sectors(lba, g_fs.sectors_per_cluster, g_cluster) == 0) {
                    return cluster;
                }
            }
        }
    }
    return 0U;
}

int fat32_init(void) {
    memset(&g_fs, 0, sizeof(g_fs));
    if (ata_sector_count() == 0U) return -1;
    if (read_sector(0U) != 0) return -1;

    uint32_t partition_lba = 0U;
    if (g_sector[510] == 0x55U && g_sector[511] == 0xAAU &&
        g_sector[82] == 'F' && g_sector[83] == 'A' && g_sector[84] == 'T') {
        /* Sector zero is already an FAT boot sector: no partition table. */
    } else if (g_sector[510] == 0x55U && g_sector[511] == 0xAAU) {
        uint32_t best = 0U;
        for (uint32_t i = 0; i < 4U; ++i) {
            uint8_t *entry = g_sector + 446U + i * 16U;
            uint8_t type = entry[4];
            uint32_t start = le32(entry + 8);
            if (type == 0x0BU || type == 0x0CU || type == 0x01U ||
                type == 0x04U || type == 0x06U) {
                best = start;
                break;
            }
        }
        partition_lba = best;
    }

    g_fs.partition_lba = partition_lba;
    if (ata_read_sectors(partition_lba, 1U, g_sector) != 0) return -1;
    if (g_sector[510] != 0x55U || g_sector[511] != 0xAAU) return -1;
    if (g_sector[16] != 2U) return -1;

    uint16_t bytes_per_sector = le16(g_sector + 11);
    uint8_t sectors_per_cluster = g_sector[13];
    uint16_t reserved = le16(g_sector + 14);
    uint32_t fat_count = g_sector[16];
    uint32_t sectors_per_fat = le32(g_sector + 36);
    uint32_t root_cluster = le32(g_sector + 44);
    uint16_t fsinfo = le16(g_sector + 48);
    (void)fsinfo;

    if (bytes_per_sector != 512U || sectors_per_cluster == 0U ||
        sectors_per_fat == 0U || root_cluster < 2U) {
        return -1;
    }

    g_fs.sectors_per_cluster = sectors_per_cluster;
    g_fs.cluster_size = bytes_per_sector * sectors_per_cluster;
    if (g_fs.cluster_size > sizeof(g_cluster)) return -1;
    g_fs.sectors_per_fat = sectors_per_fat;
    g_fs.fat_start_lba = partition_lba + reserved;
    g_fs.first_data_lba = partition_lba + reserved + fat_count * sectors_per_fat;
    g_fs.root_cluster = root_cluster;
    g_fs.total_sectors = le32(g_sector + 32);
    g_fs.mounted = 1U;

    serial_print("[FAT32] Mounted at partition LBA ");
    serial_print_dec(partition_lba);
    serial_print(", root cluster ");
    serial_print_dec(root_cluster);
    serial_putchar('\n');
    return 0;
}

int fat32_mounted(void) {
    return g_fs.mounted != 0U;
}

static void format_83(const uint8_t entry[11], char out[13]) {
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

static int name_to_83(const char *name, uint8_t out[11]) {
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

static int valid_lfn_name(const char *name) {
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

static int ascii_name_equal(const char *left, const char *right) {
    while (*left != '\0' && *right != '\0') {
        char a = *left++;
        char b = *right++;
        if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
        if (b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
        if (a != b) return 0;
    }
    return *left == *right;
}

static int find_entry_in_cluster_chain(uint32_t cluster, const uint8_t name[11],
                                       struct dir_entry_location *location,
                                       uint8_t *entry_out) {
    uint32_t chain = cluster;
    while (chain >= 2U && chain < FAT32_END_CLUSTER) {
        if (read_cluster(chain) != 0) return -1;
        for (uint32_t offset = 0; offset < g_fs.cluster_size; offset += 32U) {
            uint8_t *entry = g_cluster + offset;
            if (entry[0] == 0x00U) return 0;
            if (entry[0] == 0xE5U || entry[11] == 0x0FU ||
                (entry[11] & FAT32_ATTR_VOLUME_ID) != 0U) continue;
            if (memcmp(entry, name, 11U) == 0) {
                if (location != 0) {
                    location->cluster = chain;
                    location->index = offset;
                }
                if (entry_out != 0) memcpy(entry_out, entry, 32U);
                return 1;
            }
        }
        chain = fat_next_cluster(chain);
    }
    return 0;
}

static int find_entry_by_name(uint32_t cluster, const char *name,
                              struct dir_entry_location *location,
                              uint8_t *entry_out,
                              struct dir_entry_location *lfn_locations,
                              uint32_t *lfn_count) {
    uint8_t short_name[11];
    int has_short_name = name_to_83(name, short_name) == 0;
    if (lfn_count != 0) *lfn_count = 0U;
    if (!valid_lfn_name(name)) return 0;

    struct lfn_sequence sequence;
    lfn_reset(&sequence);
    uint32_t chain = cluster;
    uint32_t chain_count = 0U;
    while (chain >= 2U && chain < FAT32_END_CLUSTER &&
           chain_count++ < fat32_cluster_limit()) {
        if (read_cluster(chain) != 0) return -1;
        for (uint32_t offset = 0U; offset < g_fs.cluster_size; offset += 32U) {
            uint8_t *entry = g_cluster + offset;
            if (entry[0] == 0x00U) return 0;
            if (entry[0] == 0xE5U) {
                lfn_reset(&sequence);
                continue;
            }
            struct dir_entry_location current = {chain, offset};
            if (entry[11] == 0x0FU) {
                lfn_add_entry(&sequence, entry, &current);
                continue;
            }

            char long_name[FAT32_MAX_LFN_UNITS + 1U];
            int has_long_name =
                lfn_decode(&sequence, entry, long_name, sizeof(long_name));
            int matched =
                (entry[11] & FAT32_ATTR_VOLUME_ID) == 0U &&
                ((has_short_name && memcmp(entry, short_name, 11U) == 0) ||
                 (has_long_name && ascii_name_equal(name, long_name)));
            if (matched) {
                if (location != 0) *location = current;
                if (entry_out != 0) memcpy(entry_out, entry, 32U);
                if (has_long_name && lfn_count != 0) {
                    *lfn_count = sequence.total_entries;
                    if (lfn_locations != 0) {
                        memcpy(lfn_locations, sequence.locations,
                               (uint32_t)sequence.total_entries *
                                   sizeof(sequence.locations[0]));
                    }
                }
                return 1;
            }
            lfn_reset(&sequence);
        }
        chain = fat_next_cluster(chain);
    }
    return 0;
}

static int make_short_alias(uint32_t dir_cluster, const char *name,
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

struct prepared_name {
    uint8_t short_name[11];
    uint8_t utf16le[FAT32_LFN_BUFFER_BYTES];
    uint32_t utf16_length;
    uint32_t lfn_entries;
};

static int update_directory_entry(const struct dir_entry_location *location,
                                  const uint8_t entry[32]);
static int mark_directory_entry_deleted(
    const struct dir_entry_location *location);

static int prepare_name(const char *name, struct prepared_name *prepared) {
    if (!valid_lfn_name(name)) return -1;
    memset(prepared, 0, sizeof(*prepared));
    int length = ascii_to_utf16LE(name, prepared->utf16le,
                                  sizeof(prepared->utf16le));
    if (length <= 0) return -1;
    prepared->utf16_length = (uint32_t)length;
    if (name_to_83(name, prepared->short_name) != 0) {
        prepared->lfn_entries =
            (prepared->utf16_length + FAT32_LFN_UNITS_PER_ENTRY - 1U) /
            FAT32_LFN_UNITS_PER_ENTRY;
    }
    return 0;
}

static void make_lfn_entry(const struct prepared_name *name, uint32_t order,
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

static int write_lfn_entries(const struct prepared_name *name,
                             const struct dir_entry_location *locations) {
    uint8_t checksum = lfn_checksum(name->short_name);
    for (uint32_t i = 0U; i < name->lfn_entries; ++i) {
        uint32_t order = name->lfn_entries - i;
        uint8_t entry[32];
        make_lfn_entry(name, order, checksum, entry);
        if (update_directory_entry(&locations[i], entry) != 0) {
            for (uint32_t rollback = 0U; rollback < i; ++rollback) {
                (void)mark_directory_entry_deleted(&locations[rollback]);
            }
            return -1;
        }
    }
    return 0;
}

static int mark_directory_entry_deleted(
    const struct dir_entry_location *location) {
    if (read_cluster(location->cluster) != 0) return -1;
    g_cluster[location->index] = 0xE5U;
    return write_cluster(location->cluster);
}

static int mark_entry_and_lfn_deleted(
    const struct dir_entry_location *location,
    const struct dir_entry_location *lfn_locations, uint32_t lfn_count) {
    for (uint32_t i = 0U; i < lfn_count; ++i) {
        if (mark_directory_entry_deleted(&lfn_locations[i]) != 0) return -1;
    }
    return mark_directory_entry_deleted(location);
}

static uint32_t resolve_directory(const char *path) {
    if (path == 0 || path[0] == '\0') return g_fs.root_cluster;
    if (!g_fs.mounted) return 0U;

    char temp[128];
    uint32_t path_length = strlen(path);
    if (path_length >= sizeof(temp)) return 0U;
    memcpy(temp, path, path_length + 1U);
    uint32_t cluster = g_fs.root_cluster;
    char *component = temp;
    while (*component) {
        while (*component == '/') ++component;
        if (*component == '\0') break;
        char *slash = component;
        while (*slash && *slash != '/') ++slash;
        char saved = *slash;
        *slash = '\0';
        uint8_t entry[32];
        if (find_entry_by_name(cluster, component, 0, entry, 0, 0) != 1) {
            return 0U;
        }
        if ((entry[11] & FAT32_ATTR_DIRECTORY) == 0U) return 0U;
        cluster = (uint32_t)le16(entry + 20) << 16 | le16(entry + 26);
        *slash = saved;
        component = saved ? slash + 1 : slash;
    }
    return cluster;
}

int fat32_list_directory(const char *path, fat32_list_callback_t callback) {
    if (!g_fs.mounted) return -1;
    uint32_t cluster = resolve_directory(path);
    if (cluster < 2U) return -1;
    struct lfn_sequence sequence;
    lfn_reset(&sequence);
    while (cluster >= 2U && cluster < FAT32_END_CLUSTER) {
        if (read_cluster(cluster) != 0) return -1;
        for (uint32_t offset = 0; offset < g_fs.cluster_size; offset += 32U) {
            uint8_t *entry = g_cluster + offset;
            if (entry[0] == 0x00U) return 0;
            if (entry[0] == 0xE5U) {
                lfn_reset(&sequence);
                continue;
            }
            struct dir_entry_location current = {cluster, offset};
            if (entry[11] == 0x0FU) {
                lfn_add_entry(&sequence, entry, &current);
                continue;
            }
            char name[FAT32_MAX_LFN_UNITS + 1U];
            if (!lfn_decode(&sequence, entry, name, sizeof(name))) {
                format_83(entry, name);
            }
            lfn_reset(&sequence);
            if ((entry[11] & FAT32_ATTR_VOLUME_ID) != 0U ||
                entry[0] == '.') continue;
            if (callback != 0) {
                callback(name, entry[11], le32(entry + 28));
            }
        }
        cluster = fat_next_cluster(cluster);
    }
    return 0;
}

int fat32_read_file(const char *path, void *buffer, uint32_t buffer_size,
                    uint32_t *size) {
    if (!g_fs.mounted || path == 0 || buffer == 0 || size == 0) return -1;
    *size = 0U;

    char temp[128];
    uint32_t path_length = strlen(path);
    if (path_length >= sizeof(temp)) return -1;
    memcpy(temp, path, path_length + 1U);
    char *slash = 0;
    for (char *p = temp + strlen(temp); p > temp; --p) {
        if (p[-1] == '/') { slash = p - 1; break; }
    }
    char *name;
    uint32_t dir_cluster;
    if (slash == 0) {
        name = temp;
        dir_cluster = g_fs.root_cluster;
    } else {
        *slash = '\0';
        name = slash + 1;
        dir_cluster = resolve_directory(temp);
        if (dir_cluster < 2U) return -1;
    }
    if (name[0] == '\0') return -1;

    uint8_t entry[32];
    if (find_entry_by_name(dir_cluster, name, 0, entry, 0, 0) != 1) return -1;
    if ((entry[11] & FAT32_ATTR_DIRECTORY) != 0U) return -1;

    uint32_t file_size = le32(entry + 28);
    uint32_t to_read = file_size < buffer_size ? file_size : buffer_size;
    uint32_t cluster = (uint32_t)le16(entry + 20) << 16 | le16(entry + 26);
    uint32_t offset = 0U;
    while (offset < to_read && cluster >= 2U && cluster < FAT32_END_CLUSTER) {
        if (read_cluster(cluster) != 0) return -1;
        uint32_t chunk = g_fs.cluster_size;
        if (chunk > to_read - offset) chunk = to_read - offset;
        memcpy((uint8_t *)buffer + offset, g_cluster, chunk);
        offset += chunk;
        cluster = fat_next_cluster(cluster);
    }
    *size = offset;
    return offset == file_size || offset == buffer_size ? 0 : -1;
}

static int split_path(const char *path, char *parent, uint32_t parent_size,
                      char *name, uint32_t name_size) {
    if (path == 0 || path[0] == '\0' || parent == 0 || name == 0 ||
        parent_size == 0U || name_size == 0U) {
        return -1;
    }
    char temp[128];
    uint32_t path_length = strlen(path);
    if (path_length >= sizeof(temp)) return -1;
    memcpy(temp, path, path_length + 1U);

    char *slash = 0;
    for (char *p = temp + strlen(temp); p > temp; --p) {
        if (p[-1] == '/') { slash = p - 1; break; }
    }
    if (slash == 0) {
        if (parent_size < 2U || path_length + 1U > name_size) return -1;
        strncpy(parent, "/", parent_size);
        parent[parent_size - 1U] = '\0';
        strncpy(name, temp, name_size);
    } else {
        *slash = '\0';
        uint32_t parent_length = strlen(temp);
        uint32_t name_length = strlen(slash + 1);
        if ((temp[0] == '\0' ? 1U : parent_length) + 1U > parent_size ||
            name_length + 1U > name_size) {
            return -1;
        }
        if (temp[0] == '\0') {
            strncpy(parent, "/", parent_size);
        } else {
            strncpy(parent, temp, parent_size);
        }
        strncpy(name, slash + 1, name_size);
    }
    parent[parent_size - 1U] = '\0';
    name[name_size - 1U] = '\0';
    return name[0] == '\0' ? -1 : 0;
}

static int free_cluster_chain(uint32_t cluster);

static int find_free_directory_slots(
    uint32_t dir_cluster, uint32_t slot_count,
    struct dir_entry_location locations[FAT32_MAX_DIRECTORY_SLOTS]) {
    if (slot_count == 0U || slot_count > FAT32_MAX_DIRECTORY_SLOTS) return -1;
    uint32_t chain = dir_cluster;
    uint32_t last = 0U;
    uint32_t free_run = 0U;
    uint32_t chain_count = 0U;
    uint32_t end_marker_seen = 0U;
    while (chain >= 2U && chain < FAT32_END_CLUSTER) {
        if (chain_count++ >= fat32_cluster_limit()) return -1;
        last = chain;
        if (read_cluster(chain) != 0) return -1;
        for (uint32_t offset = 0U; offset < g_fs.cluster_size; offset += 32U) {
            uint8_t first = g_cluster[offset];
            if (end_marker_seen != 0U || first == 0x00U || first == 0xE5U) {
                if (first == 0x00U) end_marker_seen = 1U;
                locations[free_run].cluster = chain;
                locations[free_run].index = offset;
                if (++free_run == slot_count) return 1;
            } else {
                free_run = 0U;
            }
        }
        uint32_t next = fat_next_cluster(chain);
        if (next == FAT32_BAD_CLUSTER) return -1;
        if (next >= FAT32_END_CLUSTER) break;
        if (next < 2U || next >= fat32_cluster_limit()) return -1;
        chain = next;
    }

    while (free_run < slot_count) {
        uint32_t next = allocate_cluster();
        if (next == 0U) return -1;
        if (fat_set_cluster(last, next) != 0) {
            (void)free_cluster_chain(next);
            return -1;
        }
        last = next;
        for (uint32_t offset = 0U; offset < g_fs.cluster_size; offset += 32U) {
            locations[free_run].cluster = next;
            locations[free_run].index = offset;
            if (++free_run == slot_count) return 1;
        }
    }
    return 1;
}

static int insert_directory_entry(uint32_t dir_cluster,
                                  const struct prepared_name *prepared,
                                  const uint8_t entry[32]) {
    struct dir_entry_location locations[FAT32_MAX_DIRECTORY_SLOTS];
    uint32_t slot_count = prepared->lfn_entries + 1U;
    if (find_free_directory_slots(dir_cluster, slot_count, locations) != 1) {
        return -1;
    }
    struct dir_entry_location *short_location =
        &locations[prepared->lfn_entries];
    if (write_lfn_entries(prepared, locations) != 0 ||
        update_directory_entry(short_location, entry) != 0) {
        for (uint32_t i = 0U; i < prepared->lfn_entries; ++i) {
            (void)mark_directory_entry_deleted(&locations[i]);
        }
        return -1;
    }
    return 0;
}

static int update_directory_entry(const struct dir_entry_location *location,
                                  const uint8_t entry[32]) {
    if (read_cluster(location->cluster) != 0) return -1;
    memcpy(g_cluster + location->index, entry, 32U);
    return write_cluster(location->cluster);
}

static int free_cluster_chain(uint32_t cluster) {
    if (cluster == 0U) return 0;
    uint32_t limit = fat32_cluster_limit();
    for (uint32_t count = 0U; count < limit; ++count) {
        if (cluster < 2U || cluster >= limit || cluster == FAT32_BAD_CLUSTER) {
            return -1;
        }
        uint32_t next = fat_next_cluster(cluster);
        if (fat_set_cluster(cluster, FAT32_CLUSTER_FREE) != 0) return -1;
        if (next >= FAT32_END_CLUSTER) return 0;
        if (next < 2U || next >= limit || next == FAT32_BAD_CLUSTER) return -1;
        cluster = next;
    }
    return -1;
}

int fat32_write_file(const char *path, const void *buffer, uint32_t size) {
    if (!g_fs.mounted || path == 0 || (size != 0U && buffer == 0)) return -1;
    if (size > FAT32_MAX_FILE_SIZE) return -1;
    char parent[128], name[128];
    if (split_path(path, parent, sizeof(parent), name, sizeof(name)) != 0) return -1;
    uint32_t dir_cluster = resolve_directory(parent);
    if (dir_cluster < 2U) return -1;

    struct prepared_name prepared;
    if (prepare_name(name, &prepared) != 0) return -1;
    uint8_t entry[32];
    struct dir_entry_location location;
    int found = find_entry_by_name(dir_cluster, name, &location, entry, 0, 0);
    if (found < 0) return -1;
    if (found == 1 && (entry[11] & FAT32_ATTR_DIRECTORY) != 0U) return -1;
    if (found == 1) {
        memcpy(prepared.short_name, entry, sizeof(prepared.short_name));
    } else if (prepared.lfn_entries != 0U &&
               make_short_alias(dir_cluster, name, prepared.short_name) != 0) {
        return -1;
    }

    uint32_t old_cluster = found == 1
        ? ((uint32_t)le16(entry + 20) << 16 | le16(entry + 26)) : 0U;
    if (old_cluster >= 2U && free_cluster_chain(old_cluster) != 0) return -1;

    uint32_t first = 0U;
    uint32_t current = 0U;
    uint32_t written = 0U;
    while (written < size) {
        uint32_t next = allocate_cluster();
        if (next == 0U) {
            if (first >= 2U) (void)free_cluster_chain(first);
            return -1;
        }
        if (first == 0U) first = next;
        else (void)fat_set_cluster(current, next);
        current = next;

        memset(g_cluster, 0, g_fs.cluster_size);
        uint32_t chunk = size - written;
        if (chunk > g_fs.cluster_size) chunk = g_fs.cluster_size;
        memcpy(g_cluster, (const uint8_t *)buffer + written, chunk);
        written += chunk;
        if (write_cluster(current) != 0) {
            (void)free_cluster_chain(first);
            return -1;
        }
    }

    memset(entry, 0, 32U);
    memcpy(entry, prepared.short_name, 11U);
    entry[11] = 0x20U;
    entry[26] = (uint8_t)first;
    entry[27] = (uint8_t)(first >> 8);
    entry[20] = (uint8_t)(first >> 16);
    entry[21] = (uint8_t)(first >> 24);
    entry[28] = (uint8_t)size;
    entry[29] = (uint8_t)(size >> 8);
    entry[30] = (uint8_t)(size >> 16);
    entry[31] = (uint8_t)(size >> 24);

    if (found != 1) {
        int result = insert_directory_entry(dir_cluster, &prepared, entry);
        if (result != 0 && first >= 2U) {
            (void)free_cluster_chain(first);
        }
        return result;
    }
    return update_directory_entry(&location, entry);
}

int fat32_copy_file(const char *src_path, const char *dst_path) {
    if (!g_fs.mounted || src_path == 0 || dst_path == 0) return -1;
    uint8_t *buffer = (uint8_t *)kmalloc(FAT32_MAX_FILE_SIZE);
    if (buffer == 0) return -1;
    uint32_t size = 0U;
    int result = fat32_read_file(src_path, buffer, FAT32_MAX_FILE_SIZE, &size);
    if (result == 0) result = fat32_write_file(dst_path, buffer, size);
    kfree(buffer);
    return result;
}

int fat32_is_directory(const char *path) {
    if (!g_fs.mounted || path == 0) return -1;
    char parent[128], name[128];
    if (split_path(path, parent, sizeof(parent), name, sizeof(name)) != 0) {
        return -1;
    }
    uint32_t dir_cluster = resolve_directory(parent);
    if (dir_cluster < 2U) return -1;
    uint8_t entry[32];
    if (find_entry_by_name(dir_cluster, name, 0, entry, 0, 0) != 1) {
        return -1;
    }
    return (entry[11] & FAT32_ATTR_DIRECTORY) != 0U ? 1 : 0;
}

static uint32_t entry_first_cluster(const uint8_t entry[32]) {
    return ((uint32_t)le16(entry + 20) << 16) | le16(entry + 26);
}

static void delete_entry_by_name(uint32_t dir_cluster, const char *name) {
    uint8_t entry[32];
    struct dir_entry_location location;
    struct dir_entry_location lfn_locations[FAT32_MAX_LFN_ENTRIES];
    uint32_t lfn_count = 0U;
    if (find_entry_by_name(dir_cluster, name, &location, entry,
                           lfn_locations, &lfn_count) == 1) {
        (void)mark_entry_and_lfn_deleted(&location, lfn_locations, lfn_count);
    }
}

static int directory_chain_contains(uint32_t start_cluster, uint32_t target) {
    uint32_t cluster = start_cluster;
    uint32_t limit = fat32_cluster_limit();
    for (uint32_t count = 0U; count < limit; ++count) {
        if (cluster == target) return 1;
        if (cluster < 2U || cluster >= limit || cluster == g_fs.root_cluster) {
            return 0;
        }
        if (read_cluster(cluster) != 0) return 0;
        uint8_t *dotdot = g_cluster + 32U;
        if (dotdot[0] != '.' || dotdot[1] != '.' ||
            (dotdot[11] & FAT32_ATTR_DIRECTORY) == 0U) {
            return 0;
        }
        uint32_t parent = entry_first_cluster(dotdot);
        if (parent == cluster || parent < 2U || parent >= limit) return 0;
        cluster = parent;
    }
    return 0;
}

static int update_parent_pointer(uint32_t dir_cluster, uint32_t parent_cluster) {
    if (read_cluster(dir_cluster) != 0) return -1;
    uint8_t *dotdot = g_cluster + 32U;
    if (dotdot[0] != '.' || dotdot[1] != '.' ||
        (dotdot[11] & FAT32_ATTR_DIRECTORY) == 0U) {
        return -1;
    }
    dotdot[20] = (uint8_t)(parent_cluster >> 16);
    dotdot[21] = (uint8_t)(parent_cluster >> 24);
    dotdot[26] = (uint8_t)parent_cluster;
    dotdot[27] = (uint8_t)(parent_cluster >> 8);
    return write_cluster(dir_cluster);
}

int fat32_path_within(const char *ancestor, const char *path) {
    if (!g_fs.mounted || ancestor == 0 || path == 0) return -1;
    char ancestor_parent[128], ancestor_name[128];
    char path_parent[128], path_name[128];
    if (split_path(ancestor, ancestor_parent, sizeof(ancestor_parent),
                   ancestor_name, sizeof(ancestor_name)) != 0 ||
        split_path(path, path_parent, sizeof(path_parent),
                   path_name, sizeof(path_name)) != 0) {
        return -1;
    }
    uint32_t ancestor_dir = resolve_directory(ancestor_parent);
    if (ancestor_dir < 2U) return -1;
    uint8_t ancestor_entry[32];
    if (find_entry_by_name(ancestor_dir, ancestor_name, 0, ancestor_entry,
                           0, 0) != 1 ||
        (ancestor_entry[11] & FAT32_ATTR_DIRECTORY) == 0U) {
        return -1;
    }
    uint32_t ancestor_cluster = entry_first_cluster(ancestor_entry);

    uint32_t path_dir = resolve_directory(path_parent);
    if (path_dir < 2U) return -1;
    uint8_t path_entry[32];
    int found = find_entry_by_name(path_dir, path_name, 0, path_entry, 0, 0);
    uint32_t start;
    if (found == 1 && (path_entry[11] & FAT32_ATTR_DIRECTORY) != 0U) {
        start = entry_first_cluster(path_entry);
    } else {
        start = path_dir;
    }
    return directory_chain_contains(start, ancestor_cluster);
}

int fat32_move(const char *src_path, const char *dst_path) {
    if (!g_fs.mounted || src_path == 0 || dst_path == 0) return -1;
    char src_parent[128], src_name[128];
    char dst_parent[128], dst_name[128];
    if (split_path(src_path, src_parent, sizeof(src_parent), src_name,
                   sizeof(src_name)) != 0 ||
        split_path(dst_path, dst_parent, sizeof(dst_parent), dst_name,
                   sizeof(dst_name)) != 0) {
        return -1;
    }
    uint32_t src_dir = resolve_directory(src_parent);
    uint32_t dst_dir = resolve_directory(dst_parent);
    if (src_dir < 2U || dst_dir < 2U) return -1;

    uint8_t entry[32];
    struct dir_entry_location location;
    struct dir_entry_location lfn_locations[FAT32_MAX_LFN_ENTRIES];
    uint32_t lfn_count = 0U;
    if (find_entry_by_name(src_dir, src_name, &location, entry,
                           lfn_locations, &lfn_count) != 1) {
        return -1;
    }
    struct dir_entry_location existing_location;
    uint8_t existing[32];
    int dst_found = find_entry_by_name(dst_dir, dst_name, &existing_location,
                                       existing, 0, 0);
    if (dst_found < 0) return -1;
    if (dst_found == 1) {
        if (existing_location.cluster == location.cluster &&
            existing_location.index == location.index) {
            return -1;
        }
        if ((existing[11] & FAT32_ATTR_DIRECTORY) != 0U ||
            fat32_delete_file(dst_path) != 0) {
            return -1;
        }
    }

    struct prepared_name prepared;
    if (prepare_name(dst_name, &prepared) != 0) return -1;
    if (prepared.lfn_entries != 0U &&
        make_short_alias(dst_dir, dst_name, prepared.short_name) != 0) {
        return -1;
    }

    uint8_t new_entry[32];
    memcpy(new_entry, entry, sizeof(new_entry));
    memcpy(new_entry, prepared.short_name, sizeof(prepared.short_name));

    int is_dir = (entry[11] & FAT32_ATTR_DIRECTORY) != 0U;
    uint32_t own_cluster = entry_first_cluster(entry);
    if (is_dir &&
        (own_cluster < 2U || own_cluster >= fat32_cluster_limit() ||
         directory_chain_contains(dst_dir, own_cluster))) {
        return -1;
    }

    if (insert_directory_entry(dst_dir, &prepared, new_entry) != 0) {
        return -1;
    }
    if (is_dir && update_parent_pointer(own_cluster, dst_dir) != 0) {
        delete_entry_by_name(dst_dir, dst_name);
        return -1;
    }
    if (mark_entry_and_lfn_deleted(&location, lfn_locations, lfn_count) != 0) {
        delete_entry_by_name(dst_dir, dst_name);
        return -1;
    }
    return 0;
}

int fat32_delete_file(const char *path) {
    if (!g_fs.mounted) return -1;
    char parent[128], name[128];
    if (split_path(path, parent, sizeof(parent), name, sizeof(name)) != 0) return -1;
    uint32_t dir_cluster = resolve_directory(parent);
    if (dir_cluster < 2U) return -1;
    uint8_t entry[32];
    struct dir_entry_location location;
    struct dir_entry_location lfn_locations[FAT32_MAX_LFN_ENTRIES];
    uint32_t lfn_count = 0U;
    int found = find_entry_by_name(dir_cluster, name, &location, entry,
                                   lfn_locations, &lfn_count);
    if (found != 1 || (entry[11] & FAT32_ATTR_DIRECTORY) != 0U) return -1;
    uint32_t cluster = (uint32_t)le16(entry + 20) << 16 | le16(entry + 26);
    if (free_cluster_chain(cluster) != 0) return -1;
    return mark_entry_and_lfn_deleted(&location, lfn_locations, lfn_count);
}

int fat32_make_directory(const char *path) {
    if (!g_fs.mounted) return -1;
    char parent[128], name[128];
    if (split_path(path, parent, sizeof(parent), name, sizeof(name)) != 0) return -1;
    uint32_t dir_cluster = resolve_directory(parent);
    if (dir_cluster < 2U) return -1;
    struct prepared_name prepared;
    if (prepare_name(name, &prepared) != 0) return -1;
    uint8_t existing[32];
    int found = find_entry_by_name(dir_cluster, name, 0, existing, 0, 0);
    if (found != 0) return -1;
    if (prepared.lfn_entries != 0U &&
        make_short_alias(dir_cluster, name, prepared.short_name) != 0) {
        return -1;
    }

    uint32_t cluster = allocate_cluster();
    if (cluster == 0U) return -1;
    memset(g_cluster, 0, g_fs.cluster_size);
    g_cluster[0] = '.';
    memset(g_cluster + 1, ' ', 10U);
    g_cluster[11] = FAT32_ATTR_DIRECTORY;
    g_cluster[26] = (uint8_t)cluster;
    g_cluster[27] = (uint8_t)(cluster >> 8);
    g_cluster[20] = (uint8_t)(cluster >> 16);
    g_cluster[21] = (uint8_t)(cluster >> 24);
    g_cluster[32] = '.';
    g_cluster[33] = '.';
    memset(g_cluster + 34, ' ', 9U);
    g_cluster[43] = FAT32_ATTR_DIRECTORY;
    g_cluster[58] = (uint8_t)dir_cluster;
    g_cluster[59] = (uint8_t)(dir_cluster >> 8);
    g_cluster[52] = (uint8_t)(dir_cluster >> 16);
    g_cluster[53] = (uint8_t)(dir_cluster >> 24);
    if (write_cluster(cluster) != 0) {
        (void)free_cluster_chain(cluster);
        return -1;
    }

    uint8_t entry[32];
    memset(entry, 0, 32U);
    memcpy(entry, prepared.short_name, 11U);
    entry[11] = FAT32_ATTR_DIRECTORY;
    entry[26] = (uint8_t)cluster;
    entry[27] = (uint8_t)(cluster >> 8);
    entry[20] = (uint8_t)(cluster >> 16);
    entry[21] = (uint8_t)(cluster >> 24);
    struct dir_entry_location locations[FAT32_MAX_DIRECTORY_SLOTS];
    uint32_t slot_count = prepared.lfn_entries + 1U;
    if (find_free_directory_slots(dir_cluster, slot_count, locations) != 1) {
        (void)free_cluster_chain(cluster);
        return -1;
    }
    struct dir_entry_location *short_location =
        &locations[prepared.lfn_entries];
    if (write_lfn_entries(&prepared, locations) != 0 ||
        update_directory_entry(short_location, entry) != 0) {
        for (uint32_t i = 0U; i < prepared.lfn_entries; ++i) {
            (void)mark_directory_entry_deleted(&locations[i]);
        }
        (void)free_cluster_chain(cluster);
        return -1;
    }
    return 0;
}

static int fat32_directory_is_empty(uint32_t first_cluster) {
    uint32_t limit = fat32_cluster_limit();
    uint32_t cluster = first_cluster;

    for (uint32_t count = 0U; count < limit; ++count) {
        if (cluster < 2U || cluster >= limit || read_cluster(cluster) != 0) {
            return -1;
        }
        uint32_t next = fat_next_cluster(cluster);
        for (uint32_t offset = 0; offset < g_fs.cluster_size; offset += 32U) {
            uint8_t *entry = g_cluster + offset;
            if (entry[0] == 0x00U) return 1;
            if (entry[0] == 0xE5U || entry[11] == 0x0FU ||
                (entry[11] & FAT32_ATTR_VOLUME_ID) != 0U ||
                entry[0] == '.') {
                continue;
            }
            return 0;
        }
        if (next >= FAT32_END_CLUSTER) return 1;
        if (next < 2U || next >= limit || next == FAT32_BAD_CLUSTER) return -1;
        cluster = next;
    }
    return -1;
}

static int fat32_delete_directory_contents(uint32_t first_cluster, uint32_t depth) {
    uint32_t limit = fat32_cluster_limit();
    uint32_t cluster = first_cluster;
    struct lfn_location_sequence sequence;

    if (depth >= FAT32_MAX_DELETE_DEPTH) return -1;
    lfn_location_reset(&sequence);
    for (uint32_t count = 0U; count < limit; ++count) {
        if (cluster < 2U || cluster >= limit || read_cluster(cluster) != 0) {
            return -1;
        }
        uint32_t next = fat_next_cluster(cluster);
        if (next < 2U && next < FAT32_END_CLUSTER) return -1;
        if (next == FAT32_BAD_CLUSTER || next >= FAT32_END_CLUSTER) {
            if (next == FAT32_BAD_CLUSTER) return -1;
        } else if (next >= limit) {
            return -1;
        }

        for (uint32_t offset = 0; offset < g_fs.cluster_size; offset += 32U) {
            if (read_cluster(cluster) != 0) return -1;
            uint8_t entry[32];
            memcpy(entry, g_cluster + offset, sizeof(entry));
            if (entry[0] == 0x00U) return 0;
            if (entry[0] == 0xE5U) {
                lfn_location_reset(&sequence);
                continue;
            }
            struct dir_entry_location location = {cluster, offset};
            if (entry[11] == 0x0FU) {
                lfn_location_add_entry(&sequence, entry, &location);
                continue;
            }

            uint32_t lfn_count = lfn_locations_match(&sequence, entry)
                ? sequence.total_entries : 0U;
            if ((entry[11] & FAT32_ATTR_VOLUME_ID) != 0U ||
                entry[0] == '.') {
                lfn_location_reset(&sequence);
                continue;
            }

            uint32_t child_cluster =
                ((uint32_t)le16(entry + 20) << 16) | le16(entry + 26);
            if ((entry[11] & FAT32_ATTR_DIRECTORY) != 0U) {
                if (child_cluster < 2U ||
                    fat32_delete_directory_contents(child_cluster, depth + 1U) != 0 ||
                    free_cluster_chain(child_cluster) != 0) {
                    return -1;
                }
            } else if (free_cluster_chain(child_cluster) != 0) {
                return -1;
            }

            if (mark_entry_and_lfn_deleted(&location, sequence.locations,
                                           lfn_count) != 0) {
                return -1;
            }
            lfn_location_reset(&sequence);
        }

        if (next >= FAT32_END_CLUSTER) return 0;
        cluster = next;
    }
    return -1;
}

static int fat32_remove_directory_internal(const char *path, int recursive) {
    if (!g_fs.mounted) return -1;
    char parent[128], name[128];
    if (split_path(path, parent, sizeof(parent), name, sizeof(name)) != 0) return -1;
    uint32_t dir_cluster = resolve_directory(parent);
    if (dir_cluster < 2U) return -1;
    uint8_t entry[32];
    struct dir_entry_location location;
    struct dir_entry_location lfn_locations[FAT32_MAX_LFN_ENTRIES];
    uint32_t lfn_count = 0U;
    int found = find_entry_by_name(dir_cluster, name, &location, entry,
                                   lfn_locations, &lfn_count);
    if (found != 1 || (entry[11] & FAT32_ATTR_DIRECTORY) == 0U) return -1;

    uint32_t cluster = ((uint32_t)le16(entry + 20) << 16) | le16(entry + 26);
    if (cluster < 2U || cluster >= fat32_cluster_limit()) return -1;
    if (recursive) {
        if (fat32_delete_directory_contents(cluster, 0U) != 0) return -1;
    } else if (fat32_directory_is_empty(cluster) != 1) {
        return -1;
    }

    if (free_cluster_chain(cluster) != 0) return -1;
    return mark_entry_and_lfn_deleted(&location, lfn_locations, lfn_count);
}

int fat32_remove_directory(const char *path) {
    return fat32_remove_directory_internal(path, 0);
}

int fat32_remove_directory_recursive(const char *path) {
    return fat32_remove_directory_internal(path, 1);
}
