/*
 * Bronto Engine - DEFLATE compression (see inflate.h).
 *
 * LZ77 over a 32 KB window (hash chains, one step of lazy matching), then each
 * block of up to 16K symbols is written the smallest of three ways: with its
 * own Huffman codes, with the fixed ones, or stored as it is. Not as tight as
 * zlib's best, but close for the things games carry (text, scenes, sounds,
 * uncompressed images), and plain C with nothing else needed.
 */
#include "bronto/inflate.h"
#include "bronto/memory.h"

#include <string.h>

#define DF_WSIZE 32768
#define DF_WMASK (DF_WSIZE - 1)
#define DF_HBITS 15
#define DF_HSIZE (1 << DF_HBITS)
#define DF_MAXLEN 258
#define DF_BLOCK 16384              /* symbols per block */

static const uint16_t k_lbase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t k_lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t k_dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537,
                                      2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const uint8_t k_dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
static const uint8_t k_clorder[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

/* ---- bits out (least significant first) ---- */

typedef struct df_out { uint8_t *p; size_t n, cap; uint32_t bits; int nbits; bool failed; } df_out;

static void df_byte(df_out *o, uint8_t b) {
    if (o->failed) return;
    if (o->n == o->cap) {
        size_t cap = o->cap ? o->cap * 2 : 4096;
        uint8_t *g = (uint8_t *)br_realloc(o->p, cap);
        if (!g) { o->failed = true; return; }
        o->p = g;
        o->cap = cap;
    }
    o->p[o->n++] = b;
}

static void df_bits(df_out *o, uint32_t v, int n) {
    o->bits |= v << o->nbits;
    o->nbits += n;
    while (o->nbits >= 8) { df_byte(o, (uint8_t)o->bits); o->bits >>= 8; o->nbits -= 8; }
}

static void df_align(df_out *o) {
    if (o->nbits > 0) df_byte(o, (uint8_t)o->bits);
    o->bits = 0;
    o->nbits = 0;
}

/* ---- Huffman code lengths, at most `limit` bits ---- */

/* Lengths for the symbols with a count (0 for the others). Too deep a tree:
 * halve the counts (keeping them above 0) and try again. */
static void df_lengths(const uint32_t *count_in, int n, int limit, uint8_t *len) {
    uint32_t count[288];
    int sym[288], used = 0, i;
    memcpy(count, count_in, sizeof(uint32_t) * (size_t)n);
    for (;;) {
        /* the two-queue build: leaves sorted by count, merged nodes in order */
        uint32_t w[2 * 288];
        int parent[2 * 288], leaves = 0, nodes, lq = 0, nq, nq_end, k, depth_max = 0;
        used = 0;
        for (i = 0; i < n; ++i) { len[i] = 0; if (count[i]) sym[used++] = i; }
        if (used == 0) return;
        if (used == 1) { len[sym[0]] = 1; return; }
        for (i = 1; i < used; ++i) {                          /* insertion sort by count (stable) */
            int s = sym[i], j = i - 1;
            while (j >= 0 && count[sym[j]] > count[s]) { sym[j + 1] = sym[j]; --j; }
            sym[j + 1] = s;
        }
        leaves = used;
        for (i = 0; i < leaves; ++i) w[i] = count[sym[i]];
        nodes = leaves;
        nq = nq_end = leaves;
        for (k = 0; k < leaves - 1; ++k) {                    /* take the two lightest, twice */
            int pick[2], t;
            for (t = 0; t < 2; ++t) {
                if (lq < leaves && (nq >= nq_end || w[lq] <= w[nq])) pick[t] = lq++;
                else pick[t] = nq++;
            }
            w[nodes] = w[pick[0]] + w[pick[1]];
            parent[pick[0]] = parent[pick[1]] = nodes;
            nodes++;
            nq_end = nodes;
        }
        {
            int depth[2 * 288];
            depth[nodes - 1] = 0;
            for (i = nodes - 2; i >= 0; --i) depth[i] = depth[parent[i]] + 1;   /* parents come after children */
            for (i = 0; i < leaves; ++i) {
                len[sym[i]] = (uint8_t)depth[i];
                if (depth[i] > depth_max) depth_max = depth[i];
            }
        }
        if (depth_max <= limit) return;
        for (i = 0; i < n; ++i) if (count[i]) count[i] = (count[i] >> 1) | 1;
    }
}

/* Canonical codes for lengths, bit-reversed for writing least significant first. */
static void df_codes(const uint8_t *len, int n, uint16_t *code) {
    int bl_count[16] = { 0 }, next[16], i, c = 0, b;
    for (i = 0; i < n; ++i) bl_count[len[i]]++;
    bl_count[0] = 0;
    for (b = 1; b < 16; ++b) { c = (c + bl_count[b - 1]) << 1; next[b] = c; }
    for (i = 0; i < n; ++i) {
        uint32_t v, r = 0;
        int k;
        if (!len[i]) { code[i] = 0; continue; }
        v = (uint32_t)next[len[i]]++;
        for (k = 0; k < len[i]; ++k) { r = (r << 1) | (v & 1); v >>= 1; }
        code[i] = (uint16_t)r;
    }
}

/* ---- a block ---- */

typedef struct df_sym { uint16_t lit; uint16_t dist; } df_sym;    /* dist 0: a literal (or 256: the end) */

static int df_lcode(int length) { int i = 28; while (k_lbase[i] > length) --i; return i; }
static int df_dcode(int dist) { int i = 29; while (k_dbase[i] > dist) --i; return i; }

/* The run-length coded lengths of both codes (16: repeat, 17 / 18: zeros). */
static int df_rle(const uint8_t *lens, int n, uint8_t *out_sym, uint8_t *out_extra) {
    int i = 0, k = 0;
    while (i < n) {
        int run = 1;
        while (i + run < n && lens[i + run] == lens[i]) ++run;
        if (lens[i] == 0 && run >= 3) {
            int r = run > 138 ? 138 : run;
            if (r >= 11) { out_sym[k] = 18; out_extra[k++] = (uint8_t)(r - 11); }
            else { out_sym[k] = 17; out_extra[k++] = (uint8_t)(r - 3); }
            i += r;
        } else if (lens[i] != 0 && run >= 4) {
            int r = run - 1 > 6 ? 6 : run - 1;
            out_sym[k] = lens[i]; out_extra[k++] = 0;
            out_sym[k] = 16; out_extra[k++] = (uint8_t)(r - 3);
            i += 1 + r;
        } else {
            out_sym[k] = lens[i]; out_extra[k++] = 0;
            ++i;
        }
    }
    return k;
}

static void df_write_syms(df_out *o, const df_sym *s, int n, const uint16_t *lc, const uint8_t *ll, const uint16_t *dc, const uint8_t *dl) {
    int i;
    for (i = 0; i < n; ++i) {
        if (!s[i].dist) { df_bits(o, lc[s[i].lit], ll[s[i].lit]); continue; }
        {
            int li = df_lcode(s[i].lit), di = df_dcode(s[i].dist);
            df_bits(o, lc[257 + li], ll[257 + li]);
            if (k_lext[li]) df_bits(o, (uint32_t)(s[i].lit - k_lbase[li]), k_lext[li]);
            df_bits(o, dc[di], dl[di]);
            if (k_dext[di]) df_bits(o, (uint32_t)(s[i].dist - k_dbase[di]), k_dext[di]);
        }
    }
}

static void df_block(df_out *o, const df_sym *s, int n, const uint8_t *raw, size_t raw_n, bool last) {
    uint32_t lf[286], dfq[30], cf[19];
    uint8_t ll[286], dl[30], cl[19], fll[288], fdl[30], all[286 + 30], rs[286 + 30], rx[286 + 30];
    uint16_t lc[286], dc[30], cc[19], flc[288], fdc[30];
    uint64_t extra = 0, dyn, fix, stored;
    int i, hlit = 257, hdist = 1, hclen = 4, nr, used_d = 0;
    memset(lf, 0, sizeof(lf));
    memset(dfq, 0, sizeof(dfq));
    for (i = 0; i < n; ++i) {
        if (!s[i].dist) { lf[s[i].lit]++; continue; }
        {
            int li = df_lcode(s[i].lit), di = df_dcode(s[i].dist);
            lf[257 + li]++;
            dfq[di]++;
            extra += k_lext[li] + k_dext[di];
        }
    }
    lf[256] = 1;
    for (i = 0; i < 30; ++i) if (dfq[i]) ++used_d;
    if (used_d < 2) { if (!dfq[0]) dfq[0] = 1; else dfq[1] = 1; }   /* (a complete distance code) */
    df_lengths(lf, 286, 15, ll);
    df_lengths(dfq, 30, 15, dl);
    for (i = 0; i < 286; ++i) if (ll[i]) hlit = i + 1;
    for (i = 0; i < 30; ++i) if (dl[i]) hdist = i + 1;
    if (hlit < 257) hlit = 257;
    memcpy(all, ll, (size_t)hlit);
    memcpy(all + hlit, dl, (size_t)hdist);
    nr = df_rle(all, hlit + hdist, rs, rx);
    memset(cf, 0, sizeof(cf));
    for (i = 0; i < nr; ++i) cf[rs[i]]++;
    df_lengths(cf, 19, 7, cl);
    for (i = 0; i < 19; ++i) if (cl[k_clorder[i]]) hclen = i + 1;
    /* what each way would cost, in bits */
    dyn = 3 + 14 + 3 * (uint64_t)hclen + extra;
    for (i = 0; i < nr; ++i) dyn += cl[rs[i]] + (rs[i] == 16 ? 2 : rs[i] == 17 ? 3 : rs[i] == 18 ? 7 : 0);
    for (i = 0; i < 286; ++i) dyn += (uint64_t)lf[i] * ll[i];
    for (i = 0; i < 30; ++i) if (dl[i]) dyn += (uint64_t)dfq[i] * dl[i];
    for (i = 0; i < 288; ++i) fll[i] = (uint8_t)(i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8);
    for (i = 0; i < 30; ++i) fdl[i] = 5;
    fix = 3 + extra;
    for (i = 0; i < 286; ++i) fix += (uint64_t)lf[i] * fll[i];
    for (i = 0; i < 30; ++i) if (dl[i]) fix += (uint64_t)dfq[i] * 5;
    stored = (raw_n / 65535 + 1) * 40 + 8 * (uint64_t)raw_n + 8;
    if (stored < dyn && stored < fix) {
        size_t at = 0;
        do {                                                   /* stored, 65535 bytes a piece */
            size_t piece = raw_n - at > 65535 ? 65535 : raw_n - at;
            bool fin = last && at + piece == raw_n;
            df_bits(o, fin ? 1u : 0u, 1);
            df_bits(o, 0, 2);
            df_align(o);
            df_byte(o, (uint8_t)piece); df_byte(o, (uint8_t)(piece >> 8));
            df_byte(o, (uint8_t)~piece); df_byte(o, (uint8_t)(~piece >> 8));
            for (i = 0; i < (int)piece; ++i) df_byte(o, raw[at + (size_t)i]);
            at += piece;
        } while (at < raw_n);
        return;
    }
    df_bits(o, last ? 1u : 0u, 1);
    if (fix <= dyn) {
        df_bits(o, 1, 2);
        df_codes(fll, 288, flc);
        df_codes(fdl, 30, fdc);
        df_write_syms(o, s, n, flc, fll, fdc, fdl);
        df_bits(o, flc[256], fll[256]);
        return;
    }
    df_bits(o, 2, 2);
    df_bits(o, (uint32_t)(hlit - 257), 5);
    df_bits(o, (uint32_t)(hdist - 1), 5);
    df_bits(o, (uint32_t)(hclen - 4), 4);
    for (i = 0; i < hclen; ++i) df_bits(o, cl[k_clorder[i]], 3);
    df_codes(cl, 19, cc);
    for (i = 0; i < nr; ++i) {
        df_bits(o, cc[rs[i]], cl[rs[i]]);
        if (rs[i] == 16) df_bits(o, rx[i], 2);
        else if (rs[i] == 17) df_bits(o, rx[i], 3);
        else if (rs[i] == 18) df_bits(o, rx[i], 7);
    }
    df_codes(ll, 286, lc);
    df_codes(dl, 30, dc);
    df_write_syms(o, s, n, lc, ll, dc, dl);
    df_bits(o, lc[256], ll[256]);
}

/* ---- LZ77 ---- */

typedef struct df_lz {
    const uint8_t *d;
    size_t size;
    int32_t *head, *prev;
    int chain;
} df_lz;

static uint32_t df_hash(const uint8_t *p) { return (((uint32_t)p[0] << 10) ^ ((uint32_t)p[1] << 5) ^ p[2]) & (DF_HSIZE - 1); }

static void df_insert(df_lz *z, size_t j) {
    uint32_t h;
    if (j + 2 >= z->size) return;
    h = df_hash(z->d + j);
    z->prev[j & DF_WMASK] = z->head[h];
    z->head[h] = (int32_t)j;
}

static int df_find(const df_lz *z, size_t i, int *dist) {
    int best = 2, chain = z->chain;
    size_t max = z->size - i > DF_MAXLEN ? DF_MAXLEN : z->size - i;
    int32_t cand;
    if (max < 3) return 0;
    cand = z->head[df_hash(z->d + i)];
    while (cand >= 0 && i - (size_t)cand <= DF_WSIZE && chain-- > 0) {
        const uint8_t *a = z->d + cand, *b = z->d + i;
        if (a[best] == b[best] && a[0] == b[0] && a[1] == b[1]) {
            size_t l = 2;
            while (l < max && a[l] == b[l]) ++l;
            if ((int)l > best) {
                best = (int)l;
                *dist = (int)(i - (size_t)cand);
                if (l >= max) break;
            }
        }
        {
            int32_t next = z->prev[cand & DF_WMASK];
            if (next >= cand) break;                              /* (an old link) */
            cand = next;
        }
    }
    return best >= 3 ? best : 0;
}

void *br_deflate(const void *data, size_t size, size_t *out_size, int level) {
    df_out o;
    df_lz z;
    df_sym *syms;
    int n = 0, nice = level >= 9 ? 258 : level >= 6 ? 128 : 32;
    size_t i = 0, block_start = 0;
    bool have = false;
    int plen = 0, pdist = 0;
    memset(&o, 0, sizeof(o));
    if (out_size) *out_size = 0;
    if (size == 0) {                                              /* an empty fixed block */
        df_bits(&o, 1, 1);
        df_bits(&o, 1, 2);
        df_bits(&o, 0, 7);
        df_align(&o);
        if (o.failed) { br_free(o.p); return NULL; }
        if (out_size) *out_size = o.n;
        return o.p ? o.p : br_alloc(1);
    }
    z.d = (const uint8_t *)data;
    z.size = size;
    z.chain = level <= 1 ? 8 : level <= 5 ? 64 : level <= 8 ? 256 : 1024;
    z.head = (int32_t *)br_alloc(sizeof(int32_t) * DF_HSIZE);
    z.prev = (int32_t *)br_alloc(sizeof(int32_t) * DF_WSIZE);
    syms = (df_sym *)br_alloc(sizeof(df_sym) * (DF_BLOCK + 2));
    if (!z.head || !z.prev || !syms) { br_free(z.head); br_free(z.prev); br_free(syms); return NULL; }
    memset(z.head, 0xff, sizeof(int32_t) * DF_HSIZE);
    memset(z.prev, 0xff, sizeof(int32_t) * DF_WSIZE);
#define DF_EMIT(L, D) do { syms[n].lit = (uint16_t)(L); syms[n].dist = (uint16_t)(D); ++n; } while (0)
#define DF_FLUSH(END, LAST) do { df_block(&o, syms, n, z.d + block_start, (END) - block_start, (LAST)); n = 0; block_start = (END); } while (0)
    while (i < size) {
        int dist = 0, len = df_find(&z, i, &dist);
        df_insert(&z, i);
        if (have) {
            if (len > plen && n < DF_BLOCK) {                     /* better one step on: the last byte alone */
                DF_EMIT(z.d[i - 1], 0);
                plen = len;
                pdist = dist;
                ++i;
            } else {
                size_t s = i - 1, j;
                DF_EMIT(plen, pdist);
                for (j = i + 1; j < s + (size_t)plen; ++j) df_insert(&z, j);
                i = s + (size_t)plen;
                have = false;
            }
        } else if (len >= nice) {
            size_t j;
            DF_EMIT(len, dist);
            for (j = i + 1; j < i + (size_t)len; ++j) df_insert(&z, j);
            i += (size_t)len;
        } else if (len >= 3) {
            have = true;
            plen = len;
            pdist = dist;
            ++i;
        } else {
            DF_EMIT(z.d[i], 0);
            ++i;
        }
        if (n >= DF_BLOCK && !have) DF_FLUSH(i, false);
    }
    if (have) { DF_EMIT(plen, pdist); have = false; }
    DF_FLUSH(size, true);
#undef DF_EMIT
#undef DF_FLUSH
    df_align(&o);
    br_free(z.head);
    br_free(z.prev);
    br_free(syms);
    if (o.failed) { br_free(o.p); return NULL; }
    if (out_size) *out_size = o.n;
    return o.p;
}

static uint32_t df_adler(const uint8_t *p, size_t n) {
    uint32_t a = 1, b = 0;
    size_t i;
    for (i = 0; i < n; ++i) { a = (a + p[i]) % 65521u; b = (b + a) % 65521u; }
    return (b << 16) | a;
}

void *br_zlib_compress(const void *data, size_t size, size_t *out_size, int level) {
    size_t n = 0;
    uint8_t *raw = (uint8_t *)br_deflate(data, size, &n, level), *out;
    uint32_t ad;
    if (!raw) return NULL;
    if (!(out = (uint8_t *)br_alloc(n + 6))) { br_free(raw); return NULL; }
    out[0] = 0x78;
    out[1] = 0xda;                                              /* (0x78da % 31 == 0) */
    memcpy(out + 2, raw, n);
    ad = df_adler((const uint8_t *)data, size);
    out[n + 2] = (uint8_t)(ad >> 24); out[n + 3] = (uint8_t)(ad >> 16); out[n + 4] = (uint8_t)(ad >> 8); out[n + 5] = (uint8_t)ad;
    br_free(raw);
    if (out_size) *out_size = n + 6;
    return out;
}
