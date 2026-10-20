/* Bronto web C library: <stdlib.h> */
#ifndef BW_STDLIB_H
#define BW_STDLIB_H
#include "_bw_common.h"
BW_BEGIN
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 0x7fffffff
#define MB_CUR_MAX 4
typedef struct { int quot, rem; } div_t;
typedef struct { long quot, rem; } ldiv_t;
typedef struct { long long quot, rem; } lldiv_t;

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *p, size_t size);
void  free(void *p);
void *aligned_alloc(size_t align, size_t size);

BW_NORETURN void abort(void);
BW_NORETURN void exit(int status);
BW_NORETURN void _Exit(int status);
int   atexit(void (*fn)(void));
char *getenv(const char *name);
int   system(const char *command);

int       atoi(const char *s);
long      atol(const char *s);
long long atoll(const char *s);
double    atof(const char *s);
long               strtol(const char *restrict s, char **restrict end, int base);
unsigned long      strtoul(const char *restrict s, char **restrict end, int base);
long long          strtoll(const char *restrict s, char **restrict end, int base);
unsigned long long strtoull(const char *restrict s, char **restrict end, int base);
double             strtod(const char *restrict s, char **restrict end);
float              strtof(const char *restrict s, char **restrict end);
long double        strtold(const char *restrict s, char **restrict end);

int  rand(void);
void srand(unsigned seed);
void  qsort(void *base, size_t count, size_t size, int (*cmp)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t count, size_t size, int (*cmp)(const void *, const void *));

int abs(int v);
long labs(long v);
long long llabs(long long v);
div_t div(int a, int b);
ldiv_t ldiv(long a, long b);
lldiv_t lldiv(long long a, long long b);
BW_END
#endif
