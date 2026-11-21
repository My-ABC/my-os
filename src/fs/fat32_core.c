#include "fat32_int.h"
#include "ata.h"
#include "serial.h"
#include "string.h"

uint8_t g_sector[512];
uint8_t g_cluster[32768];
struct fat32_fs g_fs;


uint16_t le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}


uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}


uint32_t fat32_cluster_limit(void) {
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


int read_cluster(uint32_t cluster) {
    if (cluster < 2U || cluster >= fat32_cluster_limit()) return -1;
    uint32_t lba = g_fs.first_data_lba +
                   (cluster - 2U) * g_fs.sectors_per_cluster;
    return ata_read_sectors(lba, g_fs.sectors_per_cluster, g_cluster);
}


int write_cluster(uint32_t cluster) {
    if (cluster < 2U || cluster >= fat32_cluster_limit()) return -1;
    uint32_t lba = g_fs.first_data_lba +
                   (cluster - 2U) * g_fs.sectors_per_cluster;
    return ata_write_sectors(lba, g_fs.sectors_per_cluster, g_cluster);
}


uint32_t fat_next_cluster(uint32_t cluster) {
    if (cluster < 2U || cluster >= fat32_cluster_limit()) return 0U;
    uint32_t fat_offset = cluster * 4U;
    uint32_t sector = g_fs.fat_start_lba + (fat_offset / 512U);
    if (read_sector(sector) != 0) return 0U;
    return le32(g_sector + (fat_offset % 512U)) & 0x0FFFFFFFU;
}


int fat_set_cluster(uint32_t cluster, uint32_t value) {
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


uint32_t allocate_cluster(void) {
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


int free_cluster_chain(uint32_t cluster) {
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

