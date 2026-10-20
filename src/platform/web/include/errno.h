/* Bronto web C library: <errno.h> */
#ifndef BW_ERRNO_H
#define BW_ERRNO_H
#include "_bw_common.h"
BW_BEGIN
extern int errno;
BW_END
#define EPERM 1
#define ENOENT 2
#define EIO 5
#define EBADF 9
#define ENOMEM 12
#define EACCES 13
#define EEXIST 17
#define ENOTDIR 20
#define EISDIR 21
#define EINVAL 22
#define ENOSPC 28
#define EDOM 33
#define ERANGE 34
#define ENOTEMPTY 39
#define ESPIPE 29
#define ENOSYS 38
#endif
