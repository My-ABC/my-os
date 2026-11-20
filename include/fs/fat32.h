#ifndef _FAT32_H
#define _FAT32_H

#include "stdint.h"

#define FAT32_MAX_FILE_SIZE 0x100000U

typedef void (*fat32_list_callback_t)(const char *name, uint32_t attr, uint32_t size);

int fat32_init(void);
int fat32_mounted(void);
int fat32_list_directory(const char *path, fat32_list_callback_t callback);
int fat32_read_file(const char *path, void *buffer, uint32_t buffer_size, uint32_t *size);
int fat32_write_file(const char *path, const void *buffer, uint32_t size);
int fat32_delete_file(const char *path);
int fat32_make_directory(const char *path);
int fat32_remove_directory(const char *path);
int fat32_remove_directory_recursive(const char *path);

#endif
