#ifndef _STDLIB_H
#define _STDLIB_H

#include "stddef.h"

// 内存管理
void* malloc(size_t size);
void* calloc(size_t num, size_t size);
void* realloc(void* ptr, size_t size);
void free(void* ptr);

// 堆管理
void heap_init(void);
void heap_check(void);
void heap_stats(uint32_t* total, uint32_t* used, uint32_t* free);

// 分页分配（新增）
void* page_alloc(size_t pages);
void page_free(void* ptr, size_t pages);
void* page_alloc_one(void);
void page_free_one(void* ptr);

// 字符串转换
int atoi(const char* str);
long atol(const char* str);
char* itoa(int value, char* str, int base);
char* utoa(unsigned int value, char* str, int base);

// 随机数
int rand(void);
void srand(unsigned int seed);
void srand_auto(void);

// 退出
void abort(void);
void exit(int status);
void system(const char* command);

#endif