/*
 * Bronto web pages (see webpage.h).
 */
#include "webpage.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define WP_PATH 1024

/* ---- a growing text buffer ---- */
typedef struct wp_buf { char *data; size_t size, cap; bool failed; } wp_buf;

static void wp_put(wp_buf *b, const char *s, size_t n) {
    if (b->failed) return;
    if (b->size + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 4096;
        char *grown;
        while (cap < b->size + n + 1) cap *= 2;
        if (!(grown = (char *)br_realloc(b->data, cap))) { b->failed = true; return; }
        b->data = grown;
        b->cap = cap;
    }
    memcpy(b->data + b->size, s, n);
    b->size += n;
    b->data[b->size] = 0;
}
static void wp_puts(wp_buf *b, const char *s) { wp_put(b, s, strlen(s)); }

static void wp_base64(wp_buf *b, const unsigned char *p, size_t n) {
    static const char k[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char chunk[4 * 1024 + 8];
    size_t i, c = 0, line = 0;
    for (i = 0; i + 2 < n; i += 3) {
        uint32_t v = ((uint32_t)p[i] << 16) | ((uint32_t)p[i + 1] << 8) | p[i + 2];
        chunk[c++] = k[v >> 18]; chunk[c++] = k[(v >> 12) & 63]; chunk[c++] = k[(v >> 6) & 63]; chunk[c++] = k[v & 63];
        if ((line += 4) >= 120) { chunk[c++] = '\n'; line = 0; }      /* lines, so editors cope with the page */
        if (c >= sizeof(chunk) - 8) { wp_put(b, chunk, c); c = 0; }
    }
    if (n - i == 1) {
        uint32_t v = (uint32_t)p[i] << 16;
        chunk[c++] = k[v >> 18]; chunk[c++] = k[(v >> 12) & 63]; chunk[c++] = '='; chunk[c++] = '=';
    } else if (n - i == 2) {
        uint32_t v = ((uint32_t)p[i] << 16) | ((uint32_t)p[i + 1] << 8);
        chunk[c++] = k[v >> 18]; chunk[c++] = k[(v >> 12) & 63]; chunk[c++] = k[(v >> 6) & 63]; chunk[c++] = '=';
    }
    wp_put(b, chunk, c);
}

static void wp_html(wp_buf *b, const char *s) {            /* text inside HTML (and its attributes) */
    for (; *s; ++s) {
        switch (*s) {
        case '&': wp_puts(b, "&amp;"); break;
        case '<': wp_puts(b, "&lt;"); break;
        case '>': wp_puts(b, "&gt;"); break;
        case '"': wp_puts(b, "&quot;"); break;
        default: wp_put(b, s, 1); break;
        }
    }
}

static void wp_json_string(wp_buf *b, const char *s) {     /* a JavaScript string, safe inside <script> */
    wp_puts(b, "\"");
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;
        char esc[8];
        if (c == '"' || c == '\\') { esc[0] = '\\'; esc[1] = (char)c; wp_put(b, esc, 2); }
        else if (c < 0x20 || c == '<' || c == '>' || c == '&') { snprintf(esc, sizeof(esc), "\\u%04x", c); wp_puts(b, esc); }
        else wp_put(b, s, 1);
    }
    wp_puts(b, "\"");
}

/* ---- finding the runtime ---- */
bool webpage_find_runtime(const char *dir, char *wasm, char *runtime_js, char *shell, size_t size) {
    static const char *const places[] = { "", "web", "players/web" };
    size_t i;
    for (i = 0; i < BR_ARRAY_COUNT(places); ++i) {
        char base[WP_PATH];
        if (places[i][0]) br_path_join(base, sizeof(base), dir, places[i]);
        else snprintf(base, sizeof(base), "%s", dir);
        br_path_join(wasm, size, base, "bronto_player.wasm");
        br_path_join(runtime_js, size, base, "bronto.js");
        br_path_join(shell, size, base, "shell.html");
        if (br_fs_exists(wasm) && br_fs_exists(runtime_js) && br_fs_exists(shell)) return true;
    }
    return false;
}

/* ---- packing a project ---- */
typedef struct wp_filter { const br_project *project; } wp_filter;

static bool wp_pack_filter(const char *rel, bool is_dir, void *user) {
    const wp_filter *f = (const wp_filter *)user;
    if (f->project && br_project_excludes(f->project, rel)) return false;
    if (is_dir && strcmp(rel, "build") == 0) return false;
    if (!is_dir && (br_path_has_ext(rel, ".brpak") || br_path_has_ext(rel, ".o") || br_path_has_ext(rel, ".tmp"))) return false;
    return true;
}

void *webpage_pack_project(const char *folder, const br_project *project, size_t *size, br_pack_stats *stats,
                           char *err, size_t err_size) {
    char tmp[WP_PATH];
    wp_filter f;
    void *data;
    static unsigned counter;
    f.project = project;
    *size = 0;
    /* br_pack_create writes a file: a hidden one in the project (packs leave hidden files out), read back */
    snprintf(tmp, sizeof(tmp), "%s/.bronto-web-%u.tmp", folder[0] ? folder : ".", ++counter);
    if (!br_pack_create(folder, tmp, false, wp_pack_filter, &f, stats, err, err_size)) { br_fs_remove(tmp); return NULL; }
    data = br_file_read_all(tmp, size);
    br_fs_remove(tmp);
    if (!data && err && err_size) snprintf(err, err_size, "can't read the pack back");
    return data;
}

/* ---- the page ---- */
static void wp_icon(wp_buf *b, const br_image *icon) {
    br_image small;
    const br_image *use = icon;
    void *png;
    size_t size;
    if (!icon || !icon->pixels) { wp_puts(b, "data:,"); return; }
    small.pixels = NULL;
    if (icon->width > 128 || icon->height > 128) {             /* the page only needs a small one */
        int w = icon->width >= icon->height ? 128 : (int)((long)icon->width * 128 / icon->height);
        int h = icon->height >= icon->width ? 128 : (int)((long)icon->height * 128 / icon->width);
        if (br_image_downscale(icon, &small, w < 1 ? 1 : w, h < 1 ? 1 : h)) use = &small;
    }
    if ((png = br_image_encode_png(use, &size)) != NULL) {
        wp_puts(b, "data:image/png;base64,");
        wp_base64(b, (const unsigned char *)png, size);
        br_free(png);
    } else {
        wp_puts(b, "data:,");
    }
    if (small.pixels) br_image_destroy(&small);
}

bool webpage_write(const webpage *page, const char *runtime_js_path, const char *shell_path, const char *out_path,
                   char *err, size_t err_size) {
    size_t js_size, shell_size;
    char *js = (char *)br_file_read_all(runtime_js_path, &js_size);
    char *shell = (char *)br_file_read_all(shell_path, &shell_size);
    const char *p, *title = page->title && page->title[0] ? page->title : "Bronto game";
    wp_buf b = { NULL, 0, 0, false };
    bool ok;
    if (err && err_size) err[0] = 0;
    if (!js || !shell) {
        if (err && err_size) snprintf(err, err_size, "can't read %s", !js ? runtime_js_path : shell_path);
        br_free(js); br_free(shell);
        return false;
    }
    if (strstr(js, "</script")) {                                 /* it goes inside a <script> */
        if (err && err_size) snprintf(err, err_size, "%s can't contain \"</script\"", runtime_js_path);
        br_free(js); br_free(shell);
        return false;
    }
    for (p = shell; *p;) {
        const char *open = strstr(p, "{{"), *close;
        char name[32];
        size_t n;
        if (!open || !(close = strstr(open + 2, "}}")) || (size_t)(close - open - 2) >= sizeof(name)) { wp_puts(&b, p); break; }
        wp_put(&b, p, (size_t)(open - p));
        n = (size_t)(close - open - 2);
        memcpy(name, open + 2, n);
        name[n] = 0;
        if (strcmp(name, "TITLE") == 0) wp_html(&b, title);
        else if (strcmp(name, "ICON") == 0) wp_icon(&b, page->icon);
        else if (strcmp(name, "RUNTIME") == 0) wp_puts(&b, js);
        else if (strcmp(name, "WASM") == 0) wp_base64(&b, (const unsigned char *)page->wasm, page->wasm_size);
        else if (strcmp(name, "PACK") == 0) { if (page->pack) wp_base64(&b, (const unsigned char *)page->pack, page->pack_size); }
        else if (strcmp(name, "REPLAY") == 0) { if (page->replay) wp_base64(&b, (const unsigned char *)page->replay, page->replay_size); }
        else if (strcmp(name, "ARGS") == 0) {
            int i;
            wp_puts(&b, "[");
            for (i = 0; i < page->arg_count; ++i) { if (i) wp_puts(&b, ", "); wp_json_string(&b, page->args[i]); }
            if (page->replay) wp_puts(&b, page->arg_count ? ", \"--replay\", \"replay.brec\"" : "\"--replay\", \"replay.brec\"");
            wp_puts(&b, "]");
        } else if (strcmp(name, "STORAGE_KEY") == 0) wp_json_string(&b, page->storage_key ? page->storage_key : title);
        else wp_put(&b, open, n + 4);                             /* not ours: leave it */
        p = close + 2;
    }
    ok = !b.failed && br_file_write_all(out_path, b.data, b.size);
    if (!ok && err && err_size) snprintf(err, err_size, b.failed ? "out of memory" : "can't write %s", out_path);
    br_free(b.data);
    br_free(js);
    br_free(shell);
    return ok;
}
