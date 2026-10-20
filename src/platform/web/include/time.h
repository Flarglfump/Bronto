/* Bronto web C library: <time.h> */
#ifndef BW_TIME_H
#define BW_TIME_H
#include "_bw_common.h"
BW_BEGIN
typedef long long time_t;
typedef long long clock_t;
#define CLOCKS_PER_SEC 1000000LL
struct tm { int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst; };
struct timespec { time_t tv_sec; long tv_nsec; };
time_t  time(time_t *t);
clock_t clock(void);
double  difftime(time_t a, time_t b);
struct tm *gmtime(const time_t *t);
struct tm *localtime(const time_t *t);
time_t  mktime(struct tm *tm);
size_t  strftime(char *restrict s, size_t max, const char *restrict fmt, const struct tm *restrict tm);
BW_END
#endif
