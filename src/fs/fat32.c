#include "fat32.h"
#include "fat32_int.h"
#include "string.h"
#include "heap.h"


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
    } else if (prepared.needs_alias != 0U &&
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
    if (prepared.needs_alias != 0U &&
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

