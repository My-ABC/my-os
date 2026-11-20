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

static int name_to_83(const char *name, uint8_t out[11]) {
    if (name == 0 || name[0] == '\0' || strlen(name) > 12U) return -1;
    memset(out, ' ', 11U);
    const char *dot = strchr(name, '.');
    uint32_t base_len = dot == 0 ? strlen(name) : (uint32_t)(dot - name);
    if (base_len == 0U || base_len > 8U) return -1;
    for (uint32_t i = 0; i < base_len; ++i) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c == '/' || c == '\\') return -1;
        out[i] = (uint8_t)c;
    }
    if (dot != 0) {
        uint32_t ext_len = strlen(dot + 1);
        if (ext_len == 0U || ext_len > 3U) return -1;
        for (uint32_t i = 0; i < ext_len; ++i) {
            char c = dot[1 + i];
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            out[8U + i] = (uint8_t)c;
        }
    }
    return 0;
}

struct dir_entry_location {
    uint32_t cluster;
    uint32_t index;
};

static int find_entry_in_cluster_chain(uint32_t cluster, const uint8_t name[11],
                                       struct dir_entry_location *location,
                                       uint8_t *entry_out) {
    uint32_t chain = cluster;
    while (chain >= 2U && chain < FAT32_END_CLUSTER) {
        if (read_cluster(chain) != 0) return -1;
        for (uint32_t offset = 0; offset < g_fs.cluster_size; offset += 32U) {
            uint8_t *entry = g_cluster + offset;
            if (entry[0] == 0x00U) return 0;
            if (entry[0] == 0xE5U || entry[11] == 0x0FU) continue;
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

static uint32_t resolve_directory(const char *path) {
    if (path == 0 || path[0] == '\0') return g_fs.root_cluster;
    if (!g_fs.mounted) return 0U;

    char temp[128];
    strncpy(temp, path, sizeof(temp));
    temp[sizeof(temp) - 1U] = '\0';
    uint32_t cluster = g_fs.root_cluster;
    char *component = temp;
    while (*component) {
        while (*component == '/') ++component;
        if (*component == '\0') break;
        char *slash = component;
        while (*slash && *slash != '/') ++slash;
        char saved = *slash;
        *slash = '\0';
        uint8_t name[11];
        if (name_to_83(component, name) != 0) return 0U;
        uint8_t entry[32];
        if (find_entry_in_cluster_chain(cluster, name, 0, entry) != 1) return 0U;
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
    while (cluster >= 2U && cluster < FAT32_END_CLUSTER) {
        if (read_cluster(cluster) != 0) return -1;
        for (uint32_t offset = 0; offset < g_fs.cluster_size; offset += 32U) {
            uint8_t *entry = g_cluster + offset;
            if (entry[0] == 0x00U) return 0;
            if (entry[0] == 0xE5U || entry[11] == 0x0FU ||
                (entry[11] & 0x08U) != 0U) continue;
            if (entry[0] == '.' ) continue;
            char name[13];
            format_83(entry, name);
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
    strncpy(temp, path, sizeof(temp));
    temp[sizeof(temp) - 1U] = '\0';
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
        if (dir_cluster < 2U) dir_cluster = g_fs.root_cluster;
    }
    if (name[0] == '\0') return -1;

    uint8_t short_name[11];
    if (name_to_83(name, short_name) != 0) return -1;
    uint8_t entry[32];
    if (find_entry_in_cluster_chain(dir_cluster, short_name, 0, entry) != 1) return -1;
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
    if (path == 0 || path[0] == '\0') return -1;
    char temp[128];
    strncpy(temp, path, sizeof(temp));
    temp[sizeof(temp) - 1U] = '\0';

    char *slash = 0;
    for (char *p = temp + strlen(temp); p > temp; --p) {
        if (p[-1] == '/') { slash = p - 1; break; }
    }
    if (slash == 0) {
        strncpy(parent, "/", parent_size);
        parent[parent_size - 1U] = '\0';
        strncpy(name, temp, name_size);
    } else {
        *slash = '\0';
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

static int find_free_directory_slot(uint32_t dir_cluster,
                                    struct dir_entry_location *location) {
    uint32_t chain = dir_cluster;
    uint32_t last = 0U;
    while (chain >= 2U && chain < FAT32_END_CLUSTER) {
        last = chain;
        if (read_cluster(chain) != 0) return -1;
        for (uint32_t offset = 0; offset < g_fs.cluster_size; offset += 32U) {
            if (g_cluster[offset] == 0x00U || g_cluster[offset] == 0xE5U) {
                location->cluster = chain;
                location->index = offset;
                return 1;
            }
        }
        chain = fat_next_cluster(chain);
    }
    uint32_t next = allocate_cluster();
    if (next == 0U) return -1;
    if (fat_set_cluster(last, next) != 0) return -1;
    location->cluster = next;
    location->index = 0U;
    return 1;
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

static int append_cluster(uint32_t first, uint32_t *last_out) {
    uint32_t cluster = first;
    uint32_t last = first;
    while (cluster >= 2U && cluster < FAT32_END_CLUSTER) {
        last = cluster;
        uint32_t next = fat_next_cluster(cluster);
        if (next >= FAT32_END_CLUSTER) break;
        cluster = next;
    }
    uint32_t next = allocate_cluster();
    if (next == 0U) return -1;
    if (fat_set_cluster(last, next) != 0) return -1;
    if (last_out != 0) *last_out = next;
    return 0;
}

int fat32_write_file(const char *path, const void *buffer, uint32_t size) {
    if (!g_fs.mounted || path == 0 || (size != 0U && buffer == 0)) return -1;
    if (size > FAT32_MAX_FILE_SIZE) return -1;
    char parent[128], name[128];
    if (split_path(path, parent, sizeof(parent), name, sizeof(name)) != 0) return -1;
    uint32_t dir_cluster = resolve_directory(parent);
    if (dir_cluster < 2U) return -1;

    uint8_t short_name[11];
    if (name_to_83(name, short_name) != 0) return -1;
    uint8_t entry[32];
    struct dir_entry_location location;
    int found = find_entry_in_cluster_chain(dir_cluster, short_name, &location, entry);
    if (found < 0) return -1;
    if (found == 1 && (entry[11] & FAT32_ATTR_DIRECTORY) != 0U) return -1;

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
    memcpy(entry, short_name, 11U);
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
        if (find_free_directory_slot(dir_cluster, &location) != 1) {
            if (first >= 2U) (void)free_cluster_chain(first);
            return -1;
        }
    }
    return update_directory_entry(&location, entry);
}

int fat32_delete_file(const char *path) {
    if (!g_fs.mounted) return -1;
    char parent[128], name[128];
    if (split_path(path, parent, sizeof(parent), name, sizeof(name)) != 0) return -1;
    uint32_t dir_cluster = resolve_directory(parent);
    if (dir_cluster < 2U) return -1;
    uint8_t short_name[11];
    if (name_to_83(name, short_name) != 0) return -1;
    uint8_t entry[32];
    struct dir_entry_location location;
    int found = find_entry_in_cluster_chain(dir_cluster, short_name, &location, entry);
    if (found != 1 || (entry[11] & FAT32_ATTR_DIRECTORY) != 0U) return -1;
    uint32_t cluster = (uint32_t)le16(entry + 20) << 16 | le16(entry + 26);
    if (free_cluster_chain(cluster) != 0) return -1;
    if (read_cluster(location.cluster) != 0) return -1;
    g_cluster[location.index] = 0xE5U;
    return write_cluster(location.cluster);
}

int fat32_make_directory(const char *path) {
    if (!g_fs.mounted) return -1;
    char parent[128], name[128];
    if (split_path(path, parent, sizeof(parent), name, sizeof(name)) != 0) return -1;
    uint32_t dir_cluster = resolve_directory(parent);
    if (dir_cluster < 2U) return -1;
    uint8_t short_name[11];
    if (name_to_83(name, short_name) != 0) return -1;
    if (find_entry_in_cluster_chain(dir_cluster, short_name, 0, 0) == 1) return -1;

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
    memcpy(entry, short_name, 11U);
    entry[11] = FAT32_ATTR_DIRECTORY;
    entry[26] = (uint8_t)cluster;
    entry[27] = (uint8_t)(cluster >> 8);
    entry[20] = (uint8_t)(cluster >> 16);
    entry[21] = (uint8_t)(cluster >> 24);
    struct dir_entry_location location;
    if (find_free_directory_slot(dir_cluster, &location) != 1) {
        (void)free_cluster_chain(cluster);
        return -1;
    }
    return update_directory_entry(&location, entry);
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

    if (depth >= FAT32_MAX_DELETE_DEPTH) return -1;
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
            if (entry[0] == 0xE5U || entry[11] == 0x0FU ||
                (entry[11] & FAT32_ATTR_VOLUME_ID) != 0U ||
                entry[0] == '.') {
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

            if (read_cluster(cluster) != 0) return -1;
            g_cluster[offset] = 0xE5U;
            if (write_cluster(cluster) != 0) return -1;
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
    uint8_t short_name[11];
    if (name_to_83(name, short_name) != 0) return -1;
    uint8_t entry[32];
    struct dir_entry_location location;
    int found = find_entry_in_cluster_chain(dir_cluster, short_name, &location, entry);
    if (found != 1 || (entry[11] & FAT32_ATTR_DIRECTORY) == 0U) return -1;

    uint32_t cluster = ((uint32_t)le16(entry + 20) << 16) | le16(entry + 26);
    if (cluster < 2U || cluster >= fat32_cluster_limit()) return -1;
    if (recursive) {
        if (fat32_delete_directory_contents(cluster, 0U) != 0) return -1;
    } else if (fat32_directory_is_empty(cluster) != 1) {
        return -1;
    }

    if (free_cluster_chain(cluster) != 0) return -1;
    if (read_cluster(location.cluster) != 0) return -1;
    g_cluster[location.index] = 0xE5U;
    return write_cluster(location.cluster);
}

int fat32_remove_directory(const char *path) {
    return fat32_remove_directory_internal(path, 0);
}

int fat32_remove_directory_recursive(const char *path) {
    return fat32_remove_directory_internal(path, 1);
}
