/* PNG reading, and the DEFLATE / zlib decompressor under it (RFC 1950, 1951,
 * and the PNG specification). No libraries: a small Huffman decoder in the
 * style of zlib's "puff". */
#include "bronto/render.h"
#include "bronto/inflate.h"
#include "bronto/memory.h"
#include "bronto/log.h"

#include <string.h>

/* ---- inflate ------------------------------------------------------------------ */

typedef struct inf_state {
    const uint8_t *in;
    size_t in_size, in_pos;
    uint32_t bits;                 /* bit buffer */
    int bit_count;
    uint8_t *out;
    size_t out_size, out_cap, out_limit;
    bool fail;
} inf_state;

#define INF_MAXBITS 15
typedef struct inf_huff { short count[INF_MAXBITS + 1]; short symbol[288]; } inf_huff;

static int inf_bits(inf_state *s, int need) {
    uint32_t val = s->bits;
    while (s->bit_count < need) {
        if (s->in_pos >= s->in_size) { s->fail = true; return 0; }
        val |= (uint32_t)s->in[s->in_pos++] << s->bit_count;
        s->bit_count += 8;
    }
    s->bits = val >> need;
    s->bit_count -= need;
    return (int)(val & ((1u << need) - 1u));
}

static bool inf_put(inf_state *s, uint8_t b) {
    if (s->out_size == s->out_cap) {
        size_t cap = s->out_cap ? s->out_cap * 2 : 4096;
        uint8_t *n;
        if (s->out_limit && cap > s->out_limit) cap = s->out_limit;
        if (cap <= s->out_size) { s->fail = true; return false; }   /* more than the caller allows */
        n = (uint8_t *)br_realloc(s->out, cap);
        if (!n) { s->fail = true; return false; }
        s->out = n;
        s->out_cap = cap;
    }
    s->out[s->out_size++] = b;
    return true;
}

static int inf_decode(inf_state *s, const inf_huff *h) {
    int code = 0, first = 0, index = 0, len;
    for (len = 1; len <= INF_MAXBITS; ++len) {
        int count;
        code |= inf_bits(s, 1);
        if (s->fail) return -1;
        count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;                     /* ran out of codes */
}

/* Builds a decoding table from code lengths; false if they over-subscribe. */
static bool inf_build(inf_huff *h, const short *length, int n) {
    short offs[INF_MAXBITS + 1];
    int sym, len, left = 1;
    for (len = 0; len <= INF_MAXBITS; ++len) h->count[len] = 0;
    for (sym = 0; sym < n; ++sym) h->count[length[sym]]++;
    if (h->count[0] == n) return true;                         /* no codes: fine until used */
    for (len = 1; len <= INF_MAXBITS; ++len) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return false;
    }
    offs[1] = 0;
    for (len = 1; len < INF_MAXBITS; ++len) offs[len + 1] = (short)(offs[len] + h->count[len]);
    for (sym = 0; sym < n; ++sym)
        if (length[sym] != 0) h->symbol[offs[length[sym]]++] = (short)sym;
    return true;
}

static const short k_len_base[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const short k_len_extra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const short k_dist_base[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                                       1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const short k_dist_extra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static bool inf_codes(inf_state *s, const inf_huff *lencode, const inf_huff *distcode) {
    for (;;) {
        int sym = inf_decode(s, lencode);
        if (sym < 0 || s->fail) return false;
        if (sym < 256) { if (!inf_put(s, (uint8_t)sym)) return false; continue; }
        if (sym == 256) return true;
        sym -= 257;
        if (sym >= 29) return false;
        {
            int len = k_len_base[sym] + inf_bits(s, k_len_extra[sym]), dsym = inf_decode(s, distcode);
            size_t dist;
            if (dsym < 0 || dsym >= 30 || s->fail) return false;
            dist = (size_t)k_dist_base[dsym] + (size_t)inf_bits(s, k_dist_extra[dsym]);
            if (s->fail || dist > s->out_size) return false;      /* reaching back before the start */
            while (len-- > 0) if (!inf_put(s, s->out[s->out_size - dist])) return false;
        }
    }
}

static bool inf_stored(inf_state *s) {
    size_t len;
    s->bits = 0;                                    /* to a byte boundary */
    s->bit_count = 0;
    if (s->in_pos + 4 > s->in_size) return false;
    len = (size_t)s->in[s->in_pos] | ((size_t)s->in[s->in_pos + 1] << 8);
    if ((s->in[s->in_pos + 2] ^ 0xFF) != s->in[s->in_pos] || (s->in[s->in_pos + 3] ^ 0xFF) != s->in[s->in_pos + 1]) return false;
    s->in_pos += 4;
    if (s->in_pos + len > s->in_size) return false;
    while (len-- > 0) if (!inf_put(s, s->in[s->in_pos++])) return false;
    return true;
}

static bool inf_fixed(inf_state *s) {
    static inf_huff lencode, distcode;
    static bool ready;
    if (!ready) {
        short lengths[288];
        int sym;
        for (sym = 0; sym < 144; ++sym) lengths[sym] = 8;
        for (; sym < 256; ++sym) lengths[sym] = 9;
        for (; sym < 280; ++sym) lengths[sym] = 7;
        for (; sym < 288; ++sym) lengths[sym] = 8;
        inf_build(&lencode, lengths, 288);
        for (sym = 0; sym < 30; ++sym) lengths[sym] = 5;
        inf_build(&distcode, lengths, 30);
        ready = true;
    }
    return inf_codes(s, &lencode, &distcode);
}

static bool inf_dynamic(inf_state *s) {
    static const short order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    short lengths[320];
    inf_huff lencode, distcode;
    int nlen = inf_bits(s, 5) + 257, ndist = inf_bits(s, 5) + 1, ncode = inf_bits(s, 4) + 4, index;
    if (s->fail || nlen > 286 || ndist > 30) return false;
    for (index = 0; index < ncode; ++index) lengths[order[index]] = (short)inf_bits(s, 3);
    for (; index < 19; ++index) lengths[order[index]] = 0;
    if (s->fail || !inf_build(&lencode, lengths, 19)) return false;
    index = 0;
    while (index < nlen + ndist) {
        int sym = inf_decode(s, &lencode), len = 0, rep;
        if (sym < 0 || s->fail) return false;
        if (sym < 16) { lengths[index++] = (short)sym; continue; }
        if (sym == 16) {
            if (index == 0) return false;
            len = lengths[index - 1];
            rep = 3 + inf_bits(s, 2);
        } else if (sym == 17) {
            rep = 3 + inf_bits(s, 3);
        } else {
            rep = 11 + inf_bits(s, 7);
        }
        if (s->fail || index + rep > nlen + ndist) return false;
        while (rep-- > 0) lengths[index++] = (short)len;
    }
    if (lengths[256] == 0) return false;                       /* no end-of-block code */
    if (!inf_build(&lencode, lengths, nlen) || !inf_build(&distcode, lengths + nlen, ndist)) return false;
    return inf_codes(s, &lencode, &distcode);
}

void *br_inflate(const void *data, size_t size, size_t *out_size, size_t limit) {
    inf_state s;
    int last;
    memset(&s, 0, sizeof(s));
    s.in = (const uint8_t *)data;
    s.in_size = size;
    s.out_limit = limit;
    do {
        int type;
        last = inf_bits(&s, 1);
        type = inf_bits(&s, 2);
        if (s.fail) break;
        if (type == 0) { if (!inf_stored(&s)) s.fail = true; }
        else if (type == 1) { if (!inf_fixed(&s)) s.fail = true; }
        else if (type == 2) { if (!inf_dynamic(&s)) s.fail = true; }
        else s.fail = true;
    } while (!last && !s.fail);
    if (s.fail) { br_free(s.out); return NULL; }
    if (!s.out) s.out = (uint8_t *)br_alloc(1);                /* empty, but not NULL */
    if (out_size) *out_size = s.out_size;
    return s.out;
}

static uint32_t adler32(const uint8_t *p, size_t n) {
    uint32_t a = 1, b = 0;
    size_t i;
    for (i = 0; i < n; ++i) { a = (a + p[i]) % 65521u; b = (b + a) % 65521u; }
    return (b << 16) | a;
}

void *br_zlib_decompress(const void *data, size_t size, size_t *out_size, size_t limit) {
    const uint8_t *p = (const uint8_t *)data;
    uint8_t *out;
    size_t n;
    if (size < 6 || (p[0] & 0x0F) != 8 || (p[0] >> 4) > 7 || ((p[0] << 8) | p[1]) % 31 != 0 || (p[1] & 0x20)) return NULL;
    out = (uint8_t *)br_inflate(p + 2, size - 2, &n, limit);
    if (!out) return NULL;
    /* (the stream's end isn't known exactly here, so the checksum is found from the end) */
    {
        uint32_t want = ((uint32_t)p[size - 4] << 24) | ((uint32_t)p[size - 3] << 16) | ((uint32_t)p[size - 2] << 8) | p[size - 1];
        if (adler32(out, n) != want) { br_free(out); return NULL; }
    }
    if (out_size) *out_size = n;
    return out;
}

/* ---- PNG ----------------------------------------------------------------------- */

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

static uint8_t paeth(int a, int b, int c) {
    int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return (uint8_t)a;
    return (uint8_t)(pb <= pc ? b : c);
}

/* Undoes the filters of one (sub)image of w x h, in place. bpp: bytes per
 * pixel (at least 1), stride: bytes per row. False on a bad filter type. */
static bool unfilter(uint8_t *data, int h, size_t stride, int bpp) {
    int y;
    size_t x;
    uint8_t *prev = NULL;
    for (y = 0; y < h; ++y) {
        uint8_t *row = data + (size_t)y * (stride + 1);
        int f = row[0];
        uint8_t *cur = row + 1;
        for (x = 0; x < stride; ++x) {
            int a = x >= (size_t)bpp ? cur[x - (size_t)bpp] : 0, b = prev ? prev[x] : 0, c = prev && x >= (size_t)bpp ? prev[x - (size_t)bpp] : 0;
            switch (f) {
            case 0: break;
            case 1: cur[x] = (uint8_t)(cur[x] + a); break;
            case 2: cur[x] = (uint8_t)(cur[x] + b); break;
            case 3: cur[x] = (uint8_t)(cur[x] + ((a + b) >> 1)); break;
            case 4: cur[x] = (uint8_t)(cur[x] + paeth(a, b, c)); break;
            default: return false;
            }
        }
        prev = cur;
    }
    return true;
}

typedef struct png_info {
    int width, height, depth, ctype, interlace;
    br_color palette[256];
    int palette_count;
    bool has_key;
    uint16_t key[3];               /* tRNS for grey (key[0]) or RGB */
} png_info;

static int png_channels(int ctype) { return ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : 4; }

/* One sample (0..65535 scale kept as raw value) of row `row` at pixel x, channel c. */
static uint16_t png_sample(const uint8_t *row, int x, int c, int channels, int depth) {
    if (depth == 16) { const uint8_t *p = row + ((size_t)x * channels + c) * 2; return (uint16_t)((p[0] << 8) | p[1]); }
    if (depth == 8) return row[(size_t)x * channels + c];
    {   /* 1, 2, 4 bits: one channel (grey or palette) */
        size_t bit = (size_t)x * (size_t)depth;
        int shift = 8 - depth - (int)(bit & 7);
        return (uint16_t)((row[bit >> 3] >> shift) & ((1 << depth) - 1));
    }
}

static uint8_t to8(uint16_t v, int depth) {
    switch (depth) {
    case 16: return (uint8_t)(v >> 8);
    case 8: return (uint8_t)v;
    case 4: return (uint8_t)(v * 17);
    case 2: return (uint8_t)(v * 85);
    default: return (uint8_t)(v * 255);
    }
}

static br_color png_pixel(const png_info *in, const uint8_t *row, int x) {
    int ch = png_channels(in->ctype);
    br_color c;
    switch (in->ctype) {
    case 0: {
        uint16_t g = png_sample(row, x, 0, ch, in->depth);
        uint8_t v = to8(g, in->depth);
        c = br_rgba(v, v, v, in->has_key && g == in->key[0] ? 0 : 255);
        break;
    }
    case 2: {
        uint16_t r = png_sample(row, x, 0, ch, in->depth), g = png_sample(row, x, 1, ch, in->depth), b = png_sample(row, x, 2, ch, in->depth);
        bool clear = in->has_key && r == in->key[0] && g == in->key[1] && b == in->key[2];
        c = br_rgba(to8(r, in->depth), to8(g, in->depth), to8(b, in->depth), clear ? 0 : 255);
        break;
    }
    case 3: {
        uint16_t i = png_sample(row, x, 0, ch, in->depth);
        c = i < in->palette_count ? in->palette[i] : br_rgba(0, 0, 0, 255);
        break;
    }
    case 4: {
        uint8_t v = to8(png_sample(row, x, 0, ch, in->depth), in->depth);
        c = br_rgba(v, v, v, to8(png_sample(row, x, 1, ch, in->depth), in->depth));
        break;
    }
    default:
        c = br_rgba(to8(png_sample(row, x, 0, ch, in->depth), in->depth), to8(png_sample(row, x, 1, ch, in->depth), in->depth),
                    to8(png_sample(row, x, 2, ch, in->depth), in->depth), to8(png_sample(row, x, 3, ch, in->depth), in->depth));
        break;
    }
    return c;
}

/* Reads the filtered rows of a w x h (sub)image at `data` into the image, at
 * (x0 + x * dx, y0 + y * dy). Returns the bytes used, or 0 on a bad filter. */
static size_t png_pass(const png_info *in, br_image *img, uint8_t *data, size_t avail, int w, int h, int x0, int y0, int dx, int dy) {
    int ch = png_channels(in->ctype), bits = ch * in->depth, bpp = bits < 8 ? 1 : bits / 8, x, y;
    size_t stride = ((size_t)w * (size_t)bits + 7) / 8, need = (size_t)h * (stride + 1);
    if (w == 0 || h == 0) return 0;
    if (need > avail || !unfilter(data, h, stride, bpp)) return (size_t)-1;
    for (y = 0; y < h; ++y) {
        const uint8_t *row = data + (size_t)y * (stride + 1) + 1;
        for (x = 0; x < w; ++x) img->pixels[(size_t)(y0 + y * dy) * (size_t)img->width + (size_t)(x0 + x * dx)] = png_pixel(in, row, x);
    }
    return need;
}

bool br_image_load_png_mem(br_image *img, const void *data, size_t size) {
    const uint8_t *p = (const uint8_t *)data;
    png_info in;
    uint8_t *z = NULL, *raw;
    size_t at = 8, zlen = 0, zcap = 0, raw_size, used;
    bool have_header = false, done = false;
    memset(&in, 0, sizeof(in));
    img->pixels = NULL;
    img->width = img->height = 0;
    if (size < 8 || memcmp(p, "\x89PNG\r\n\x1a\n", 8) != 0) { BR_ERROR("png: not a PNG file"); return false; }
    while (!done && at + 12 <= size) {
        uint32_t len = be32(p + at);
        const uint8_t *type = p + at + 4, *body = p + at + 8;
        if (len > size - at - 12) { BR_ERROR("png: a chunk runs past the end of the file"); goto fail; }
        if (memcmp(type, "IHDR", 4) == 0) {
            if (len < 13) goto bad;
            in.width = (int)be32(body);
            in.height = (int)be32(body + 4);
            in.depth = body[8];
            in.ctype = body[9];
            in.interlace = body[12];
            if (body[10] != 0 || body[11] != 0 || in.interlace > 1 || in.width <= 0 || in.height <= 0 || in.width > 32768 || in.height > 32768) goto bad;
            if (!((in.ctype == 0 && (in.depth == 1 || in.depth == 2 || in.depth == 4 || in.depth == 8 || in.depth == 16)) ||
                  (in.ctype == 3 && (in.depth == 1 || in.depth == 2 || in.depth == 4 || in.depth == 8)) ||
                  ((in.ctype == 2 || in.ctype == 4 || in.ctype == 6) && (in.depth == 8 || in.depth == 16)))) goto bad;
            have_header = true;
        } else if (memcmp(type, "PLTE", 4) == 0) {
            uint32_t i;
            if (len % 3 || len > 768) goto bad;
            in.palette_count = (int)(len / 3);
            for (i = 0; i < len / 3; ++i) in.palette[i] = br_rgba(body[i * 3], body[i * 3 + 1], body[i * 3 + 2], 255);
        } else if (memcmp(type, "tRNS", 4) == 0) {
            uint32_t i;
            if (in.ctype == 3) for (i = 0; i < len && i < 256; ++i) in.palette[i].a = body[i];
            else if (in.ctype == 0 && len >= 2) { in.has_key = true; in.key[0] = (uint16_t)((body[0] << 8) | body[1]); }
            else if (in.ctype == 2 && len >= 6) {
                in.has_key = true;
                for (i = 0; i < 3; ++i) in.key[i] = (uint16_t)((body[i * 2] << 8) | body[i * 2 + 1]);
            }
        } else if (memcmp(type, "IDAT", 4) == 0) {
            if (zlen + len > zcap) {
                size_t cap = zcap ? zcap * 2 : 65536;
                uint8_t *n;
                while (cap < zlen + len) cap *= 2;
                if (!(n = (uint8_t *)br_realloc(z, cap))) goto fail;
                z = n;
                zcap = cap;
            }
            memcpy(z + zlen, body, len);
            zlen += len;
        } else if (memcmp(type, "IEND", 4) == 0) {
            done = true;
        } else if (!(type[0] & 0x20)) {                         /* a critical chunk we don't know */
            BR_ERROR("png: unknown critical chunk '%.4s'", (const char *)type);
            goto fail;
        }
        at += 12 + len;
    }
    if (!have_header || !zlen || (in.ctype == 3 && !in.palette_count)) goto bad;
    {   /* how many bytes the rows take, at most (a bound for the decompressor) */
        size_t row = ((size_t)in.width * (size_t)png_channels(in.ctype) * (size_t)in.depth + 7) / 8 + 1;
        size_t limit = row * (size_t)in.height * 2 + 1024;
        raw = (uint8_t *)br_zlib_decompress(z, zlen, &raw_size, limit);
    }
    br_free(z);
    z = NULL;
    if (!raw) { BR_ERROR("png: the image data is damaged"); return false; }
    if (!br_image_init(img, in.width, in.height)) { br_free(raw); return false; }
    if (!in.interlace) {
        used = png_pass(&in, img, raw, raw_size, in.width, in.height, 0, 0, 1, 1);
    } else {                                                       /* Adam7: seven passes */
        static const int sx[7] = { 0, 4, 0, 2, 0, 1, 0 }, sy[7] = { 0, 0, 4, 0, 2, 0, 1 };
        static const int dx[7] = { 8, 8, 4, 4, 2, 2, 1 }, dy[7] = { 8, 8, 8, 4, 4, 2, 2 };
        int k;
        used = 0;
        for (k = 0; k < 7; ++k) {
            int w = in.width > sx[k] ? (in.width - sx[k] + dx[k] - 1) / dx[k] : 0;
            int h = in.height > sy[k] ? (in.height - sy[k] + dy[k] - 1) / dy[k] : 0;
            size_t n = png_pass(&in, img, raw + used, raw_size - used, w, h, sx[k], sy[k], dx[k], dy[k]);
            if (n == (size_t)-1) { used = n; break; }
            used += n;
        }
    }
    br_free(raw);
    if (used == (size_t)-1) { BR_ERROR("png: the image data is damaged"); br_image_destroy(img); return false; }
    return true;
bad:
    BR_ERROR("png: unsupported or broken header");
fail:
    br_free(z);
    return false;
}
