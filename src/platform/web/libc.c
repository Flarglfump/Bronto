/*
 * Bronto web runtime - the C library: strings, characters, conversions,
 * sorting, time and exit.
 * (printf and files: stdio.c; memory: malloc.c; maths: math.c;
 * setjmp: sjlj.c; the program's start: start.c.)
 */
#include "bw.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int errno;

/* ==== <string.h> ========================================================== */
/* With -mbulk-memory these builtins become the memory.copy / memory.fill
 * instructions (not calls back into these functions). */
void *memcpy(void *restrict d, const void *restrict s, size_t n) { __builtin_memcpy(d, s, n); return d; }
void *memmove(void *d, const void *s, size_t n) { __builtin_memmove(d, s, n); return d; }
void *memset(void *d, int c, size_t n) { __builtin_memset(d, c, n); return d; }

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;
    for (; n; --n, ++x, ++y) if (*x != *y) return *x - *y;
    return 0;
}
void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char *)s;
    for (; n; --n, ++p) if (*p == (unsigned char)c) return (void *)p;
    return NULL;
}
size_t strlen(const char *s) { const char *p = s; while (*p) ++p; return (size_t)(p - s); }
size_t strnlen(const char *s, size_t max) { size_t n = 0; while (n < max && s[n]) ++n; return n; }
char *strcpy(char *restrict d, const char *restrict s) { char *r = d; while ((*d++ = *s++) != 0) {} return r; }
char *strncpy(char *restrict d, const char *restrict s, size_t n) {
    size_t i = 0;
    for (; i < n && s[i]; ++i) d[i] = s[i];
    for (; i < n; ++i) d[i] = 0;
    return d;
}
char *strcat(char *restrict d, const char *restrict s) { strcpy(d + strlen(d), s); return d; }
char *strncat(char *restrict d, const char *restrict s, size_t n) {
    char *e = d + strlen(d);
    size_t i = 0;
    for (; i < n && s[i]; ++i) e[i] = s[i];
    e[i] = 0;
    return d;
}
int strcmp(const char *a, const char *b) {
    for (; *a && *a == *b; ++a, ++b) {}
    return (unsigned char)*a - (unsigned char)*b;
}
int strncmp(const char *a, const char *b, size_t n) {
    for (; n && *a && *a == *b; --n, ++a, ++b) {}
    return n ? (unsigned char)*a - (unsigned char)*b : 0;
}
int strcoll(const char *a, const char *b) { return strcmp(a, b); }
char *strchr(const char *s, int c) {
    for (;; ++s) {
        if (*s == (char)c) return (char *)s;
        if (!*s) return NULL;
    }
}
char *strrchr(const char *s, int c) {
    const char *found = NULL;
    for (;; ++s) {
        if (*s == (char)c) found = s;
        if (!*s) return (char *)found;
    }
}
char *strstr(const char *h, const char *n) {
    size_t len = strlen(n);
    if (!len) return (char *)h;
    for (; *h; ++h) if (*h == *n && strncmp(h, n, len) == 0) return (char *)h;
    return NULL;
}
size_t strspn(const char *s, const char *accept) { size_t n = 0; while (s[n] && strchr(accept, s[n])) ++n; return n; }
size_t strcspn(const char *s, const char *reject) { size_t n = 0; while (s[n] && !strchr(reject, s[n])) ++n; return n; }
char *strpbrk(const char *s, const char *accept) { s += strcspn(s, accept); return *s ? (char *)s : NULL; }
char *strtok(char *restrict s, const char *restrict delim) {
    static char *next;
    char *start;
    if (!s) s = next;
    if (!s) return NULL;
    s += strspn(s, delim);
    if (!*s) { next = NULL; return NULL; }
    start = s;
    s += strcspn(s, delim);
    if (*s) { *s = 0; next = s + 1; } else next = NULL;
    return start;
}
char *strdup(const char *s) { size_t n = strlen(s) + 1; char *p = (char *)malloc(n); if (p) memcpy(p, s, n); return p; }
char *strndup(const char *s, size_t n) {
    size_t len = strnlen(s, n);
    char *p = (char *)malloc(len + 1);
    if (p) { memcpy(p, s, len); p[len] = 0; }
    return p;
}
int strcasecmp(const char *a, const char *b) {
    for (; *a && tolower((unsigned char)*a) == tolower((unsigned char)*b); ++a, ++b) {}
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}
int strncasecmp(const char *a, const char *b, size_t n) {
    for (; n && *a && tolower((unsigned char)*a) == tolower((unsigned char)*b); --n, ++a, ++b) {}
    return n ? tolower((unsigned char)*a) - tolower((unsigned char)*b) : 0;
}
char *strerror(int err) {
    switch (err) {
    case 0: return "Success";
    case EPERM: return "Operation not permitted";
    case ENOENT: return "No such file or directory";
    case EIO: return "Input/output error";
    case EBADF: return "Bad file descriptor";
    case ENOMEM: return "Out of memory";
    case EACCES: return "Permission denied";
    case EEXIST: return "File exists";
    case ENOTDIR: return "Not a directory";
    case EISDIR: return "Is a directory";
    case EINVAL: return "Invalid argument";
    case ENOSPC: return "No space left";
    case EDOM: return "Argument out of domain";
    case ERANGE: return "Result out of range";
    case ENOTEMPTY: return "Directory not empty";
    case ENOSYS: return "Not available on the web";
    default: return "Unknown error";
    }
}

/* ==== <ctype.h> =========================================================== */
int isdigit(int c) { return c >= '0' && c <= '9'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int isalpha(int c) { return islower(c) || isupper(c); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int isspace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
int isblank(int c) { return c == ' ' || c == '\t'; }
int iscntrl(int c) { return (c >= 0 && c < 32) || c == 127; }
int isprint(int c) { return c >= 32 && c < 127; }
int isgraph(int c) { return c > 32 && c < 127; }
int ispunct(int c) { return isgraph(c) && !isalnum(c); }
int tolower(int c) { return isupper(c) ? c + 32 : c; }
int toupper(int c) { return islower(c) ? c - 32 : c; }

/* ==== numbers from text =================================================== */
static int digit_value(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return 99;
}

/* The shared part of strtol and friends: magnitude in *mag, sign in *neg. */
static const char *scan_integer(const char *s, int base, unsigned long long limit, unsigned long long *mag,
                                bool *neg, bool *overflow) {
    const char *start = s, *digits;
    unsigned long long v = 0;
    *neg = false;
    *overflow = false;
    while (isspace((unsigned char)*s)) ++s;
    if (*s == '+' || *s == '-') *neg = *s++ == '-';
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X') && digit_value(s[2]) < 16) { s += 2; base = 16; }
    else if (base == 0) base = s[0] == '0' ? 8 : 10;
    if (base < 2 || base > 36) { errno = EINVAL; *mag = 0; return start; }
    digits = s;
    for (; digit_value(*s) < base; ++s) {
        unsigned d = (unsigned)digit_value(*s);
        if (v > (limit - d) / (unsigned)base) { *overflow = true; v = limit; }
        else if (!*overflow) v = v * (unsigned)base + d;
    }
    *mag = v;
    return s == digits ? start : s;
}

long long strtoll(const char *restrict s, char **restrict end, int base) {
    unsigned long long m;
    bool neg, over;
    const char *e = scan_integer(s, base, (unsigned long long)LLONG_MAX + 1, &m, &neg, &over);
    if (end) *end = (char *)e;
    if (over || (!neg && m > (unsigned long long)LLONG_MAX)) { errno = ERANGE; return neg ? LLONG_MIN : LLONG_MAX; }
    return neg ? (long long)(0 - m) : (long long)m;
}
unsigned long long strtoull(const char *restrict s, char **restrict end, int base) {
    unsigned long long m;
    bool neg, over;
    const char *e = scan_integer(s, base, ULLONG_MAX, &m, &neg, &over);
    if (end) *end = (char *)e;
    if (over) { errno = ERANGE; return ULLONG_MAX; }
    return neg ? 0 - m : m;
}
long strtol(const char *restrict s, char **restrict end, int base) {
    long long v = strtoll(s, end, base);
    if (v > LONG_MAX) { errno = ERANGE; return LONG_MAX; }
    if (v < LONG_MIN) { errno = ERANGE; return LONG_MIN; }
    return (long)v;
}
unsigned long strtoul(const char *restrict s, char **restrict end, int base) {
    unsigned long long m;
    bool neg, over;
    const char *e = scan_integer(s, base, ULONG_MAX, &m, &neg, &over);
    if (end) *end = (char *)e;
    if (over) { errno = ERANGE; return ULONG_MAX; }
    return neg ? (unsigned long)(0 - m) : (unsigned long)m;
}
int atoi(const char *s) { return (int)strtol(s, NULL, 10); }
long atol(const char *s) { return strtol(s, NULL, 10); }
long long atoll(const char *s) { return strtoll(s, NULL, 10); }

static bool match_word(const char *s, const char *word) {
    for (; *word; ++s, ++word) if (tolower((unsigned char)*s) != *word) return false;
    return true;
}

/* Finds where the number ends (C's rules), then lets JavaScript's Number()
 * do the correctly rounded conversion of that text. */
double strtod(const char *restrict s, char **restrict end) {
    const char *p = s, *num;
    bool neg = false, nonzero = false;
    double v;
    while (isspace((unsigned char)*p)) ++p;
    num = p;
    if (*p == '+' || *p == '-') neg = *p++ == '-';
    if (match_word(p, "inf")) {
        p += match_word(p, "infinity") ? 8 : 3;
        if (end) *end = (char *)p;
        return neg ? -HUGE_VAL : HUGE_VAL;
    }
    if (match_word(p, "nan")) {
        p += 3;
        if (*p == '(') { const char *q = p + 1; while (isalnum((unsigned char)*q) || *q == '_') ++q; if (*q == ')') p = q + 1; }
        if (end) *end = (char *)p;
        return neg ? -(double)NAN : (double)NAN;
    }
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && (isxdigit((unsigned char)p[2]) || (p[2] == '.' && isxdigit((unsigned char)p[3])))) {
        double m = 0;                      /* hexadecimal: 0x1.8p3 */
        int exp2 = 0;
        bool dot = false;
        p += 2;
        for (;; ++p) {
            if (*p == '.' && !dot) { dot = true; continue; }
            if (!isxdigit((unsigned char)*p)) break;
            if (m < 9007199254740992.0) { m = m * 16 + digit_value(*p); if (dot) exp2 -= 4; }
            else if (!dot) exp2 += 4;
        }
        if ((*p == 'p' || *p == 'P') && (isdigit((unsigned char)p[1]) || ((p[1] == '+' || p[1] == '-') && isdigit((unsigned char)p[2])))) {
            char *e2;
            exp2 += (int)strtol(p + 1, &e2, 10);
            p = e2;
        }
        if (end) *end = (char *)p;
        v = ldexp(m, exp2);
        return neg ? -v : v;
    }
    {
        const char *digits = p, *q;
        size_t count = 0;
        while (isdigit((unsigned char)*p)) { ++p; ++count; }
        if (*p == '.') { ++p; while (isdigit((unsigned char)*p)) { ++p; ++count; } }
        if (!count) { if (end) *end = (char *)s; return 0; }
        for (q = digits; q < p; ++q) if (*q >= '1' && *q <= '9') { nonzero = true; break; }
        if ((*p == 'e' || *p == 'E') && (isdigit((unsigned char)p[1]) || ((p[1] == '+' || p[1] == '-') && isdigit((unsigned char)p[2])))) {
            p += 2;
            while (isdigit((unsigned char)*p)) ++p;
        }
    }
    if (end) *end = (char *)p;
    v = bw_js_parse_double(num, (size_t)(p - num));
    if (isinf(v) || (nonzero && fabs(v) < 2.2250738585072014e-308)) errno = ERANGE;   /* overflow, or underflow */
    return v;
}
float strtof(const char *restrict s, char **restrict end) { return (float)strtod(s, end); }
long double strtold(const char *restrict s, char **restrict end) { return strtod(s, end); }
double atof(const char *s) { return strtod(s, NULL); }

/* ==== the rest of <stdlib.h> ============================================== */
int abs(int v) { return v < 0 ? -v : v; }
long labs(long v) { return v < 0 ? -v : v; }
long long llabs(long long v) { return v < 0 ? -v : v; }
div_t div(int a, int b) { div_t r; r.quot = a / b; r.rem = a % b; return r; }
ldiv_t ldiv(long a, long b) { ldiv_t r; r.quot = a / b; r.rem = a % b; return r; }
lldiv_t lldiv(long long a, long long b) { lldiv_t r; r.quot = a / b; r.rem = a % b; return r; }

static unsigned long long g_rand = 1;
void srand(unsigned seed) { g_rand = seed; }
int rand(void) {
    g_rand = g_rand * 6364136223846793005ULL + 1442695040888963407ULL;
    return (int)((g_rand >> 33) & RAND_MAX);
}

/* qsort: a merge sort (stable, as glibc's usually is), insertion sort for
 * short runs, and a plain insertion sort if there's no memory for the
 * merge buffer. */
static void swap_bytes(unsigned char *a, unsigned char *b, size_t n) {
    while (n--) { unsigned char t = *a; *a++ = *b; *b++ = t; }
}
static void insertion_sort(unsigned char *base, size_t count, size_t size, int (*cmp)(const void *, const void *)) {
    size_t i, j;
    for (i = 1; i < count; ++i)
        for (j = i; j > 0 && cmp(base + (j - 1) * size, base + j * size) > 0; --j)
            swap_bytes(base + (j - 1) * size, base + j * size, size);
}
static void merge_sort(unsigned char *base, unsigned char *tmp, size_t count, size_t size,
                       int (*cmp)(const void *, const void *)) {
    size_t half, i, j, k;
    if (count <= 12) { insertion_sort(base, count, size, cmp); return; }
    half = count / 2;
    merge_sort(base, tmp, half, size, cmp);
    merge_sort(base + half * size, tmp, count - half, size, cmp);
    if (cmp(base + (half - 1) * size, base + half * size) <= 0) return;   /* already in order */
    memcpy(tmp, base, half * size);
    i = 0; j = half; k = 0;
    while (i < half && j < count) {
        if (cmp(tmp + i * size, base + j * size) <= 0) { memcpy(base + k * size, tmp + i * size, size); ++i; }
        else { memmove(base + k * size, base + j * size, size); ++j; }
        ++k;
    }
    if (i < half) memcpy(base + k * size, tmp + i * size, (half - i) * size);
}
void qsort(void *base, size_t count, size_t size, int (*cmp)(const void *, const void *)) {
    unsigned char *tmp;
    if (count < 2 || !size) return;
    if (count <= 12 || !(tmp = (unsigned char *)malloc((count / 2 + 1) * size))) {
        insertion_sort((unsigned char *)base, count, size, cmp);
        return;
    }
    merge_sort((unsigned char *)base, tmp, count, size, cmp);
    free(tmp);
}
void *bsearch(const void *key, const void *base, size_t count, size_t size, int (*cmp)(const void *, const void *)) {
    const unsigned char *b = (const unsigned char *)base;
    while (count) {
        size_t mid = count / 2;
        int c = cmp(key, b + mid * size);
        if (c == 0) return (void *)(b + mid * size);
        if (c > 0) { b += (mid + 1) * size; count -= mid + 1; }
        else count = mid;
    }
    return NULL;
}

char *getenv(const char *name) { (void)name; return NULL; }   /* the web has no environment */
int system(const char *command) { return command ? -1 : 0; }  /* nor a shell */

/* ==== exit ================================================================ */
static void (*g_atexit[32])(void);
static int g_atexit_count;
int atexit(void (*fn)(void)) {
    if (g_atexit_count == 32) return -1;
    g_atexit[g_atexit_count++] = fn;
    return 0;
}
void _Exit(int status) {
    bw_js_exit(status);
    __builtin_trap();
}
void exit(int status) {
    while (g_atexit_count > 0) g_atexit[--g_atexit_count]();
    bw_stdio_flush_all();
    _Exit(status);
}
void abort(void) {
    bw_stdio_flush_all();
    fputs("abort()\n", stderr);
    bw_stdio_flush_all();
    __builtin_trap();
}
void __bw_assert_fail(const char *expr, const char *file, int line);
void __bw_assert_fail(const char *expr, const char *file, int line) {
    fprintf(stderr, "%s:%d: assertion failed: %s\n", file, line, expr);
    abort();
}

/* ==== odds and ends ======================================================= */
char *setlocale(int category, const char *locale) {
    (void)category;
    return (!locale || !locale[0] || strcmp(locale, "C") == 0 || strcmp(locale, "POSIX") == 0) ? (char *)"C" : NULL;
}
__bw_sighandler signal(int sig, __bw_sighandler handler) { (void)sig; (void)handler; return SIG_DFL; }
int raise(int sig) { if (sig == SIGABRT) abort(); return 0; }
ssize_t write(int fd, const void *p, size_t n) {
    if (fd != 1 && fd != 2) { errno = EBADF; return -1; }
    fflush(fd == 1 ? stdout : stderr);
    bw_js_write(fd, (const char *)p, n);
    return (ssize_t)n;
}
int usleep(unsigned usec) { (void)usec; return 0; }   /* nothing can wait on the browser's thread */

/* ==== <time.h> ============================================================ */
time_t time(time_t *t) {
    time_t now = (time_t)(bw_js_date_now() / 1000.0);
    if (t) *t = now;
    return now;
}
clock_t clock(void) { return (clock_t)(bw_js_now() * 1000.0); }
double difftime(time_t a, time_t b) { return (double)(a - b); }

/* Days since 1970-01-01 -> civil date (Howard Hinnant's algorithm). */
static void civil_from_days(long long z, int *y, int *m, int *d) {
    long long era, yoe, doy, mp;
    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    yoe = z - era * 146097;
    yoe = (yoe - yoe / 1460 + yoe / 36524 - yoe / 146096) / 365;
    doy = z - era * 146097 - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yoe + era * 400 + (*m <= 2));
}
static long long days_from_civil(long long y, int m, int d) {
    long long era, yoe, doy, doe;
    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
static struct tm *split_time(long long t, struct tm *tm) {
    long long days = t >= 0 ? t / 86400 : -((-t + 86399) / 86400), secs = t - days * 86400;
    int y, m, d;
    civil_from_days(days, &y, &m, &d);
    tm->tm_year = y - 1900;
    tm->tm_mon = m - 1;
    tm->tm_mday = d;
    tm->tm_hour = (int)(secs / 3600);
    tm->tm_min = (int)(secs / 60 % 60);
    tm->tm_sec = (int)(secs % 60);
    tm->tm_wday = (int)((days % 7 + 11) % 7);          /* 1970-01-01 was a Thursday */
    tm->tm_yday = (int)(days - days_from_civil(y, 1, 1));
    tm->tm_isdst = 0;
    return tm;
}
struct tm *gmtime(const time_t *t) { static struct tm tm; return split_time(*t, &tm); }
struct tm *localtime(const time_t *t) {
    static struct tm tm;
    return split_time(*t + (long long)bw_js_tz_offset((double)*t * 1000.0) * 60, &tm);
}
time_t mktime(struct tm *tm) {
    long long y = tm->tm_year + 1900LL, mon = tm->tm_mon, t, local;
    y += mon / 12; mon %= 12;
    if (mon < 0) { mon += 12; --y; }
    local = days_from_civil(y, (int)mon + 1, 1) * 86400 + (long long)(tm->tm_mday - 1) * 86400
          + tm->tm_hour * 3600LL + tm->tm_min * 60LL + tm->tm_sec;
    t = local - (long long)bw_js_tz_offset((double)local * 1000.0) * 60;
    split_time(local, tm);
    return t;
}
size_t strftime(char *restrict s, size_t max, const char *restrict fmt, const struct tm *restrict tm) {
    static const char *const days[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
    static const char *const months[] = { "January", "February", "March", "April", "May", "June", "July",
                                          "August", "September", "October", "November", "December" };
    size_t n = 0;
    char part[64];
    for (; *fmt; ++fmt) {
        size_t len;
        if (*fmt != '%') { part[0] = *fmt; part[1] = 0; }
        else switch (*++fmt) {
            case 'Y': snprintf(part, sizeof(part), "%d", tm->tm_year + 1900); break;
            case 'y': snprintf(part, sizeof(part), "%02d", (tm->tm_year + 1900) % 100); break;
            case 'm': snprintf(part, sizeof(part), "%02d", tm->tm_mon + 1); break;
            case 'd': snprintf(part, sizeof(part), "%02d", tm->tm_mday); break;
            case 'e': snprintf(part, sizeof(part), "%2d", tm->tm_mday); break;
            case 'H': snprintf(part, sizeof(part), "%02d", tm->tm_hour); break;
            case 'I': snprintf(part, sizeof(part), "%02d", tm->tm_hour % 12 ? tm->tm_hour % 12 : 12); break;
            case 'M': snprintf(part, sizeof(part), "%02d", tm->tm_min); break;
            case 'S': snprintf(part, sizeof(part), "%02d", tm->tm_sec); break;
            case 'p': snprintf(part, sizeof(part), "%s", tm->tm_hour < 12 ? "AM" : "PM"); break;
            case 'j': snprintf(part, sizeof(part), "%03d", tm->tm_yday + 1); break;
            case 'a': snprintf(part, sizeof(part), "%.3s", days[tm->tm_wday % 7]); break;
            case 'A': snprintf(part, sizeof(part), "%s", days[tm->tm_wday % 7]); break;
            case 'b': case 'h': snprintf(part, sizeof(part), "%.3s", months[tm->tm_mon % 12]); break;
            case 'B': snprintf(part, sizeof(part), "%s", months[tm->tm_mon % 12]); break;
            case 'F': snprintf(part, sizeof(part), "%d-%02d-%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday); break;
            case 'T': snprintf(part, sizeof(part), "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec); break;
            case 'D': case 'x': snprintf(part, sizeof(part), "%02d/%02d/%02d", tm->tm_mon + 1, tm->tm_mday, (tm->tm_year + 1900) % 100); break;
            case 'X': snprintf(part, sizeof(part), "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec); break;
            case 'c': snprintf(part, sizeof(part), "%.3s %.3s %2d %02d:%02d:%02d %d", days[tm->tm_wday % 7], months[tm->tm_mon % 12],
                               tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, tm->tm_year + 1900); break;
            case 'n': snprintf(part, sizeof(part), "\n"); break;
            case 't': snprintf(part, sizeof(part), "\t"); break;
            case '%': snprintf(part, sizeof(part), "%%"); break;
            case 0: --fmt; part[0] = 0; break;
            default: snprintf(part, sizeof(part), "%%%c", *fmt); break;
        }
        len = strlen(part);
        if (n + len >= max) return 0;
        memcpy(s + n, part, len);
        n += len;
    }
    if (n >= max) return 0;
    s[n] = 0;
    return n;
}

/* Memory for the page to fill (arguments, files). */
BW_EXPORT(bronto_alloc) void *bronto_alloc(size_t size);
BW_EXPORT(bronto_free) void bronto_free(void *p);
void *bronto_alloc(size_t size) { return malloc(size); }
void bronto_free(void *p) { free(p); }
