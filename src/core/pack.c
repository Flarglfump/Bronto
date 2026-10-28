/*
 * Bronto Engine - pack files (see pack.h).
 */
#include "bronto/pack.h"
#include "bronto/fs.h"
#include "bronto/memory.h"
#include "bronto/log.h"
#include "bronto/inflate.h"

#include <stdio.h>
#include <string.h>

#define PK_TRAILER 32u                    /* magic 8, version 4, count 4, index offset 8, pack size 8 */

typedef struct pk_entry { char *name; uint64_t offset, size, stored; int method; } pk_entry;   /* method 1: DEFLATE */

static bool g_pk_compress = true;
void br_pack_set_compression(bool on) { g_pk_compress = on; }

static struct {
    bool mounted;
    char path[BR_PATH_MAX];
    uint64_t start;                       /* where the pack begins in the file */
    pk_entry *entries;
    uint32_t count;
} PK;

/* ---- little-endian numbers ---- */

static void pk_put(unsigned char *p, uint64_t v, int bytes) { int i; for (i = 0; i < bytes; ++i) p[i] = (unsigned char)(v >> (8 * i)); }
static uint64_t pk_get(const unsigned char *p, int bytes) {
    uint64_t v = 0;
    int i;
    for (i = bytes - 1; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

/* ---- writing ---- */

typedef struct pk_writer {
    FILE *f;
    uint64_t start, at;
    char **names;
    uint64_t *offsets, *sizes, *stored;
    uint8_t *methods;
    uint32_t count, cap;
    uint64_t unpacked;
    br_pack_filter filter;
    void *user;
    char *err;
    size_t err_size;
    bool failed;
} pk_writer;

static void pk_fail(pk_writer *w, const char *fmt, const char *what) {
    if (!w->failed && w->err && w->err_size) snprintf(w->err, w->err_size, fmt, what);
    w->failed = true;
}

/* Files that are compressed already: packing them again gains nothing. */
static bool pk_compressed_already(const char *rel) {
    static const char *const ext[] = { ".png", ".jpg", ".jpeg", ".gif", ".zip", ".gz", ".ogg", ".mp3", ".brpak", ".wasm.gz" };
    size_t i;
    for (i = 0; i < sizeof(ext) / sizeof(ext[0]); ++i) if (br_path_has_ext(rel, ext[i])) return true;
    return false;
}

static void pk_add_file(pk_writer *w, const char *full, const char *rel) {
    size_t size, n;
    void *data;
    if (w->failed) return;
    if (w->count == w->cap) {
        uint32_t cap = w->cap ? w->cap * 2 : 64;
        char **names = (char **)br_realloc(w->names, sizeof(char *) * cap);
        uint64_t *offs = names ? (uint64_t *)br_realloc(w->offsets, sizeof(uint64_t) * cap) : NULL;
        uint64_t *sizes = offs ? (uint64_t *)br_realloc(w->sizes, sizeof(uint64_t) * cap) : NULL;
        uint64_t *stored = sizes ? (uint64_t *)br_realloc(w->stored, sizeof(uint64_t) * cap) : NULL;
        uint8_t *methods = stored ? (uint8_t *)br_realloc(w->methods, cap) : NULL;
        if (names) w->names = names;
        if (offs) w->offsets = offs;
        if (sizes) w->sizes = sizes;
        if (stored) w->stored = stored;
        if (!methods) { pk_fail(w, "out of memory (%s)", rel); return; }
        w->methods = methods;
        w->cap = cap;
    }
    if (!(data = br_file_read_all(full, &size))) { pk_fail(w, "can't read %s", full); return; }
    n = strlen(rel);
    if (n > 0xFFFF || !(w->names[w->count] = (char *)br_alloc(n + 1))) { br_free(data); pk_fail(w, "can't add %s", rel); return; }
    memcpy(w->names[w->count], rel, n + 1);
    w->offsets[w->count] = w->at - w->start;
    w->sizes[w->count] = size;
    w->stored[w->count] = size;
    w->methods[w->count] = 0;
    w->unpacked += size;
    if (g_pk_compress && size >= 64 && !pk_compressed_already(rel)) {   /* smaller, if it gets much smaller */
        size_t packed = 0;
        void *z = br_deflate(data, size, &packed, 6);
        if (z && packed < size - size / 10) {
            br_free(data);
            data = z;
            w->stored[w->count] = packed;
            w->methods[w->count] = 1;
            size = packed;
        } else {
            br_free(z);
        }
    }
    w->count++;
    if (size && fwrite(data, 1, size, w->f) != size) pk_fail(w, "can't write %s", rel);
    w->at += size;
    br_free(data);
}

static void pk_walk(pk_writer *w, const char *dir, const char *rel_dir, int depth) {
    br_dir_list l;
    int i;
    if (w->failed || depth > 32 || !br_fs_list(dir, false, &l)) return;
    for (i = 0; i < l.count && !w->failed; ++i) {
        const br_dir_entry *e = &l.entries[i];
        char full[BR_PATH_MAX], rel[BR_PATH_MAX];
        br_path_join(full, sizeof(full), dir, e->name);
        if (rel_dir[0]) snprintf(rel, sizeof(rel), "%s/%s", rel_dir, e->name);
        else snprintf(rel, sizeof(rel), "%s", e->name);
        if (w->filter && !w->filter(rel, e->is_dir, w->user)) continue;
        if (e->is_dir) pk_walk(w, full, rel, depth + 1);
        else pk_add_file(w, full, rel);
    }
    br_dir_list_free(&l);
}

bool br_pack_create(const char *folder, const char *out_path, bool append, br_pack_filter filter, void *user,
                    br_pack_stats *stats, char *err, size_t err_size) {
    pk_writer w;
    unsigned char trailer[PK_TRAILER], head[25];
    uint64_t index_at;
    uint32_t i;
    memset(&w, 0, sizeof(w));
    w.filter = filter;
    w.user = user;
    w.err = err;
    w.err_size = err_size;
    if (err && err_size) err[0] = 0;
    if (!br_fs_is_dir(folder)) { pk_fail(&w, "%s isn't a folder", folder); return false; }
    if (!(w.f = fopen(out_path, append ? "ab" : "wb"))) { pk_fail(&w, "can't write %s", out_path); return false; }
    if (append) {                                        /* the pack starts where the file ended */
        long end;
        if (fseek(w.f, 0, SEEK_END) != 0 || (end = ftell(w.f)) < 0) { fclose(w.f); pk_fail(&w, "can't append to %s", out_path); return false; }
        w.start = w.at = (uint64_t)end;
    }
    pk_walk(&w, folder, "", 0);
    index_at = w.at - w.start;
    for (i = 0; i < w.count && !w.failed; ++i) {        /* the index: name length, name, offset, size, stored size, method */
        size_t n = strlen(w.names[i]);
        pk_put(head, n, 2);
        if (fwrite(head, 1, 2, w.f) != 2 || fwrite(w.names[i], 1, n, w.f) != n) pk_fail(&w, "can't write %s", out_path);
        pk_put(head, w.offsets[i], 8);
        pk_put(head + 8, w.sizes[i], 8);
        pk_put(head + 16, w.stored[i], 8);
        head[24] = w.methods[i];
        if (fwrite(head, 1, 25, w.f) != 25) pk_fail(&w, "can't write %s", out_path);
        w.at += 2 + n + 25;
    }
    memcpy(trailer, BR_PACK_MAGIC, 8);
    pk_put(trailer + 8, BR_PACK_VERSION, 4);
    pk_put(trailer + 12, w.count, 4);
    pk_put(trailer + 16, index_at, 8);
    pk_put(trailer + 24, w.at + PK_TRAILER - w.start, 8);
    if (!w.failed && fwrite(trailer, 1, PK_TRAILER, w.f) != PK_TRAILER) pk_fail(&w, "can't write %s", out_path);
    if (fclose(w.f) != 0) pk_fail(&w, "can't write %s", out_path);
    if (stats) { stats->files = w.count; stats->bytes = index_at; stats->unpacked = w.unpacked; }
    for (i = 0; i < w.count; ++i) br_free(w.names[i]);
    br_free(w.names);
    br_free(w.offsets);
    br_free(w.sizes);
    br_free(w.stored);
    br_free(w.methods);
    return !w.failed;
}

/* ---- reading ---- */

static void *pk_reader(const char *path, size_t *size);

static bool pk_relative(const char *p) { return p[0] && p[0] != '/' && p[0] != '\\' && !(p[1] == ':'); }

static const pk_entry *pk_find(const char *rel) {
    uint32_t i;
    while (rel[0] == '.' && (rel[1] == '/' || rel[1] == '\\')) rel += 2;
    for (i = 0; i < PK.count; ++i) {
        const char *a = PK.entries[i].name, *b = rel;
        while (*a && (*a == *b || (*a == '/' && *b == '\\'))) { ++a; ++b; }
        if (!*a && !*b) return &PK.entries[i];
    }
    return NULL;
}

void br_pack_unmount(void) {
    uint32_t i;
    for (i = 0; i < PK.count; ++i) br_free(PK.entries[i].name);
    br_free(PK.entries);
    memset(&PK, 0, sizeof(PK));
    br_file_set_reader(NULL);
}

bool br_pack_mount(const char *path) {
    FILE *f;
    long end;
    unsigned char trailer[PK_TRAILER];
    unsigned char *index = NULL;
    uint64_t pack_size, index_at, index_size, p = 0;
    uint32_t count, i, version, tail;
    br_pack_unmount();
    if (!(f = fopen(path, "rb"))) return false;
    if (fseek(f, 0, SEEK_END) != 0 || (end = ftell(f)) < (long)PK_TRAILER ||
        fseek(f, end - (long)PK_TRAILER, SEEK_SET) != 0 || fread(trailer, 1, PK_TRAILER, f) != PK_TRAILER ||
        memcmp(trailer, BR_PACK_MAGIC, 8) != 0) {
        fclose(f);
        return false;                                     /* no pack here */
    }
    count = (uint32_t)pk_get(trailer + 12, 4);
    index_at = pk_get(trailer + 16, 8);
    pack_size = pk_get(trailer + 24, 8);
    version = (uint32_t)pk_get(trailer + 8, 4);
    if ((version != 1 && version != BR_PACK_VERSION) || pack_size > (uint64_t)end || index_at + PK_TRAILER > pack_size) {
        fclose(f);
        BR_WARN("%s: a pack this version can't read", path);
        return false;
    }
    PK.start = (uint64_t)end - pack_size;
    index_size = pack_size - PK_TRAILER - index_at;
    PK.entries = count ? (pk_entry *)br_alloc(sizeof(pk_entry) * count) : NULL;
    index = (unsigned char *)br_alloc((size_t)index_size + 1);
    if ((count && !PK.entries) || !index || fseek(f, (long)(PK.start + index_at), SEEK_SET) != 0 ||
        fread(index, 1, (size_t)index_size, f) != (size_t)index_size) {
        fclose(f);
        br_free(index);
        br_pack_unmount();
        return false;
    }
    fclose(f);
    tail = version == 1 ? 16 : 25;                        /* (version 1: no stored size or method) */
    for (i = 0; i < count; ++i) {
        uint64_t n;
        if (p + 2 > index_size || (n = pk_get(index + p, 2), p + 2 + n + tail > index_size)) break;
        if (!(PK.entries[i].name = (char *)br_alloc((size_t)n + 1))) break;
        memcpy(PK.entries[i].name, index + p + 2, (size_t)n);
        PK.entries[i].name[n] = 0;
        PK.entries[i].offset = pk_get(index + p + 2 + n, 8);
        PK.entries[i].size = pk_get(index + p + 2 + n + 8, 8);
        PK.entries[i].stored = version == 1 ? PK.entries[i].size : pk_get(index + p + 2 + n + 16, 8);
        PK.entries[i].method = version == 1 ? 0 : index[p + 2 + n + 24];
        PK.count = i + 1;
        p += 2 + n + tail;
    }
    br_free(index);
    if (PK.count != count) { br_pack_unmount(); BR_WARN("%s: the pack's index is damaged", path); return false; }
    snprintf(PK.path, sizeof(PK.path), "%s", path);
    PK.mounted = true;
    br_file_set_reader(pk_reader);                        /* br_file_read_all looks here first */
    return true;
}

bool br_pack_mounted(void) { return PK.mounted; }
bool br_pack_contains(const char *rel) { return PK.mounted && rel && pk_find(rel) != NULL; }
uint32_t br_pack_file_count(void) { return PK.count; }
const char *br_pack_file_name(uint32_t i) { return i < PK.count ? PK.entries[i].name : NULL; }

void *br_pack_read(const char *rel, size_t *size) {
    const pk_entry *e;
    FILE *f;
    char *buf;
    if (size) *size = 0;
    if (!PK.mounted || !rel || !(e = pk_find(rel))) return NULL;
    if (!(f = fopen(PK.path, "rb"))) return NULL;
    buf = (char *)br_alloc((size_t)e->stored + 1);
    if (!buf || fseek(f, (long)(PK.start + e->offset), SEEK_SET) != 0 ||
        (e->stored && fread(buf, 1, (size_t)e->stored, f) != (size_t)e->stored)) {
        br_free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    if (e->method == 1) {                                 /* compressed: inflate it */
        size_t n = 0;
        char *raw = (char *)br_inflate(buf, (size_t)e->stored, &n, (size_t)e->size);
        char *out = raw && n == e->size ? (char *)br_alloc((size_t)e->size + 1) : NULL;
        br_free(buf);
        if (!out) { br_free(raw); BR_WARN("pack: %s is damaged", rel); return NULL; }
        if (n) memcpy(out, raw, n);
        br_free(raw);
        buf = out;
    } else if (e->method != 0) {
        br_free(buf);
        return NULL;
    }
    buf[e->size] = 0;
    if (size) *size = (size_t)e->size;
    return buf;
}

static void *pk_reader(const char *path, size_t *size) {
    return pk_relative(path) ? br_pack_read(path, size) : NULL;
}
