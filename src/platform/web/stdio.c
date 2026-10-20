/*
 * Bronto web runtime - <stdio.h>: files (over memfs.c), the console
 * (stdout and stderr go to the page, which shows them in the browser's
 * console), printf and scanf.
 */
#include "bw.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { F_FILE, F_STDIN, F_STDOUT, F_STDERR };

struct __bw_file {
    int kind;
    bw_node *node;
    size_t pos;
    bool can_read, can_write, append, eof, error;
    int unget;                     /* -1: none */
    char buf[1024];                /* console: the line being written */
    size_t buffered;
    int mode;                      /* _IOLBF / _IOFBF / _IONBF (console) */
    struct __bw_file *next_open;
};

static FILE g_stdin = { F_STDIN, NULL, 0, true, false, false, true, false, -1, { 0 }, 0, _IONBF, NULL };
static FILE g_stdout = { F_STDOUT, NULL, 0, false, true, false, false, false, -1, { 0 }, 0, _IOLBF, NULL };
static FILE g_stderr = { F_STDERR, NULL, 0, false, true, false, false, false, -1, { 0 }, 0, _IOLBF, NULL };
FILE *const stdin = &g_stdin;
FILE *const stdout = &g_stdout;
FILE *const stderr = &g_stderr;
static FILE *g_open;               /* every fopen'ed file, for flushing at exit */

/* ==== the console ========================================================= */
static void console_flush(FILE *f) {
    if (f->buffered) bw_js_write(f->kind == F_STDERR ? 2 : 1, f->buf, f->buffered);
    f->buffered = 0;
}
static size_t console_write(FILE *f, const char *p, size_t n) {
    size_t i;
    if (f->mode == _IONBF) { console_flush(f); bw_js_write(f->kind == F_STDERR ? 2 : 1, p, n); return n; }
    for (i = 0; i < n; ++i) {
        f->buf[f->buffered++] = p[i];
        if (f->buffered == sizeof(f->buf) || (p[i] == '\n' && f->mode == _IOLBF)) console_flush(f);
    }
    return n;
}

void bw_stdio_flush_all(void) {
    FILE *f;
    console_flush(stdout);
    console_flush(stderr);
    for (f = g_open; f; f = f->next_open) if (f->node) bw_fs_save(f->node);
}

/* ==== files =============================================================== */
FILE *fopen(const char *restrict path, const char *restrict mode) {
    bw_node *n;
    FILE *f;
    bool plus = strchr(mode, '+') != NULL;
    int err = 0;
    if (!path || !path[0]) { errno = ENOENT; return NULL; }
    if (mode[0] == 'r') {
        if (!(n = bw_fs_find(path))) { errno = ENOENT; return NULL; }
        if (n->is_dir && plus) { errno = EISDIR; return NULL; }
    } else if (mode[0] == 'w') {
        if (!(n = bw_fs_create_file(path, &err))) { errno = err; return NULL; }
    } else if (mode[0] == 'a') {
        n = bw_fs_find(path);
        if (!n && !(n = bw_fs_create_file(path, &err))) { errno = err; return NULL; }
        if (n->is_dir) { errno = EISDIR; return NULL; }
    } else { errno = EINVAL; return NULL; }
    if (mode[0] == 'w' && strchr(mode, 'x') && n->size) { errno = EEXIST; return NULL; }
    if (!(f = (FILE *)calloc(1, sizeof(*f)))) { errno = ENOMEM; return NULL; }
    f->kind = F_FILE;
    f->node = n;
    f->can_read = mode[0] == 'r' || plus;
    f->can_write = mode[0] != 'r' || plus;
    f->append = mode[0] == 'a';
    f->unget = -1;
    f->mode = _IOFBF;
    n->open_count++;
    f->next_open = g_open;
    g_open = f;
    return f;
}

int fclose(FILE *f) {
    FILE **p;
    if (!f) return EOF;
    if (f->kind != F_FILE) { fflush(f); return 0; }
    for (p = &g_open; *p; p = &(*p)->next_open) if (*p == f) { *p = f->next_open; break; }
    bw_fs_save(f->node);
    bw_fs_release(f->node);
    free(f);
    return 0;
}

FILE *freopen(const char *restrict path, const char *restrict mode, FILE *restrict f) {
    if (f && f->kind == F_FILE) fclose(f);
    return path ? fopen(path, mode) : NULL;
}

size_t fread(void *restrict p, size_t size, size_t count, FILE *restrict f) {
    size_t want, got = 0;
    unsigned char *out = (unsigned char *)p;
    if (!size || !count) return 0;
    if (!f->can_read) { f->error = true; errno = EBADF; return 0; }
    want = size * count;
    if (f->unget >= 0) { out[got++] = (unsigned char)f->unget; f->unget = -1; }
    if (f->kind == F_STDIN) { f->eof = true; return got / size; }
    if (f->node->is_dir) { f->error = true; errno = EISDIR; return 0; }
    if (f->pos < f->node->size) {
        size_t n = f->node->size - f->pos;
        if (n > want - got) n = want - got;
        memcpy(out + got, f->node->data + f->pos, n);
        f->pos += n;
        got += n;
    }
    if (got < want) f->eof = true;
    return got / size;
}

size_t fwrite(const void *restrict p, size_t size, size_t count, FILE *restrict f) {
    size_t n = size * count;
    bw_node *node;
    if (!size || !count) return 0;
    if (!f->can_write) { f->error = true; errno = EBADF; return 0; }
    if (f->kind != F_FILE) return console_write(f, (const char *)p, n) / size;
    node = f->node;
    f->unget = -1;
    if (f->append) f->pos = node->size;
    if (!bw_fs_reserve(node, f->pos + n)) { f->error = true; errno = ENOSPC; return 0; }
    if (f->pos > node->size) memset(node->data + node->size, 0, f->pos - node->size);
    memcpy(node->data + f->pos, p, n);
    f->pos += n;
    if (f->pos > node->size) node->size = f->pos;
    bw_fs_touch(node);
    return count;
}

int fseek(FILE *f, long offset, int whence) {
    long long base;
    if (f->kind != F_FILE) { errno = ESPIPE; return -1; }
    base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (long long)f->pos : (long long)f->node->size;
    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) { errno = EINVAL; return -1; }
    if (base + offset < 0) { errno = EINVAL; return -1; }
    f->pos = (size_t)(base + offset);
    f->eof = false;
    f->unget = -1;
    return 0;
}
long ftell(FILE *f) {
    if (f->kind != F_FILE) { errno = ESPIPE; return -1; }
    return (long)f->pos - (f->unget >= 0 ? 1 : 0);
}
int fgetpos(FILE *restrict f, fpos_t *restrict pos) { long p = ftell(f); if (p < 0) return -1; *pos = p; return 0; }
int fsetpos(FILE *f, const fpos_t *pos) { return fseek(f, (long)*pos, SEEK_SET); }
void rewind(FILE *f) { fseek(f, 0, SEEK_SET); f->error = false; }

int fflush(FILE *f) {
    if (!f) { bw_stdio_flush_all(); return 0; }
    if (f->kind == F_STDOUT || f->kind == F_STDERR) console_flush(f);
    else if (f->kind == F_FILE) bw_fs_save(f->node);
    return 0;
}
int feof(FILE *f) { return f->eof; }
int ferror(FILE *f) { return f->error; }
void clearerr(FILE *f) { f->eof = f->error = false; }
int setvbuf(FILE *restrict f, char *restrict buf, int mode, size_t size) {
    (void)buf; (void)size;
    if (f->kind == F_STDOUT || f->kind == F_STDERR) { console_flush(f); f->mode = mode; }
    return 0;
}
void setbuf(FILE *restrict f, char *restrict buf) { setvbuf(f, buf, buf ? _IOFBF : _IONBF, BUFSIZ); }
int fileno(FILE *f) { return f->kind == F_STDIN ? 0 : f->kind == F_STDOUT ? 1 : f->kind == F_STDERR ? 2 : 3; }

int fgetc(FILE *f) {
    unsigned char c;
    return fread(&c, 1, 1, f) == 1 ? c : EOF;
}
int getc(FILE *f) { return fgetc(f); }
int getchar(void) { return fgetc(stdin); }
int ungetc(int c, FILE *f) {
    if (c == EOF || f->unget >= 0) return EOF;
    if (f->kind == F_FILE && f->pos > 0 && f->node->data[f->pos - 1] == (unsigned char)c) { f->pos--; f->eof = false; return c; }
    f->unget = (unsigned char)c;
    f->eof = false;
    return c;
}
char *fgets(char *restrict s, int n, FILE *restrict f) {
    int i = 0, c = 0;
    if (n <= 0) return NULL;
    while (i < n - 1 && (c = fgetc(f)) != EOF) {
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    if (i == 0 && c == EOF) return NULL;
    s[i] = 0;
    return s;
}
int fputc(int c, FILE *f) { unsigned char b = (unsigned char)c; return fwrite(&b, 1, 1, f) == 1 ? b : EOF; }
int putc(int c, FILE *f) { return fputc(c, f); }
int putchar(int c) { return fputc(c, stdout); }
int fputs(const char *restrict s, FILE *restrict f) { size_t n = strlen(s); return fwrite(s, 1, n, f) == n || !n ? 0 : EOF; }
int puts(const char *s) { if (fputs(s, stdout) == EOF) return EOF; return fputc('\n', stdout) == EOF ? EOF : 0; }
void perror(const char *s) {
    if (s && s[0]) fprintf(stderr, "%s: %s\n", s, strerror(errno));
    else fprintf(stderr, "%s\n", strerror(errno));
}

int remove(const char *path) {
    int err = bw_fs_remove(path);
    if (err) { errno = err; return -1; }
    return 0;
}
int rename(const char *from, const char *to) {
    int err = bw_fs_rename(from, to);
    if (err) { errno = err; return -1; }
    return 0;
}
char *tmpnam(char *s) {
    static char name[L_tmpnam];
    static unsigned counter;
    char *out = s ? s : name;
    snprintf(out, L_tmpnam, "/tmp/bw%u", ++counter);
    return out;
}
FILE *tmpfile(void) {
    char name[L_tmpnam];
    FILE *f;
    bw_fs_mkdir("/tmp");
    if (!(f = fopen(tmpnam(name), "w+"))) return NULL;
    f->node->persist = false;
    bw_fs_remove(name);                                   /* gone when it's closed */
    return f;
}

/* ==== printf ============================================================== */
typedef struct sink {
    char *buf;            /* snprintf: the caller's buffer (cap bytes) */
    size_t cap;
    FILE *file;           /* fprintf */
    size_t count;         /* characters produced (what printf returns) */
} sink;

static void out(sink *s, const char *p, size_t n) {
    if (s->file) fwrite(p, 1, n, s->file);
    else if (s->count < s->cap) {
        size_t room = s->cap - s->count;
        memcpy(s->buf + s->count, p, n < room ? n : room);
    }
    s->count += n;
}
static void out_repeat(sink *s, char c, size_t n) {
    char chunk[32];
    memset(chunk, c, sizeof(chunk));
    while (n) { size_t k = n < sizeof(chunk) ? n : sizeof(chunk); out(s, chunk, k); n -= k; }
}

enum { FL_LEFT = 1, FL_PLUS = 2, FL_SPACE = 4, FL_ALT = 8, FL_ZERO = 16 };

/* prefix ("-", "0x"...) + zeros + body, padded to the width. */
static void out_padded(sink *s, int flags, int width, const char *prefix, size_t zeros, const char *body, size_t len) {
    size_t plen = strlen(prefix), total = plen + zeros + len;
    size_t pad = width > 0 && (size_t)width > total ? (size_t)width - total : 0;
    if (!(flags & FL_LEFT) && !(flags & FL_ZERO)) out_repeat(s, ' ', pad);
    out(s, prefix, plen);
    if (!(flags & FL_LEFT) && (flags & FL_ZERO)) out_repeat(s, '0', pad);
    out_repeat(s, '0', zeros);
    out(s, body, len);
    if (flags & FL_LEFT) out_repeat(s, ' ', pad);
}

static void format_int(sink *s, unsigned long long v, bool neg, int base, bool upper, int flags, int width, int prec, char conv) {
    char digits[72], prefix[4] = { 0 };
    const char *set = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    size_t n = 0, zeros = 0;
    int pi = 0;
    while (v) { digits[sizeof(digits) - 1 - n++] = set[v % (unsigned)base]; v /= (unsigned)base; }
    if (prec < 0) prec = 1;
    else flags &= ~FL_ZERO;                                    /* a precision turns off '0' */
    if ((size_t)prec > n) zeros = (size_t)prec - n;
    if (neg) prefix[pi++] = '-';
    else if (flags & FL_PLUS) prefix[pi++] = '+';
    else if (flags & FL_SPACE) prefix[pi++] = ' ';
    if (flags & FL_ALT) {
        if (base == 16 && n) { prefix[pi++] = '0'; prefix[pi++] = upper ? 'X' : 'x'; }
        else if (base == 8 && zeros == 0) zeros = 1;
    }
    if (conv == 'p' && n) { prefix[pi++] = '0'; prefix[pi++] = 'x'; }
    out_padded(s, flags, width, prefix, zeros, digits + sizeof(digits) - n, n);
}

/* Floating point. A double is exactly m * 2^e; its decimal digits are
 * worked out exactly with a big number (base 10^9 limbs), then rounded to
 * the digits wanted, halves to even - as glibc and musl do - so printf
 * gives the same text here as on the desktop. */
#define FP_LIMBS 130                          /* 2^1074 * 2^53 needs ~ 800 digits: 90 limbs */
typedef struct fp_big { uint32_t limb[FP_LIMBS]; int n; } fp_big;

static void big_mul(fp_big *b, uint32_t by) {        /* by < 2^32 */
    uint64_t carry = 0;
    int i;
    for (i = 0; i < b->n; ++i) {
        uint64_t t = (uint64_t)b->limb[i] * by + carry;
        b->limb[i] = (uint32_t)(t % 1000000000u);
        carry = t / 1000000000u;
    }
    while (carry && b->n < FP_LIMBS) { b->limb[b->n++] = (uint32_t)(carry % 1000000000u); carry /= 1000000000u; }
}

/* The exact digits of v (finite, >= 0): v = 0.DIGITS x 10^dexp. Returns how
 * many digits (trailing zeros dropped); "0" with dexp 1 for zero. */
static int fp_exact(double v, char *digits, int *dexp) {
    uint64_t bits, m;
    int e, len = 0, i, j;
    fp_big b;
    char tmp[FP_LIMBS * 9 + 1];
    if (v == 0) { digits[0] = '0'; *dexp = 1; return 1; }
    memcpy(&bits, &v, 8);
    e = (int)((bits >> 52) & 0x7ff);
    m = bits & 0xfffffffffffffULL;
    if (e) m |= 1ULL << 52; else e = 1;
    e -= 1075;                                           /* v = m * 2^e */
    while (!(m & 1)) { m >>= 1; ++e; }
    b.n = 0;
    while (m) { b.limb[b.n++] = (uint32_t)(m % 1000000000u); m /= 1000000000u; }
    for (i = e; i > 0; i -= 29) big_mul(&b, 1u << (i < 29 ? i : 29));                   /* * 2^e */
    for (i = -e; i > 0; i -= 13) {                                                       /* * 5^-e */
        uint32_t p5 = 1;
        for (j = 0; j < (i < 13 ? i : 13); ++j) p5 *= 5;
        big_mul(&b, p5);
    }
    for (i = b.n - 1; i >= 0; --i) {                     /* to text, most significant first */
        char part[16];
        snprintf(part, sizeof(part), i == b.n - 1 ? "%u" : "%09u", (unsigned)b.limb[i]);
        memcpy(tmp + len, part, strlen(part));
        len += (int)strlen(part);
    }
    *dexp = len + (e < 0 ? e : 0);                       /* * 10^e when we multiplied by 5^-e */
    while (len > 1 && tmp[len - 1] == '0') --len;
    memcpy(digits, tmp, (size_t)len);
    return len;
}

/* Keeps `keep` digits (rounding halves to even); may carry into a new digit. */
static int fp_round(char *d, int len, int keep, int *dexp) {
    bool up;
    int i;
    if (keep >= len) return len;
    if (keep < 0) { d[0] = '0'; return 0; }              /* nothing left: below half of the last place */
    up = d[keep] > '5' || (d[keep] == '5' && (len > keep + 1 || (keep > 0 && ((d[keep - 1] - '0') & 1))));
    len = keep;
    if (!up) return len;
    for (i = keep - 1; i >= 0; --i) {
        if (d[i] != '9') { d[i]++; return len; }
        d[i] = '0';
    }
    memmove(d + 1, d, (size_t)len);                      /* 999 -> 1000 */
    d[0] = '1';
    ++*dexp;
    return len + 1;
}

/* 'f': ddd.ddd with prec decimals; 'e': d.ddde+XX. v >= 0, finite. */
static size_t fp_digits(double v, char conv, int prec, char *out, size_t cap) {
    char d[FP_LIMBS * 9 + 4];
    int dexp, len = fp_exact(v, d, &dexp), i;
    size_t n = 0;
    bool zero = v == 0;
#define PUT(c) do { if (n + 1 < cap) out[n++] = (char)(c); } while (0)
    if (conv == 'f') {
        len = fp_round(d, len, dexp + prec, &dexp);
        if (len == 0) zero = true;
        if (zero || dexp <= 0) PUT('0');
        else for (i = 0; i < dexp; ++i) PUT(i < len ? d[i] : '0');
        if (prec > 0) {
            PUT('.');
            for (i = 0; i < prec; ++i) {
                int at = (zero ? 0 : dexp) + i;
                PUT(!zero && at >= 0 && at < len ? d[at] : '0');
            }
        }
    } else {
        int x;
        len = fp_round(d, len, prec + 1, &dexp);
        x = zero ? 0 : dexp - 1;
        PUT(zero ? '0' : d[0]);
        if (prec > 0) {
            PUT('.');
            for (i = 1; i <= prec; ++i) PUT(!zero && i < len ? d[i] : '0');
        }
        n += (size_t)snprintf(out + n, cap > n ? cap - n : 0, "e%c%02d", x < 0 ? '-' : '+', x < 0 ? -x : x);
        if (n >= cap) n = cap - 1;
    }
#undef PUT
    out[n < cap ? n : cap - 1] = 0;
    return n;
}

/* "1.5e+2" -> "1.5e+02" (C wants at least two exponent digits). */
static size_t fix_exponent(char *buf, size_t n, bool upper) {
    char *e = strchr(buf, 'e');
    size_t digits;
    if (!e) return n;
    if (upper) *e = 'E';
    digits = strlen(e + 2);
    if (digits < 2) {
        memmove(e + 3, e + 2, digits + 1);
        e[2] = '0';
        ++n;
    }
    return n;
}

static void format_float(sink *s, double v, char conv, int flags, int width, int prec) {
    char buf[512], prefix[2] = { 0 };
    size_t n;
    bool upper = conv == 'F' || conv == 'E' || conv == 'G' || conv == 'A';
    char lc = (char)tolower(conv);
    if (signbit(v)) { prefix[0] = '-'; v = -v; }
    else if (flags & FL_PLUS) prefix[0] = '+';
    else if (flags & FL_SPACE) prefix[0] = ' ';
    if (!isfinite(v)) {
        const char *word = isnan(v) ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf");
        out_padded(s, flags & ~FL_ZERO, width, prefix, 0, word, 3);
        return;
    }
    if (lc == 'a') {                                           /* hexadecimal: 0x1.8p+3 */
        bool zero = v == 0;
        int e2 = 0, i, digits = 13;
        unsigned long long bits = 0, lead = zero ? 0 : 1;
        char *p = buf;
        if (!zero) {
            uint64_t raw;
            memcpy(&raw, &v, 8);
            if (((raw >> 52) & 0x7ff) == 0) {                  /* subnormal: 0x0.xxxp-1022, as glibc writes it */
                lead = 0;
                e2 = -1022;
                bits = raw & 0xfffffffffffffULL;
            } else {
                double m = frexp(v, &e2) * 2;                  /* 1 <= m < 2 */
                e2 -= 1;
                bits = (unsigned long long)ldexp(m - 1, 52);   /* the 13 hex digits after the point */
            }
        }
        if (prec >= 0 && prec < 13) {                          /* round to prec digits, ties to even */
            int drop = 4 * (13 - prec);
            unsigned long long half = 1ULL << (drop - 1), rest = bits & ((1ULL << drop) - 1);
            bits >>= drop;
            if (rest > half || (rest == half && (bits & 1))) ++bits;
            if (bits >> (4 * prec)) { bits = 0; ++lead; }      /* 0xf.f -> 0x2.0: renormalise */
            if (lead == 2) { lead = 1; ++e2; }
            digits = prec;
        } else if (prec < 0) {                                 /* as many as it takes */
            while (digits > 0 && (bits & 15) == 0) { bits >>= 4; --digits; }
        }
        *p++ = '0'; *p++ = upper ? 'X' : 'x';
        *p++ = (char)('0' + lead);
        if (digits > 0 || (flags & FL_ALT)) *p++ = '.';
        for (i = digits - 1; i >= 0; --i) *p++ = (upper ? "0123456789ABCDEF" : "0123456789abcdef")[(bits >> (4 * i)) & 15];
        for (i = 13; i < prec && i < 100; ++i) *p++ = '0';
        p += snprintf(p, 16, "%c%+d", upper ? 'P' : 'p', e2);
        out_padded(s, flags, width, prefix, 0, buf, (size_t)(p - buf));
        return;
    }
    if (prec < 0) prec = 6;
    if (prec > 100) prec = 100;
    if (lc == 'g') {
        int P = prec == 0 ? 1 : prec, X;
        char *e;
        fp_digits(v, 'e', P - 1, buf, sizeof(buf));
        e = strchr(buf, 'e');
        X = e ? atoi(e + 1) : 0;
        if (P > X && X >= -4) n = fp_digits(v, 'f', P - 1 - X, buf, sizeof(buf));
        else n = fix_exponent(buf, fp_digits(v, 'e', P - 1, buf, sizeof(buf)), upper);
        if (!(flags & FL_ALT) && strchr(buf, '.')) {          /* drop trailing zeros */
            char *exp = strpbrk(buf, "eE"), tail[16] = { 0 };
            char *end = exp ? exp : buf + n;
            if (exp) snprintf(tail, sizeof(tail), "%s", exp);
            while (end > buf && end[-1] == '0') --end;
            if (end > buf && end[-1] == '.') --end;
            n = (size_t)(end - buf);
            memcpy(end, tail, strlen(tail) + 1);
            n += strlen(tail);
        }
    } else if (lc == 'e') {
        n = fix_exponent(buf, fp_digits(v, 'e', prec, buf, sizeof(buf)), upper);
    } else {
        n = fp_digits(v, 'f', prec, buf, sizeof(buf));
    }
    if ((flags & FL_ALT) && !strchr(buf, '.')) {              /* '#': always a decimal point */
        char *exp = strpbrk(buf, "eE");
        size_t at = exp ? (size_t)(exp - buf) : n;
        memmove(buf + at + 1, buf + at, n - at + 1);
        buf[at] = '.';
        ++n;
    }
    out_padded(s, flags, width, prefix, 0, buf, n);
}

static int format(sink *s, const char *fmt, va_list ap) {
    for (; *fmt; ++fmt) {
        int flags = 0, width = -1, prec = -1;
        char len = 0, conv;
        const char *start = fmt;
        if (*fmt != '%') {
            const char *run = fmt;
            while (fmt[1] && fmt[1] != '%') ++fmt;
            out(s, run, (size_t)(fmt - run + 1));
            continue;
        }
        for (++fmt;; ++fmt) {
            if (*fmt == '-') flags |= FL_LEFT;
            else if (*fmt == '+') flags |= FL_PLUS;
            else if (*fmt == ' ') flags |= FL_SPACE;
            else if (*fmt == '#') flags |= FL_ALT;
            else if (*fmt == '0') flags |= FL_ZERO;
            else break;
        }
        if (*fmt == '*') { width = va_arg(ap, int); if (width < 0) { flags |= FL_LEFT; width = -width; } ++fmt; }
        else if (isdigit((unsigned char)*fmt)) { width = 0; while (isdigit((unsigned char)*fmt)) width = width * 10 + (*fmt++ - '0'); }
        if (*fmt == '.') {
            ++fmt;
            if (*fmt == '*') { prec = va_arg(ap, int); ++fmt; }
            else { prec = 0; while (isdigit((unsigned char)*fmt)) prec = prec * 10 + (*fmt++ - '0'); }
        }
        if (flags & FL_LEFT) flags &= ~FL_ZERO;
        switch (*fmt) {
        case 'h': len = 'h'; if (fmt[1] == 'h') { len = 'H'; ++fmt; } ++fmt; break;
        case 'l': len = 'l'; if (fmt[1] == 'l') { len = 'q'; ++fmt; } ++fmt; break;
        case 'j': case 'q': len = 'q'; ++fmt; break;
        case 'z': case 't': len = 'z'; ++fmt; break;
        case 'L': len = 'L'; ++fmt; break;
        default: break;
        }
        conv = *fmt;
        switch (conv) {
        case 'd': case 'i': {
            long long v;
            if (len == 'q') v = va_arg(ap, long long);
            else if (len == 'l') v = va_arg(ap, long);
            else if (len == 'z') v = va_arg(ap, ptrdiff_t);
            else v = va_arg(ap, int);
            if (len == 'h') v = (short)v;
            else if (len == 'H') v = (signed char)v;
            format_int(s, v < 0 ? 0ULL - (unsigned long long)v : (unsigned long long)v, v < 0, 10, false, flags, width, prec, conv);
            break;
        }
        case 'u': case 'x': case 'X': case 'o': {
            unsigned long long v;
            if (len == 'q') v = va_arg(ap, unsigned long long);
            else if (len == 'l') v = va_arg(ap, unsigned long);
            else if (len == 'z') v = va_arg(ap, size_t);
            else v = va_arg(ap, unsigned);
            if (len == 'h') v = (unsigned short)v;
            else if (len == 'H') v = (unsigned char)v;
            format_int(s, v, false, conv == 'o' ? 8 : conv == 'u' ? 10 : 16, conv == 'X', flags & ~(FL_PLUS | FL_SPACE), width, prec, conv);
            break;
        }
        case 'p': format_int(s, (uintptr_t)va_arg(ap, void *), false, 16, false, flags, width, prec, 'p'); break;
        case 'c': { char c = (char)va_arg(ap, int); out_padded(s, flags & ~FL_ZERO, width, "", 0, &c, 1); break; }
        case 's': {
            const char *str = va_arg(ap, const char *);
            if (!str) str = "(null)";
            out_padded(s, flags & ~FL_ZERO, width, "", 0, str, prec >= 0 ? strnlen(str, (size_t)prec) : strlen(str));
            break;
        }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A':
            format_float(s, len == 'L' ? (double)va_arg(ap, long double) : va_arg(ap, double), conv, flags, width, prec);
            break;
        case 'n': {
            void *p = va_arg(ap, void *);
            if (len == 'q') *(long long *)p = (long long)s->count;
            else if (len == 'l') *(long *)p = (long)s->count;
            else if (len == 'h') *(short *)p = (short)s->count;
            else if (len == 'H') *(signed char *)p = (signed char)s->count;
            else if (len == 'z') *(size_t *)p = s->count;
            else *(int *)p = (int)s->count;
            break;
        }
        case '%': out(s, "%", 1); break;
        case 0: out(s, start, (size_t)(fmt - start)); --fmt; break;
        default: out(s, start, (size_t)(fmt - start + 1)); break;
        }
    }
    return s->count > INT_MAX ? -1 : (int)s->count;
}

int vsnprintf(char *restrict buf, size_t n, const char *restrict fmt, va_list ap) {
    sink s = { buf, n ? n - 1 : 0, NULL, 0 };
    int r = format(&s, fmt, ap);
    if (n) buf[s.count < n - 1 ? s.count : n - 1] = 0;
    return r;
}
int vsprintf(char *restrict buf, const char *restrict fmt, va_list ap) { return vsnprintf(buf, (size_t)INT_MAX, fmt, ap); }
int vfprintf(FILE *restrict f, const char *restrict fmt, va_list ap) {
    sink s = { NULL, 0, f, 0 };
    return format(&s, fmt, ap);
}
int vprintf(const char *restrict fmt, va_list ap) { return vfprintf(stdout, fmt, ap); }
int snprintf(char *restrict buf, size_t n, const char *restrict fmt, ...) {
    va_list ap; int r;
    va_start(ap, fmt); r = vsnprintf(buf, n, fmt, ap); va_end(ap);
    return r;
}
int sprintf(char *restrict buf, const char *restrict fmt, ...) {
    va_list ap; int r;
    va_start(ap, fmt); r = vsprintf(buf, fmt, ap); va_end(ap);
    return r;
}
int fprintf(FILE *restrict f, const char *restrict fmt, ...) {
    va_list ap; int r;
    va_start(ap, fmt); r = vfprintf(f, fmt, ap); va_end(ap);
    return r;
}
int printf(const char *restrict fmt, ...) {
    va_list ap; int r;
    va_start(ap, fmt); r = vfprintf(stdout, fmt, ap); va_end(ap);
    return r;
}

/* ==== scanf (from strings) ================================================ */
int vsscanf(const char *restrict str, const char *restrict fmt, va_list ap) {
    const char *s = str;
    int assigned = 0;
    for (; *fmt; ++fmt) {
        bool skip = false;
        int width = 0;
        char len = 0;
        if (isspace((unsigned char)*fmt)) { while (isspace((unsigned char)*s)) ++s; continue; }
        if (*fmt != '%' || fmt[1] == '%') {
            if (*fmt == '%') ++fmt;
            if (*s != *fmt) break;
            ++s;
            continue;
        }
        ++fmt;
        if (*fmt == '*') { skip = true; ++fmt; }
        while (isdigit((unsigned char)*fmt)) width = width * 10 + (*fmt++ - '0');
        if (*fmt == 'h') { len = 'h'; if (fmt[1] == 'h') { len = 'H'; ++fmt; } ++fmt; }
        else if (*fmt == 'l') { len = 'l'; if (fmt[1] == 'l') { len = 'q'; ++fmt; } ++fmt; }
        else if (*fmt == 'z' || *fmt == 'j' || *fmt == 't' || *fmt == 'L') { len = *fmt == 'L' ? 'L' : *fmt == 'j' ? 'q' : 'z'; ++fmt; }
        if (*fmt != 'c' && *fmt != '[' && *fmt != 'n') while (isspace((unsigned char)*s)) ++s;
        if (!*s && *fmt != 'n') return assigned ? assigned : EOF;
        switch (*fmt) {
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': {
            char tmp[64], *end;
            int base = *fmt == 'd' || *fmt == 'u' ? 10 : *fmt == 'i' ? 0 : *fmt == 'o' ? 8 : 16;
            size_t n = strlen(s);
            unsigned long long v;
            if (width && (size_t)width < n) n = (size_t)width;
            if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
            memcpy(tmp, s, n); tmp[n] = 0;
            v = (unsigned long long)strtoll(tmp, &end, base);
            if (*fmt == 'u' || *fmt == 'x' || *fmt == 'X' || *fmt == 'o') { char *e2; unsigned long long u = strtoull(tmp, &e2, base); if (e2 > end) end = e2; v = u; }
            if (end == tmp) return assigned;
            s += end - tmp;
            if (!skip) {
                void *p = va_arg(ap, void *);
                if (len == 'q') *(long long *)p = (long long)v;
                else if (len == 'l') *(long *)p = (long)v;
                else if (len == 'z') *(size_t *)p = (size_t)v;
                else if (len == 'h') *(short *)p = (short)v;
                else if (len == 'H') *(signed char *)p = (signed char)v;
                else *(int *)p = (int)v;
                ++assigned;
            }
            break;
        }
        case 'f': case 'e': case 'g': case 'E': case 'G': case 'a': {
            char tmp[128], *end;
            size_t n = strlen(s);
            double v;
            if (width && (size_t)width < n) n = (size_t)width;
            if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
            memcpy(tmp, s, n); tmp[n] = 0;
            v = strtod(tmp, &end);
            if (end == tmp) return assigned;
            s += end - tmp;
            if (!skip) {
                if (len == 'l' || len == 'L') *va_arg(ap, double *) = v;
                else *va_arg(ap, float *) = (float)v;
                ++assigned;
            }
            break;
        }
        case 's': {
            char *d = skip ? NULL : va_arg(ap, char *);
            int n = 0;
            while (*s && !isspace((unsigned char)*s) && (!width || n < width)) { if (d) d[n] = *s; ++n; ++s; }
            if (d) { d[n] = 0; ++assigned; }
            break;
        }
        case 'c': {
            char *d = skip ? NULL : va_arg(ap, char *);
            int n = 0, w = width ? width : 1;
            while (*s && n < w) { if (d) d[n] = *s; ++n; ++s; }
            if (n < w) return assigned ? assigned : EOF;
            if (d) ++assigned;
            break;
        }
        case '[': {
            char *d = skip ? NULL : va_arg(ap, char *);
            bool neg = false, set[256];
            int n = 0, i;
            ++fmt;
            if (*fmt == '^') { neg = true; ++fmt; }
            for (i = 0; i < 256; ++i) set[i] = neg;
            if (*fmt == ']') { set[(unsigned char)']'] = !neg; ++fmt; }
            for (; *fmt && *fmt != ']'; ++fmt) {
                if (fmt[1] == '-' && fmt[2] && fmt[2] != ']') {
                    for (i = (unsigned char)fmt[0]; i <= (unsigned char)fmt[2]; ++i) set[i] = !neg;
                    fmt += 2;
                } else set[(unsigned char)*fmt] = !neg;
            }
            while (*s && set[(unsigned char)*s] && (!width || n < width)) { if (d) d[n] = *s; ++n; ++s; }
            if (!n) return assigned;
            if (d) { d[n] = 0; ++assigned; }
            if (!*fmt) --fmt;
            break;
        }
        case 'n':
            if (!skip) *va_arg(ap, int *) = (int)(s - str);
            break;
        default:
            return assigned;
        }
    }
    return assigned;
}
int sscanf(const char *restrict s, const char *restrict fmt, ...) {
    va_list ap; int r;
    va_start(ap, fmt); r = vsscanf(s, fmt, ap); va_end(ap);
    return r;
}
