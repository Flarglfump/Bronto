/* Bronto web C library: <locale.h> (only the "C" locale) */
#ifndef BW_LOCALE_H
#define BW_LOCALE_H
#include "_bw_common.h"
#define LC_ALL 0
#define LC_COLLATE 1
#define LC_CTYPE 2
#define LC_MONETARY 3
#define LC_NUMERIC 4
#define LC_TIME 5
BW_BEGIN
char *setlocale(int category, const char *locale);
BW_END
#endif
