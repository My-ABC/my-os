#ifndef _FAT32_INT_H
#define _FAT32_INT_H

#include "stdint.h"

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

struct fat32_fs {
    uint32_t partition_lba;
    uint32_t fat_start_lba;
    uint32_t first_data_lba;
    uint32_t sectors_per_fat;
    uint32_t sectors_per_cluster;
    uint32_t cluster_size;
    uint32_t root_cluster;
    uint32_t total_sectors;
    uint8_t mounted;
};

extern uint8_t g_sector[512];
extern uint8_t g_cluster[32768];
extern struct fat32_fs g_fs;

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

struct prepared_name {
    uint8_t short_name[11];
    uint8_t utf16le[FAT32_LFN_BUFFER_BYTES];
    uint32_t utf16_length;
    uint32_t lfn_entries;
    uint8_t needs_alias;
};

uint16_t le16(const uint8_t *p);
uint32_t le32(const uint8_t *p);
uint32_t fat32_cluster_limit(void);
int read_cluster(uint32_t cluster);
int write_cluster(uint32_t cluster);
uint32_t fat_next_cluster(uint32_t cluster);
int fat_set_cluster(uint32_t cluster, uint32_t value);
uint32_t allocate_cluster(void);
int free_cluster_chain(uint32_t cluster);
void format_83(const uint8_t entry[11], char out[13]);
int name_to_83(const char *name, uint8_t out[11]);
int valid_lfn_name(const char *name);
uint8_t lfn_checksum(const uint8_t short_name[11]);
void lfn_reset(struct lfn_sequence *sequence);
void lfn_location_reset(struct lfn_location_sequence *sequence);
void lfn_add_entry(struct lfn_sequence *sequence,
                          const uint8_t entry[32],
                          const struct dir_entry_location *location);
void lfn_location_add_entry(
    struct lfn_location_sequence *sequence, const uint8_t entry[32],
    const struct dir_entry_location *location);
int lfn_locations_match(const struct lfn_location_sequence *sequence,
                               const uint8_t short_name[11]);
int lfn_decode(const struct lfn_sequence *sequence,
                      const uint8_t short_name[11],
                      char *name, uint32_t name_size);
int prepare_name(const char *name, struct prepared_name *prepared);
void make_lfn_entry(const struct prepared_name *name, uint32_t order,
                           uint8_t checksum, uint8_t entry[32]);
int make_short_alias(uint32_t dir_cluster, const char *name,
                            uint8_t short_name[11]);
int find_entry_in_cluster_chain(uint32_t cluster, const uint8_t name[11],
                                       struct dir_entry_location *location,
                                       uint8_t *entry_out);
int find_entry_by_name(uint32_t cluster, const char *name,
                              struct dir_entry_location *location,
                              uint8_t *entry_out,
                              struct dir_entry_location *lfn_locations,
                              uint32_t *lfn_count);
int mark_entry_and_lfn_deleted(
    const struct dir_entry_location *location,
    const struct dir_entry_location *lfn_locations, uint32_t lfn_count);
int insert_directory_entry(uint32_t dir_cluster,
                                  const struct prepared_name *prepared,
                                  const uint8_t entry[32]);
int update_directory_entry(const struct dir_entry_location *location,
                           const uint8_t entry[32]);
int split_path(const char *path, char *parent, uint32_t parent_size,
                      char *name, uint32_t name_size);
uint32_t resolve_directory(const char *path);
int directory_chain_contains(uint32_t start_cluster, uint32_t target);
int update_parent_pointer(uint32_t dir_cluster, uint32_t parent_cluster);
uint32_t entry_first_cluster(const uint8_t entry[32]);

#endif
