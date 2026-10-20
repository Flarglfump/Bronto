/* Bronto web C library: <string.h> */
#ifndef BW_STRING_H
#define BW_STRING_H
#include "_bw_common.h"
BW_BEGIN
void  *memcpy(void *restrict d, const void *restrict s, size_t n);
void  *memmove(void *d, const void *s, size_t n);
void  *memset(void *d, int c, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
void  *memchr(const void *s, int c, size_t n);
size_t strlen(const char *s);
size_t strnlen(const char *s, size_t max);
char  *strcpy(char *restrict d, const char *restrict s);
char  *strncpy(char *restrict d, const char *restrict s, size_t n);
char  *strcat(char *restrict d, const char *restrict s);
char  *strncat(char *restrict d, const char *restrict s, size_t n);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
int    strcoll(const char *a, const char *b);
char  *strchr(const char *s, int c);
char  *strrchr(const char *s, int c);
char  *strstr(const char *h, const char *n);
size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);
char  *strpbrk(const char *s, const char *accept);
char  *strtok(char *restrict s, const char *restrict delim);
char  *strdup(const char *s);
char  *strndup(const char *s, size_t n);
char  *strerror(int err);
int    strcasecmp(const char *a, const char *b);
int    strncasecmp(const char *a, const char *b, size_t n);
BW_END
#endif
