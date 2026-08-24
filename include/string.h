#ifndef _STRING_H
#define _STRING_H

#include "stddef.h"

int strcmp(const char *s1, const char *s2);
size_t strlen(const char *s1);
char *strchr(const char *s, int c);

char *strcpy(char *dest, const char *scr);
char *strcat(char *dest, const char *scr);
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);

#endif