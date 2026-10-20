/* Bronto web C library: <unistd.h> (the little that makes sense on the web) */
#ifndef BW_UNISTD_H
#define BW_UNISTD_H
#include "_bw_common.h"
BW_BEGIN
typedef long ssize_t;
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
ssize_t write(int fd, const void *p, size_t n);
int usleep(unsigned usec);
BW_END
#endif
