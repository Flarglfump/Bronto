/* Bronto web C library: <stdio.h>. Files live in an in-memory file system
 * (the page puts the game's pack there; what the game writes is kept in the
 * browser's local storage). stdout and stderr go to the browser console. */
#ifndef BW_STDIO_H
#define BW_STDIO_H
#include "_bw_common.h"
#include <stdarg.h>
BW_BEGIN
typedef struct __bw_file FILE;
typedef long long fpos_t;
extern FILE *const stdin;
extern FILE *const stdout;
extern FILE *const stderr;
#define stdin stdin
#define stdout stdout
#define stderr stderr
#define EOF (-1)
#define BUFSIZ 1024
#define FILENAME_MAX 1024
#define FOPEN_MAX 64
#define L_tmpnam 32
#define TMP_MAX 10000
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2

FILE  *fopen(const char *restrict path, const char *restrict mode);
FILE  *freopen(const char *restrict path, const char *restrict mode, FILE *restrict f);
int    fclose(FILE *f);
size_t fread(void *restrict p, size_t size, size_t count, FILE *restrict f);
size_t fwrite(const void *restrict p, size_t size, size_t count, FILE *restrict f);
int    fseek(FILE *f, long offset, int whence);
long   ftell(FILE *f);
int    fgetpos(FILE *restrict f, fpos_t *restrict pos);
int    fsetpos(FILE *f, const fpos_t *pos);
void   rewind(FILE *f);
int    fflush(FILE *f);
int    feof(FILE *f);
int    ferror(FILE *f);
void   clearerr(FILE *f);
int    setvbuf(FILE *restrict f, char *restrict buf, int mode, size_t size);
void   setbuf(FILE *restrict f, char *restrict buf);
int    fileno(FILE *f);
int    fgetc(FILE *f);
int    getc(FILE *f);
int    getchar(void);
int    ungetc(int c, FILE *f);
char  *fgets(char *restrict s, int n, FILE *restrict f);
int    fputc(int c, FILE *f);
int    putc(int c, FILE *f);
int    putchar(int c);
int    fputs(const char *restrict s, FILE *restrict f);
int    puts(const char *s);
void   perror(const char *s);
int    remove(const char *path);
int    rename(const char *from, const char *to);
FILE  *tmpfile(void);
char  *tmpnam(char *s);

int printf(const char *restrict fmt, ...) __attribute__((format(printf, 1, 2)));
int fprintf(FILE *restrict f, const char *restrict fmt, ...) __attribute__((format(printf, 2, 3)));
int sprintf(char *restrict s, const char *restrict fmt, ...) __attribute__((format(printf, 2, 3)));
int snprintf(char *restrict s, size_t n, const char *restrict fmt, ...) __attribute__((format(printf, 3, 4)));
int vprintf(const char *restrict fmt, va_list ap);
int vfprintf(FILE *restrict f, const char *restrict fmt, va_list ap);
int vsprintf(char *restrict s, const char *restrict fmt, va_list ap);
int vsnprintf(char *restrict s, size_t n, const char *restrict fmt, va_list ap);
int sscanf(const char *restrict s, const char *restrict fmt, ...);
int vsscanf(const char *restrict s, const char *restrict fmt, va_list ap);
BW_END
#endif
