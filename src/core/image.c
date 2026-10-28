/* Image storage and dependency-free BMP / TGA codecs. */
#include "bronto/render.h"
#include "bronto/memory.h"
#include "bronto/log.h"

#include <string.h>
#include <ctype.h>

bool br_image_init(br_image *img, int width, int height) {
    img->width = img->height = 0;
    img->pixels = NULL;
    if (width <= 0 || height <= 0 || width > 32768 || height > 32768) return false;
    img->pixels = (br_color *)br_calloc((size_t)width * (size_t)height, sizeof(br_color));
    if (!img->pixels) return false;
    img->width = width;
    img->height = height;
    return true;
}

void br_image_destroy(br_image *img) {
    br_free(img->pixels);
    img->pixels = NULL;
    img->width = img->height = 0;
}

bool br_image_copy(br_image *dst, const br_image *src) {
    if (!br_image_init(dst, src->width, src->height)) return false;
    memcpy(dst->pixels, src->pixels, (size_t)src->width * (size_t)src->height * sizeof(br_color));
    return true;
}

void br_image_fill(br_image *img, br_color c) {
    size_t i, n = (size_t)img->width * (size_t)img->height;
    for (i = 0; i < n; ++i) img->pixels[i] = c;
}

br_color br_color_lerp(br_color a, br_color b, float t) {
    t = br_clampf(t, 0, 1);
    return br_rgba((uint8_t)(a.r + (b.r - a.r) * t + 0.5f), (uint8_t)(a.g + (b.g - a.g) * t + 0.5f),
                   (uint8_t)(a.b + (b.b - a.b) * t + 0.5f), (uint8_t)(a.a + (b.a - a.a) * t + 0.5f));
}

/* ---- little-endian readers ---------------------------------------------- */
typedef struct reader { const uint8_t *p; size_t size, pos; bool err; } reader;

static uint32_t rd_u8(reader *r) {
    if (r->pos + 1 > r->size) { r->err = true; return 0; }
    return r->p[r->pos++];
}
static uint32_t rd_u16(reader *r) { uint32_t a = rd_u8(r); return a | (rd_u8(r) << 8); }
static uint32_t rd_u32(reader *r) { uint32_t a = rd_u16(r); return a | (rd_u16(r) << 16); }

static int mask_shift(uint32_t mask) {
    int s = 0;
    if (!mask) return 0;
    while (!(mask & 1u)) { mask >>= 1; ++s; }
    return s;
}
static uint8_t extract(uint32_t px, uint32_t mask) {
    uint32_t v, max;
    if (!mask) return 0;
    v = (px & mask) >> mask_shift(mask);
    max = mask >> mask_shift(mask);
    return (uint8_t)(max == 255 ? v : (v * 255 + max / 2) / max);
}

/* ---- BMP -------------------------------------------------------------------- */
bool br_image_load_bmp_mem(br_image *img, const void *data, size_t size) {
    reader r;
    uint32_t data_offset, header_size, compression, colors_used;
    int32_t width, height;
    uint32_t bpp, rmask = 0, gmask = 0, bmask = 0, amask = 0;
    br_color palette[256];
    bool top_down, any_alpha = false;
    size_t row_stride;
    int x, y;

    memset(&r, 0, sizeof(r));
    r.p = (const uint8_t *)data;
    r.size = size;
    if (size < 54 || r.p[0] != 'B' || r.p[1] != 'M') return false;
    r.pos = 10;
    data_offset = rd_u32(&r);
    header_size = rd_u32(&r);
    if (header_size < 40) { BR_ERROR("BMP: unsupported header size %u", header_size); return false; }
    width = (int32_t)rd_u32(&r);
    height = (int32_t)rd_u32(&r);
    rd_u16(&r); /* planes */
    bpp = rd_u16(&r);
    compression = rd_u32(&r);
    rd_u32(&r); rd_u32(&r); rd_u32(&r); /* image size, ppm x, ppm y */
    colors_used = rd_u32(&r);
    rd_u32(&r); /* important colours */
    if (r.err || width <= 0 || height == 0) return false;
    top_down = height < 0;
    if (top_down) height = -height;

    if (compression == 3 || compression == 6) {  /* BI_BITFIELDS / BI_ALPHABITFIELDS */
        rmask = rd_u32(&r); gmask = rd_u32(&r); bmask = rd_u32(&r);
        if (header_size >= 56 || compression == 6) amask = rd_u32(&r);
    } else if (compression == 0) {
        if (bpp == 32) { rmask = 0x00FF0000u; gmask = 0x0000FF00u; bmask = 0x000000FFu; amask = 0xFF000000u; }
        if (bpp == 16) { rmask = 0x7C00u; gmask = 0x03E0u; bmask = 0x001Fu; }
    } else {
        BR_ERROR("BMP: compressed BMPs are not supported (compression %u)", compression);
        return false;
    }
    if (bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) {
        BR_ERROR("BMP: unsupported bit depth %u", bpp);
        return false;
    }
    if (bpp == 8) {
        uint32_t i, n = colors_used ? colors_used : 256;
        if (n > 256) return false;
        r.pos = 14 + header_size;
        for (i = 0; i < n; ++i) {
            uint8_t b = (uint8_t)rd_u8(&r), g = (uint8_t)rd_u8(&r), rr = (uint8_t)rd_u8(&r);
            rd_u8(&r);
            palette[i] = br_rgba(rr, g, b, 255);
        }
        for (; i < 256; ++i) palette[i] = br_rgba(0, 0, 0, 255);
    }
    if (r.err) return false;
    row_stride = (((size_t)width * bpp + 31u) / 32u) * 4u;
    if (data_offset > size || row_stride * (size_t)height > size - data_offset) {
        BR_ERROR("BMP: truncated file");
        return false;
    }
    if (!br_image_init(img, width, height)) return false;

    for (y = 0; y < height; ++y) {
        const uint8_t *row = r.p + data_offset + row_stride * (size_t)(top_down ? y : height - 1 - y);
        br_color *dst = img->pixels + (size_t)y * (size_t)width;
        for (x = 0; x < width; ++x) {
            br_color c;
            if (bpp == 8) {
                c = palette[row[x]];
            } else if (bpp == 24) {
                c = br_rgba(row[x * 3 + 2], row[x * 3 + 1], row[x * 3 + 0], 255);
            } else {
                uint32_t px = bpp == 32
                    ? (uint32_t)row[x * 4] | ((uint32_t)row[x * 4 + 1] << 8) |
                      ((uint32_t)row[x * 4 + 2] << 16) | ((uint32_t)row[x * 4 + 3] << 24)
                    : (uint32_t)row[x * 2] | ((uint32_t)row[x * 2 + 1] << 8);
                c = br_rgba(extract(px, rmask), extract(px, gmask), extract(px, bmask),
                            amask ? extract(px, amask) : 255);
                if (amask && c.a) any_alpha = true;
            }
            dst[x] = c;
        }
    }
    /* 32-bit BI_RGB files usually leave the 4th byte zero: treat as opaque. */
    if (bpp == 32 && amask && !any_alpha) {
        size_t i, n = (size_t)width * (size_t)height;
        for (i = 0; i < n; ++i) img->pixels[i].a = 255;
    }
    return true;
}

static void put_u16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put_u32(uint8_t *p, uint32_t v) { put_u16(p, v & 0xFFFFu); put_u16(p + 2, v >> 16); }

bool br_image_save_bmp(const br_image *img, const char *path) {
    /* BITMAPV4HEADER with BI_BITFIELDS so the alpha channel survives. */
    const uint32_t header = 14 + 108;
    size_t pixel_bytes = (size_t)img->width * (size_t)img->height * 4u;
    size_t total = header + pixel_bytes;
    uint8_t *buf;
    int x, y;
    bool ok;
    if (!img->pixels) return false;
    buf = (uint8_t *)br_calloc(1, total);
    if (!buf) return false;
    buf[0] = 'B'; buf[1] = 'M';
    put_u32(buf + 2, (uint32_t)total);
    put_u32(buf + 10, header);
    put_u32(buf + 14, 108);
    put_u32(buf + 18, (uint32_t)img->width);
    put_u32(buf + 22, (uint32_t)(-(int32_t)img->height));  /* top-down */
    put_u16(buf + 26, 1);
    put_u16(buf + 28, 32);
    put_u32(buf + 30, 3);               /* BI_BITFIELDS */
    put_u32(buf + 34, (uint32_t)pixel_bytes);
    put_u32(buf + 38, 2835);            /* 72 DPI */
    put_u32(buf + 42, 2835);
    put_u32(buf + 54, 0x00FF0000u);     /* R mask */
    put_u32(buf + 58, 0x0000FF00u);     /* G */
    put_u32(buf + 62, 0x000000FFu);     /* B */
    put_u32(buf + 66, 0xFF000000u);     /* A */
    put_u32(buf + 70, 0x73524742u);     /* 'sRGB' colour space */
    for (y = 0; y < img->height; ++y) {
        for (x = 0; x < img->width; ++x) {
            br_color c = img->pixels[(size_t)y * (size_t)img->width + (size_t)x];
            uint8_t *p = buf + header + ((size_t)y * (size_t)img->width + (size_t)x) * 4u;
            p[0] = c.b; p[1] = c.g; p[2] = c.r; p[3] = c.a;
        }
    }
    ok = br_file_write_all(path, buf, total);
    br_free(buf);
    return ok;
}

/* ---- TGA -------------------------------------------------------------------- */
bool br_image_load_tga_mem(br_image *img, const void *data, size_t size) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t id_len, cmap_type, type, cmap_len, cmap_entry;
    int width, height, bpp, desc, bytes;
    bool rle, top_down;
    size_t pos, n, i = 0;
    if (size < 18) return false;
    id_len = p[0]; cmap_type = p[1]; type = p[2];
    cmap_len = (uint32_t)p[5] | ((uint32_t)p[6] << 8);
    cmap_entry = p[7];
    width = p[12] | (p[13] << 8);
    height = p[14] | (p[15] << 8);
    bpp = p[16];
    desc = p[17];
    if (type != 2 && type != 10) { BR_ERROR("TGA: only true-colour images are supported"); return false; }
    if (bpp != 24 && bpp != 32) { BR_ERROR("TGA: unsupported bit depth %d", bpp); return false; }
    rle = type == 10;
    top_down = (desc & 0x20) != 0;
    bytes = bpp / 8;
    pos = 18 + id_len + (cmap_type ? cmap_len * ((cmap_entry + 7u) / 8u) : 0);
    if (!br_image_init(img, width, height)) return false;
    n = (size_t)width * (size_t)height;

#define TGA_READ_PIXEL(dst) do { \
        if (pos + (size_t)bytes > size) goto truncated; \
        (dst) = br_rgba(p[pos + 2], p[pos + 1], p[pos], bytes == 4 ? p[pos + 3] : 255); \
        pos += (size_t)bytes; } while (0)

    while (i < n) {
        if (!rle) {
            TGA_READ_PIXEL(img->pixels[i]);
            ++i;
        } else {
            uint32_t hdr, count, k;
            if (pos >= size) goto truncated;
            hdr = p[pos++];
            count = (hdr & 0x7Fu) + 1u;
            if (i + count > n) goto truncated;
            if (hdr & 0x80u) {
                br_color c;
                TGA_READ_PIXEL(c);
                for (k = 0; k < count; ++k) img->pixels[i++] = c;
            } else {
                for (k = 0; k < count; ++k) { TGA_READ_PIXEL(img->pixels[i]); ++i; }
            }
        }
    }
#undef TGA_READ_PIXEL
    if (!top_down) {  /* flip to top-row-first */
        int y;
        size_t row = (size_t)width;
        for (y = 0; y < height / 2; ++y) {
            br_color *a = img->pixels + (size_t)y * row;
            br_color *b = img->pixels + (size_t)(height - 1 - y) * row;
            size_t x;
            for (x = 0; x < row; ++x) { br_color t = a[x]; a[x] = b[x]; b[x] = t; }
        }
    }
    return true;
truncated:
    BR_ERROR("TGA: truncated file");
    br_image_destroy(img);
    return false;
}

bool br_image_save_tga(const br_image *img, const char *path) {
    size_t n = (size_t)img->width * (size_t)img->height, i;
    size_t total = 18 + n * 4;
    uint8_t *buf;
    bool ok;
    if (!img->pixels) return false;
    buf = (uint8_t *)br_calloc(1, total);
    if (!buf) return false;
    buf[2] = 2;
    put_u16(buf + 12, (uint32_t)img->width);
    put_u16(buf + 14, (uint32_t)img->height);
    buf[16] = 32;
    buf[17] = 0x28;  /* top-left origin, 8 alpha bits */
    for (i = 0; i < n; ++i) {
        br_color c = img->pixels[i];
        buf[18 + i * 4 + 0] = c.b;
        buf[18 + i * 4 + 1] = c.g;
        buf[18 + i * 4 + 2] = c.r;
        buf[18 + i * 4 + 3] = c.a;
    }
    ok = br_file_write_all(path, buf, total);
    br_free(buf);
    return ok;
}

/* ---- PNG (writing only) -------------------------------------------------------- */

static uint32_t png_crc_table[256];

static uint32_t png_crc(const uint8_t *p, size_t n, uint32_t crc) {
    size_t i;
    if (!png_crc_table[1]) {                          /* CRC-32, as PNG and zip use it */
        uint32_t c, k, b;
        for (k = 0; k < 256; ++k) {
            for (c = k, b = 0; b < 8; ++b) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            png_crc_table[k] = c;
        }
    }
    crc = ~crc;
    for (i = 0; i < n; ++i) crc = png_crc_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static void put_u32be(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

/* A chunk at `p`: length, type, data (already there, after the 8 header bytes), CRC. */
static size_t png_chunk(uint8_t *p, const char *type, size_t len) {
    put_u32be(p, (uint32_t)len);
    memcpy(p + 4, type, 4);
    put_u32be(p + 8 + len, png_crc(p + 4, len + 4, 0));
    return 12 + len;
}

void *br_image_encode_png(const br_image *img, size_t *out_size) {
    size_t w, h, row, raw, blocks, zlen, total, at = 0, i, done;
    uint8_t *buf, *z;
    uint32_t a = 1, b = 0;                            /* Adler-32 of the raw data */
    int y;
    if (out_size) *out_size = 0;
    if (!img->pixels || img->width < 1 || img->height < 1) return NULL;
    w = (size_t)img->width;
    h = (size_t)img->height;
    row = 1 + w * 4;                                  /* a filter byte (0: none), then RGBA */
    raw = row * h;
    blocks = (raw + 65534) / 65535;
    zlen = 2 + raw + blocks * 5 + 4;
    total = 8 + (12 + 13) + (12 + zlen) + 12;
    if (!(buf = (uint8_t *)br_alloc(total))) return NULL;
    memcpy(buf, "\x89PNG\r\n\x1a\n", 8);
    at = 8;
    put_u32be(buf + at + 8, (uint32_t)w);             /* IHDR: 8-bit RGBA, no interlace */
    put_u32be(buf + at + 12, (uint32_t)h);
    buf[at + 16] = 8; buf[at + 17] = 6; buf[at + 18] = 0; buf[at + 19] = 0; buf[at + 20] = 0;
    at += png_chunk(buf + at, "IHDR", 13);
    z = buf + at + 8;                                 /* IDAT: a zlib stream of stored blocks */
    z[0] = 0x78; z[1] = 0x01;
    {
        size_t zi = 2, left = raw, pos = 0;
        uint8_t *block = NULL;
        for (done = 0; done < blocks; ++done) {
            size_t n = left < 65535 ? left : 65535;
            z[zi] = (uint8_t)(left <= 65535);           /* the last block */
            z[zi + 1] = (uint8_t)(n & 0xFF); z[zi + 2] = (uint8_t)(n >> 8);
            z[zi + 3] = (uint8_t)(~n & 0xFF); z[zi + 4] = (uint8_t)((~n >> 8) & 0xFF);
            block = z + zi + 5;
            for (i = 0; i < n; ++i, ++pos) {           /* the image's bytes, row by row */
                size_t r = pos / row, c = pos % row;
                uint8_t v;
                y = (int)r;
                if (c == 0) v = 0;
                else {
                    const br_color px = img->pixels[(size_t)y * w + (c - 1) / 4];
                    size_t ch = (c - 1) % 4;
                    v = ch == 0 ? px.r : ch == 1 ? px.g : ch == 2 ? px.b : px.a;
                }
                block[i] = v;
                a = (a + v) % 65521u;
                b = (b + a) % 65521u;
            }
            zi += 5 + n;
            left -= n;
        }
        put_u32be(z + zi, (b << 16) | a);
    }
    at += png_chunk(buf + at, "IDAT", zlen);
    at += png_chunk(buf + at, "IEND", 0);
    if (out_size) *out_size = at;
    return buf;
}

bool br_image_save_png(const br_image *img, const char *path) {
    size_t size;
    void *png = br_image_encode_png(img, &size);
    bool ok = png && br_file_write_all(path, png, size);
    br_free(png);
    return ok;
}

/* ---- by extension ----------------------------------------------------------- */
static bool ext_is(const char *path, const char *ext) {
    const char *dot = strrchr(path, '.');
    size_t i;
    if (!dot) return false;
    ++dot;
    for (i = 0; ext[i]; ++i)
        if (!dot[i] || tolower((unsigned char)dot[i]) != ext[i]) return false;
    return dot[i] == '\0';
}

bool br_image_load(br_image *img, const char *path) {
    size_t size;
    void *data = br_file_read_all(path, &size);
    bool ok = false;
    if (!data) { BR_ERROR("could not read image '%s'", path); return false; }
    if (ext_is(path, "png")) ok = br_image_load_png_mem(img, data, size);
    else if (ext_is(path, "bmp")) ok = br_image_load_bmp_mem(img, data, size);
    else if (ext_is(path, "tga")) ok = br_image_load_tga_mem(img, data, size);
    else BR_ERROR("unsupported image format: '%s' (use .png, .bmp or .tga)", path);
    br_free(data);
    return ok;
}

/* ---- downscaling ------------------------------------------------------------ */

bool br_image_downscale(const br_image *src, br_image *dst, int width, int height) {
    int x, y;
    if (!src->pixels || width < 1 || height < 1 || !br_image_init(dst, width, height)) return false;
    for (y = 0; y < height; ++y) {
        int y0 = y * src->height / height, y1 = (y + 1) * src->height / height;
        if (y1 <= y0) y1 = y0 + 1;
        for (x = 0; x < width; ++x) {
            int x0 = x * src->width / width, x1 = (x + 1) * src->width / width, sx, sy;
            uint32_t r = 0, g = 0, b = 0, a = 0, n = 0;
            if (x1 <= x0) x1 = x0 + 1;
            for (sy = y0; sy < y1; ++sy)
                for (sx = x0; sx < x1; ++sx) {
                    br_color c = src->pixels[sy * src->width + sx];
                    r += (uint32_t)c.r * c.a; g += (uint32_t)c.g * c.a; b += (uint32_t)c.b * c.a;
                    a += c.a;
                    ++n;
                }
            {
                br_color *o = &dst->pixels[y * width + x];
                o->a = (uint8_t)((a + n / 2) / n);
                if (a == 0) a = 1;                       /* fully clear: r = g = b = 0 */
                o->r = (uint8_t)((r + a / 2) / a);
                o->g = (uint8_t)((g + a / 2) / a);
                o->b = (uint8_t)((b + a / 2) / a);
            }
        }
    }
    return true;
}
