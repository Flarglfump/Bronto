/*
 * Bronto Engine - stdout / stderr capture for Windows. Included by capture.c.
 *
 * The C runtime's descriptors 1 and 2 are pointed at anonymous pipes
 * (_pipe / _dup2), and so are the process's standard handles, so output
 * from the CRT and from WriteFile(GetStdHandle(...)) is both caught. One
 * reader thread per pipe. A GUI program may start with no console at all;
 * then the streams are first opened on NUL, and echo has nowhere to go.
 */
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>

static CRITICAL_SECTION cap_cs;
static bool cap_cs_ready;
static HANDLE cap_threads[3];
static int cap_saved[3] = { -1, -1, -1 };    /* the original descriptors (-1: there were none) */
static int cap_pipe[3] = { -1, -1, -1 };     /* read ends */
static int cap_fd[3] = { -1, -1, -1 };       /* the streams' descriptors */
static HANDLE cap_saved_handle[3];
static bool cap_echo;

static void cap_lock(void) { EnterCriticalSection(&cap_cs); }
static void cap_unlock(void) { LeaveCriticalSection(&cap_cs); }

static FILE *cap_file(int s) { return s == 1 ? stdout : stderr; }
static DWORD cap_std(int s) { return s == 1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE; }

static DWORD WINAPI cap_reader(LPVOID arg) {
    int s = (int)(INT_PTR)arg, got;
    char buf[CAP_CHUNK];
    while ((got = _read(cap_pipe[s], buf, (unsigned)sizeof(buf))) > 0) {
        cap_push(s, buf, (size_t)got);
        if (cap_echo && cap_saved[s] >= 0) _write(cap_saved[s], buf, (unsigned)got);
    }
    return 0;
}

static void cap_restore(int s) {
    if (cap_fd[s] < 0) return;
    if (cap_saved[s] >= 0) {
        _dup2(cap_saved[s], cap_fd[s]);
    } else {
        int nul = _open("NUL", _O_WRONLY);
        if (nul >= 0) { _dup2(nul, cap_fd[s]); _close(nul); }
    }
    SetStdHandle(cap_std(s), cap_saved_handle[s]);
}

static void cap_os_stop(void);

static bool cap_os_start(bool echo) {
    int s;
    if (!cap_cs_ready) { InitializeCriticalSection(&cap_cs); cap_cs_ready = true; }
    cap_echo = echo;
    for (s = 1; s <= 2; ++s) {
        FILE *f = cap_file(s);
        int fds[2];
        cap_saved_handle[s] = GetStdHandle(cap_std(s));
        if (_fileno(f) < 0 && !freopen("NUL", "w", f)) goto fail;   /* no console: give the stream a descriptor */
        if (_fileno(f) < 0 || _pipe(fds, 65536, _O_BINARY | _O_NOINHERIT) != 0) goto fail;
        cap_saved[s] = _dup(_fileno(f));
        fflush(f);
        if (_dup2(fds[1], _fileno(f)) != 0) { _close(fds[0]); _close(fds[1]); goto fail; }
        cap_fd[s] = _fileno(f);
        _close(fds[1]);
        cap_pipe[s] = fds[0];
        SetStdHandle(cap_std(s), (HANDLE)_get_osfhandle(cap_fd[s]));
        cap_threads[s] = CreateThread(NULL, 0, cap_reader, (LPVOID)(INT_PTR)s, 0, NULL);
        if (!cap_threads[s]) goto fail;
    }
    return true;
fail:
    cap_os_stop();                                     /* undo whatever was done */
    return false;
}

static void cap_os_stop(void) {
    int s;
    for (s = 1; s <= 2; ++s) cap_restore(s);              /* closes the write ends: the readers see the end */
    for (s = 1; s <= 2; ++s) {
        if (cap_threads[s]) {
            WaitForSingleObject(cap_threads[s], 500);   /* (a program we started may still hold the pipe) */
            CloseHandle(cap_threads[s]);
            cap_threads[s] = NULL;
        }
        if (cap_pipe[s] >= 0) { _close(cap_pipe[s]); cap_pipe[s] = -1; }
        if (cap_saved[s] >= 0) { _close(cap_saved[s]); cap_saved[s] = -1; }
        cap_fd[s] = -1;
    }
}

static void cap_os_write_original(int stream, const char *text, size_t n) {
    if (stream < 1 || stream > 2) return;
    if (cap_saved[stream] >= 0) _write(cap_saved[stream], text, (unsigned)n);
}
