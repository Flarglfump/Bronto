/*
 * Bronto Engine - capturing the program's own standard output and error.
 *
 * After br_capture_start(), everything written to stdout and stderr - by
 * printf, by the engine's log, by libraries - goes into a buffer the
 * program reads back with br_capture_read(), for showing in its own window
 * (the editor's console). With `echo`, it still reaches the terminal too.
 *
 *   br_capture_start(true);
 *   ...each frame:
 *   while ((n = br_capture_read(&stream, buf, sizeof(buf))) > 0) show(stream, buf, n);
 *   ...
 *   br_capture_stop();
 *
 * The OS-specific part (pipes and a reader thread) lives in the platform
 * layer: src/platform/capture_posix.c and capture_win32.c. On the web there
 * is nothing to capture and br_capture_start() returns false.
 */
#ifndef BRONTO_CAPTURE_H
#define BRONTO_CAPTURE_H

#include "types.h"

typedef enum br_stream { BR_STREAM_STDOUT = 1, BR_STREAM_STDERR = 2 } br_stream;

/* Starts capturing both streams. False if it can't here (nothing changes). */
bool br_capture_start(bool echo);
/* Puts the streams back as they were. */
void br_capture_stop(void);
bool br_capture_active(void);
/* Flushes stdout and stderr, then copies out the next piece of captured
 * text: at most `size` bytes, all from one stream (*stream says which).
 * Returns 0 when there is nothing (yet). Not NUL-terminated. */
size_t br_capture_read(br_stream *stream, char *buffer, size_t size);
/* Bytes thrown away because the program wrote faster than it read. */
size_t br_capture_dropped(void);
/* Writes to the terminal (the stream as it was before the capture), so it
 * is not captured. Works when not capturing too. */
void br_capture_write_original(br_stream stream, const char *text, size_t length);

#endif /* BRONTO_CAPTURE_H */
