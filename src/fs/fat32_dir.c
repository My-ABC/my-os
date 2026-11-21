#include "fat32.h"
#include "fat32_int.h"
#include "string.h"


int find_entry_in_cluster_chain(uint32_t cluster, const uint8_t name[11],
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


int find_entry_by_name(uint32_t cluster, const char *name,
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
                (has_long_name
                     ? strcmp(name, long_name) == 0
                     : has_short_name &&
                           memcmp(entry, short_name, 11U) == 0);
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


int update_directory_entry(const struct dir_entry_location *location,
                                  const uint8_t entry[32]) {
    if (read_cluster(location->cluster) != 0) return -1;
    memcpy(g_cluster + location->index, entry, 32U);
    return write_cluster(location->cluster);
}


static int mark_directory_entry_deleted(
    const struct dir_entry_location *location) {
    if (read_cluster(location->cluster) != 0) return -1;
    g_cluster[location->index] = 0xE5U;
    return write_cluster(location->cluster);
}


int mark_entry_and_lfn_deleted(
    const struct dir_entry_location *location,
    const struct dir_entry_location *lfn_locations, uint32_t lfn_count) {
    for (uint32_t i = 0U; i < lfn_count; ++i) {
        if (mark_directory_entry_deleted(&lfn_locations[i]) != 0) return -1;
    }
    return mark_directory_entry_deleted(location);
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


int insert_directory_entry(uint32_t dir_cluster,
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


int split_path(const char *path, char *parent, uint32_t parent_size,
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


uint32_t resolve_directory(const char *path) {
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


int directory_chain_contains(uint32_t start_cluster, uint32_t target) {
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


int update_parent_pointer(uint32_t dir_cluster, uint32_t parent_cluster) {
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


uint32_t entry_first_cluster(const uint8_t entry[32]) {
    return ((uint32_t)le16(entry + 20) << 16) | le16(entry + 26);
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
    if (prepared.needs_alias != 0U &&
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


int fat32_remove_directory(const char *path) {
    return fat32_remove_directory_internal(path, 0);
}


int fat32_remove_directory_recursive(const char *path) {
    return fat32_remove_directory_internal(path, 1);
}

