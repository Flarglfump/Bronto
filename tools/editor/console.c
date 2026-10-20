/*
 * BRONTO EDITOR - the console's lines: what the program writes to stdout
 * and stderr (captured, see bronto/capture.h), the engine's log, and
 * print() from scripts.
 *
 * Captured output arrives in arbitrary pieces; it is cut into lines here.
 * Lines in the engine's log format ("[WARN ] file.c:12: message") are read
 * as log entries with a level. A line repeated right away is counted
 * instead of stored again. No drawing (panels.c draws it); unit tested.
 */
#include "editor.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ed_console_clear(ed_console *c) {
    int s;
    c->first = c->count = 0;
    c->level_count[0] = c->level_count[1] = c->level_count[2] = 0;
    c->added++;
    for (s = 0; s < 3; ++s) c->partial_len[s] = 0;
}

const ed_con_line *ed_console_line(const ed_console *c, uint32_t i) {
    return i < c->count ? &c->lines[(c->first + i) % ED_CONSOLE_LINES] : NULL;
}

static int level_of(ed_con_kind k) {
    return k == ED_CON_ERROR ? ED_LEVEL_ERROR : k == ED_CON_WARN ? ED_LEVEL_WARN : ED_LEVEL_LOG;
}

static bool contains_word(const char *text, const char *word) {   /* case-insensitive */
    size_t n = strlen(word);
    for (; *text; ++text) {
        size_t i;
        for (i = 0; i < n && text[i] && tolower((unsigned char)text[i]) == word[i]; ++i) {}
        if (i == n) return true;
    }
    return false;
}

/* One finished line (no newline in it). */
static void add_line(ed_console *c, ed_con_kind kind, const char *text, size_t len, double now) {
    ed_con_line *l;
    int level = level_of(kind);
    char clean[ED_CONSOLE_TEXT];
    size_t i, n = 0;
    while (len > 0 && (text[len - 1] == '\r' || text[len - 1] == ' ')) --len;
    for (i = 0; i < len && n + 1 < sizeof(clean); ++i)           /* tabs to spaces, no control characters */
        clean[n++] = text[i] == '\t' ? ' ' : ((unsigned char)text[i] < 32 ? '?' : text[i]);
    clean[n] = 0;
    if (kind == ED_CON_ERR) {                                     /* raw stderr: guess how serious */
        if (contains_word(clean, "error") || contains_word(clean, "fail")) level = ED_LEVEL_ERROR;
        else if (contains_word(clean, "warn")) level = ED_LEVEL_WARN;
    }
    if (c->count > 0) {                                           /* the same again: count it */
        l = &c->lines[(c->first + c->count - 1) % ED_CONSOLE_LINES];
        if (l->kind == kind && strcmp(l->text, clean) == 0) {
            l->repeat++;
            l->time = now;
            c->level_count[l->level]++;
            c->added++;
            return;
        }
    }
    if (c->count == ED_CONSOLE_LINES) {                           /* full: the oldest goes */
        l = &c->lines[c->first];
        c->level_count[l->level] -= l->repeat;
        c->first = (c->first + 1) % ED_CONSOLE_LINES;
        c->count--;
    }
    l = &c->lines[(c->first + c->count) % ED_CONSOLE_LINES];
    memcpy(l->text, clean, n + 1);
    l->kind = (uint8_t)kind;
    l->level = (uint8_t)level;
    l->repeat = 1;
    l->time = now;
    c->count++;
    c->level_count[level]++;
    c->added++;
}

void ed_console_add(ed_console *c, ed_con_kind kind, const char *text, double now) {
    while (text) {
        const char *nl = strchr(text, '\n');
        size_t len = nl ? (size_t)(nl - text) : strlen(text);
        if (len > 0 || nl == NULL) add_line(c, kind, text, len, now);
        text = nl ? nl + 1 : NULL;
        if (text && !*text) break;
    }
}

/* "[WARN ] physics.c:40: message": a line of the engine's log. */
static bool parse_log(const char *text, size_t len, ed_con_kind *kind, const char **msg, size_t *msg_len) {
    static const struct { const char *tag; ed_con_kind kind; } k_levels[] = {
        { "[TRACE]", ED_CON_INFO }, { "[DEBUG]", ED_CON_INFO }, { "[INFO ]", ED_CON_INFO },
        { "[WARN ]", ED_CON_WARN }, { "[ERROR]", ED_CON_ERROR }, { "[FATAL]", ED_CON_ERROR },
    };
    size_t i;
    const char *p, *end = text + len;
    if (len < 8 || text[0] != '[') return false;
    for (i = 0; i < BR_ARRAY_COUNT(k_levels); ++i)
        if (strncmp(text, k_levels[i].tag, 7) == 0) break;
    if (i == BR_ARRAY_COUNT(k_levels)) return false;
    *kind = k_levels[i].kind;
    p = text + 8;                                                 /* skip "file.c:12: " */
    {
        const char *q = p;
        while (q < end && *q != ' ' && *q != ':') ++q;
        if (q < end && *q == ':') {
            ++q;
            while (q < end && isdigit((unsigned char)*q)) ++q;
            if (q + 1 < end && q[0] == ':' && q[1] == ' ') p = q + 2;
        }
    }
    if ((size_t)(end - p) > 8 && strncmp(p, "script: ", 8) == 0) p += 8;   /* "script: player.bs:3: ..." */
    *msg = p;
    *msg_len = (size_t)(end - p);
    return true;
}

static void finish_line(ed_console *c, int stream, const char *text, size_t len, double now) {
    ed_con_kind kind;
    const char *msg;
    size_t msg_len;
    if (stream == BR_STREAM_STDERR && parse_log(text, len, &kind, &msg, &msg_len)) add_line(c, kind, msg, msg_len, now);
    else add_line(c, stream == BR_STREAM_STDERR ? ED_CON_ERR : ED_CON_OUT, text, len, now);
}

void ed_console_feed(ed_console *c, int stream, const char *bytes, size_t n, double now) {
    size_t i;
    if (stream < 1 || stream > 2) return;
    for (i = 0; i < n; ++i) {
        char ch = bytes[i];
        if (ch == '\n') {
            finish_line(c, stream, c->partial[stream], c->partial_len[stream], now);
            c->partial_len[stream] = 0;
            continue;
        }
        if (c->partial_len[stream] == 0) c->partial_time[stream] = now;
        if (c->partial_len[stream] + 1 >= ED_CONSOLE_TEXT) {      /* very long: break it */
            finish_line(c, stream, c->partial[stream], c->partial_len[stream], now);
            c->partial_len[stream] = 0;
            c->partial_time[stream] = now;
        }
        c->partial[stream][c->partial_len[stream]++] = ch;
    }
}

void ed_console_flush(ed_console *c, double now, double max_wait) {
    int s;
    for (s = 1; s <= 2; ++s)
        if (c->partial_len[s] > 0 && now - c->partial_time[s] >= max_wait) {
            finish_line(c, s, c->partial[s], c->partial_len[s], now);
            c->partial_len[s] = 0;
        }
}

bool ed_console_location(const char *text, char *file, size_t size, int *line) {
    const char *p = text;
    while ((p = strstr(p, ".bs:")) != NULL) {
        const char *start = p, *digits = p + 4;
        if (isdigit((unsigned char)*digits)) {
            size_t len;
            while (start > text && !strchr(" \t'\"(<[", start[-1])) --start;
            len = (size_t)(p + 3 - start);
            if (len > 3 && len < size) {
                memcpy(file, start, len);
                file[len] = 0;
                *line = atoi(digits);
                return true;
            }
        }
        p += 4;
    }
    return false;
}

/* ---- the editor's side: what was captured since the last frame ----------------------------- */

void ed_console_poll(editor *ed) {
    char buf[4096];
    br_stream stream;
    size_t n;
    double now = ed->engine ? ed->engine->time : 0;
    int guard = 0;
    while (guard++ < 64 && (n = br_capture_read(&stream, buf, sizeof(buf))) > 0) ed_console_feed(&ed->console, (int)stream, buf, n, now);
    ed_console_flush(&ed->console, now, 0.25);
}
