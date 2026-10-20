/* Bronto web C library: <ctype.h> (the "C" locale, ASCII) */
#ifndef BW_CTYPE_H
#define BW_CTYPE_H
#include "_bw_common.h"
BW_BEGIN
int isalnum(int c); int isalpha(int c); int isblank(int c); int iscntrl(int c);
int isdigit(int c); int isgraph(int c); int islower(int c); int isprint(int c);
int ispunct(int c); int isspace(int c); int isupper(int c); int isxdigit(int c);
int tolower(int c); int toupper(int c);
BW_END
#endif
