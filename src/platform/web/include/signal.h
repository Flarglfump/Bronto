/* Bronto web C library: <signal.h> (nothing raises signals on the web) */
#ifndef BW_SIGNAL_H
#define BW_SIGNAL_H
typedef int sig_atomic_t;
typedef void (*__bw_sighandler)(int);
#define SIG_DFL ((__bw_sighandler)0)
#define SIG_IGN ((__bw_sighandler)1)
#define SIG_ERR ((__bw_sighandler)-1)
#define SIGABRT 6
#define SIGINT 2
#define SIGSEGV 11
#define SIGTERM 15
#define SIGFPE 8
#define SIGILL 4
__bw_sighandler signal(int sig, __bw_sighandler handler);
int raise(int sig);
#endif
