/*
 * Bronto Engine - stdout / stderr capture for POSIX systems (Linux, macOS,
 * BSD). Included by capture.c.
 *
 * Each stream's file descriptor is pointed at a pipe; a reader thread
 * waits on both pipes and hands what arrives to cap_push(), and (echo)
 * passes it on to the original descriptors, the terminal.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <unistd.h>

static pthread_mutex_t cap_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t cap_thread;
static int cap_saved[3] = { -1, -1, -1 };   /* the original stdout / stderr */
static int cap_pipe[3] = { -1, -1, -1 };    /* read ends */
static int cap_quit, cap_done;               /* (under cap_mutex) */
static bool cap_echo;

static void cap_lock(void) { pthread_mutex_lock(&cap_mutex); }
static void cap_unlock(void) { pthread_mutex_unlock(&cap_mutex); }

static int cap_flag(const int *flag) { int v; cap_lock(); v = *flag; cap_unlock(); return v; }
static void cap_set(int *flag, int v) { cap_lock(); *flag = v; cap_unlock(); }

static void cap_cloexec(int fd) { if (fd >= 0) fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC); }

static void cap_write_all(int fd, const char *p, size_t n) {
    while (n > 0) {
        ssize_t w = write(fd, p, n);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return;
        p += w;
        n -= (size_t)w;
    }
}

static void *cap_reader(void *arg) {
    char buf[CAP_CHUNK];
    int open_count = 2;
    BR_UNUSED(arg);
    while (open_count > 0 && !cap_flag(&cap_quit)) {
        struct pollfd fds[2];
        int i, n = 0, map[2];
        for (i = 1; i <= 2; ++i)
            if (cap_pipe[i] >= 0) { fds[n].fd = cap_pipe[i]; fds[n].events = POLLIN; fds[n].revents = 0; map[n++] = i; }
        if (poll(fds, (nfds_t)n, 100) <= 0) continue;          /* timeout: look at cap_quit again */
        for (i = 0; i < n; ++i) {
            ssize_t got;
            if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            got = read(fds[i].fd, buf, sizeof(buf));
            if (got > 0) {
                cap_push(map[i], buf, (size_t)got);
                if (cap_echo && cap_saved[map[i]] >= 0) cap_write_all(cap_saved[map[i]], buf, (size_t)got);
            } else if (got == 0 || (errno != EINTR && errno != EAGAIN)) {
                close(cap_pipe[map[i]]);                      /* every writer is gone */
                cap_pipe[map[i]] = -1;
                --open_count;
            }
        }
    }
    cap_set(&cap_done, 1);
    return NULL;
}

static void cap_undo(int upto) {
    int s;
    for (s = 1; s <= upto; ++s) {
        if (cap_saved[s] >= 0) { dup2(cap_saved[s], s); close(cap_saved[s]); cap_saved[s] = -1; }
        if (cap_pipe[s] >= 0) { close(cap_pipe[s]); cap_pipe[s] = -1; }
    }
}

static bool cap_os_start(bool echo) {
    int s;
    cap_echo = echo;
    cap_quit = cap_done = 0;
    for (s = 1; s <= 2; ++s) {
        int fds[2];
        if (pipe(fds) != 0) { cap_undo(s - 1); return false; }
        cap_saved[s] = dup(s);
        if (cap_saved[s] < 0 || dup2(fds[1], s) < 0) {
            close(fds[0]);
            close(fds[1]);
            if (cap_saved[s] >= 0) close(cap_saved[s]);
            cap_saved[s] = -1;
            cap_undo(s - 1);
            return false;
        }
        close(fds[1]);                                       /* the stream's descriptor is the writer now */
        cap_pipe[s] = fds[0];
        cap_cloexec(fds[0]);                                 /* programs we start don't inherit these */
        cap_cloexec(cap_saved[s]);
    }
    if (pthread_create(&cap_thread, NULL, cap_reader, NULL) != 0) { cap_undo(2); return false; }
    return true;
}

static void cap_os_stop(void) {
    int s, waited;
    for (s = 1; s <= 2; ++s)                                 /* closes the pipes' write ends */
        if (cap_saved[s] >= 0) { dup2(cap_saved[s], s); }
    /* The reader finishes on its own once it has read everything - unless a
     * program we started still holds a pipe; then stop waiting after 0.5 s. */
    for (waited = 0; !cap_flag(&cap_done) && waited < 50; ++waited) poll(NULL, 0, 10);
    cap_set(&cap_quit, 1);
    pthread_join(cap_thread, NULL);
    for (s = 1; s <= 2; ++s) {
        if (cap_saved[s] >= 0) { close(cap_saved[s]); cap_saved[s] = -1; }
        if (cap_pipe[s] >= 0) { close(cap_pipe[s]); cap_pipe[s] = -1; }
    }
}

static void cap_os_write_original(int stream, const char *text, size_t n) {
    if (stream < 1 || stream > 2) return;
    if (cap_saved[stream] >= 0) cap_write_all(cap_saved[stream], text, n);
}
