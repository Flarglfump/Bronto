/*
 * Checks the web build's C library against the system's: this program
 * prints the results of many C library calls, and `make web-test` runs it
 * natively and as WebAssembly (in Node) and compares the two outputs line
 * by line. Anything printed must come out the same from any correct C
 * library, so nothing here prints pointers, times or last-digit maths.
 *
 *   libc_check <scratch folder>    (the folder must exist and be empty)
 */
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_lines;
static void say(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    ++g_lines;
}

static void check_printf(void) {
    static const double values[] = { 0.0, -0.0, 1.0, -1.5, 0.1, 0.5, 2.5, 3.14159265358979, 1e-5, 123456789.0, 1e21,
                                     1.5e300, 9.999999e-5, 0.000123456, 1e100, 6.02214076e23, 1.0 / 3.0, 255.255,
                                     4.35, 0.125, 1e-310, 5e-324, 99.5, 100.0, 1234567.0, 0.0001, 0.00001234 };
    static const char *const fmts[] = { "%f", "%.0f", "%.1f", "%.3f", "%10.2f", "%-10.2f|", "%+.2f", "% .2f", "%010.3f",
                                        "%#.0f", "%e", "%.0e", "%.3e", "%E", "%g", "%.0g", "%.1g", "%.3g", "%.10g", "%.17g",
                                        "%#g", "%G", "%12.4g|", "%-12.4g|", "%a", "%.2a", "%A" };
    size_t i, j;
    for (i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        for (j = 0; j < sizeof(fmts) / sizeof(fmts[0]); ++j) {
            printf("%s ", fmts[j]);
            printf(fmts[j], values[i]);
            printf(" ; ");
        }
        say("");
    }
    say("%f %e %g %F %E %G", INFINITY, -INFINITY, NAN, INFINITY, -INFINITY, -INFINITY);
    say("[%5.1f] [%-6g] [%06.1f]", INFINITY, -INFINITY, -2.5);
    say("%d %i %u %x %X %o %c %s %%", -42, 42, 42u, 0xbeefu, 0xbeefu, 8u, 'Z', "str");
    say("[%5d] [%-5d] [%05d] [%+d] [% d] [%.3d] [%8.3d] [%-8.3d] [%.0d]", 42, 42, 42, 42, 42, 7, 7, 7, 0);
    say("[%#x] [%#o] [%#X] [%#.3x] [%08x] [%#08x]", 255u, 8u, 255u, 1u, 255u, 255u);
    say("%ld %lu %lld %llu %lx", -2147483647L - 1, 4294967295UL, LLONG_MIN, ULLONG_MAX, 0x7fffffffUL);   /* (long is 32 bits on the web) */
    say("%hd %hu %hhd %hhu", (short)-12345, (unsigned short)54321, (signed char)-100, (unsigned char)200);
    say("%zu %zd %td", (size_t)123456, (ptrdiff_t)-5, (ptrdiff_t)77);
    say("[%10s] [%-10s] [%.2s] [%*d] [%-*d] [%.*f]", "abc", "abc", "abcdef", 6, 5, 6, 5, 2, 3.14159);
    say("[%c%c%c]", 'a', 0x42, '9');
    {
        char small[8];
        int n = snprintf(small, sizeof(small), "%s", "truncated text");
        int m = snprintf(NULL, 0, "%d-%d", 1234, 5678);
        int k;
        say("snprintf truncates: %d \"%s\" %d", n, small, m);
        snprintf(small, sizeof(small), "ab%ncd", &k);
        say("%%n counted %d", k);
    }
}

static void check_strtod(void) {
    static const char *const inputs[] = { "0", "1", "-1", "3.14159", "1e10", "1E-10", "  42.5abc", "+.5", "-.25e2", "5.",
                                          "0.1", "0.30000000000000004", "1.7976931348623157e308", "2.2250738585072014e-308",
                                          "4.9e-324", "1e400", "-1e400", "1e-400", "inf", "-Infinity", "nan", "NAN(123)",
                                          "0x1p3", "0x1.8p1", "0X.8p0", "0x10", "abc", "", ".", "e5", "1e", "1e+", "12e3x",
                                          "123456789012345678901234567890", "0.000001", "9007199254740993" };
    size_t i;
    for (i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
        char *end;
        double v;
        errno = 0;
        v = strtod(inputs[i], &end);
        say("strtod(\"%s\") = %.17g, used %d, %s", inputs[i], v, (int)(end - inputs[i]), errno == ERANGE ? "ERANGE" : "ok");
    }
    say("atof %.17g %.17g", atof("2.5"), atof("-7e-3"));
    say("strtof %.9g", (double)strtof("0.1", NULL));
}

static void check_strtol(void) {
    static const struct { const char *s; int base; } cases[] = {
        { "123", 10 }, { "-123", 10 }, { "  +77", 10 }, { "0x1F", 16 }, { "0x1F", 0 }, { "017", 0 }, { "017", 10 },
        { "z", 36 }, { "Zz", 36 }, { "101", 2 }, { "12abc", 10 }, { "abc", 10 }, { "9223372036854775807", 10 },
        { "9223372036854775808", 10 }, { "-9223372036854775809", 10 }, { "0x", 16 }, { "0x", 0 }, { "", 10 },
        { "  -0", 10 }, { "18446744073709551615", 10 } };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char *end;
        long long v;
        unsigned long long u;
        errno = 0;
        v = strtoll(cases[i].s, &end, cases[i].base);
        say("strtoll(\"%s\", %d) = %lld used %d %s", cases[i].s, cases[i].base, v, (int)(end - cases[i].s), errno == ERANGE ? "ERANGE" : "ok");
        errno = 0;
        u = strtoull(cases[i].s, &end, cases[i].base);
        say("strtoull(\"%s\", %d) = %llu used %d %s", cases[i].s, cases[i].base, u, (int)(end - cases[i].s), errno == ERANGE ? "ERANGE" : "ok");
    }
    say("strtol(\"2147483648\") = %ld", (long)strtol("2147483647", NULL, 10));
    say("atoi %d %d %d", atoi("42"), atoi("-17xyz"), atoi("  9"));
}

typedef struct item { int key, order; } item;
static int by_key(const void *a, const void *b) { return ((const item *)a)->key - ((const item *)b)->key; }
static int by_int(const void *a, const void *b) { int x = *(const int *)a, y = *(const int *)b; return (x > y) - (x < y); }

static void check_sort(void) {
    int nums[200], i, ok = 1;
    item items[50];
    char line[1024];
    size_t n = 0;
    unsigned seed = 12345;
    for (i = 0; i < 200; ++i) { seed = seed * 1103515245u + 12345u; nums[i] = (int)((seed >> 8) % 1000) - 500; }
    qsort(nums, 200, sizeof(int), by_int);
    for (i = 1; i < 200; ++i) if (nums[i - 1] > nums[i]) ok = 0;
    say("qsort 200 ints sorted: %s, first %d last %d", ok ? "yes" : "NO", nums[0], nums[199]);
    for (i = 0; i < 50; ++i) { items[i].key = (i * 7) % 5; items[i].order = i; }
    qsort(items, 50, sizeof(item), by_key);
    for (i = 0; i < 50; ++i) n += (size_t)snprintf(line + n, sizeof(line) - n, "%d:%d ", items[i].key, items[i].order);
    say("qsort keys (equal keys keep order): %s", line);
    {
        int key = nums[100];
        int *found = (int *)bsearch(&key, nums, 200, sizeof(int), by_int);
        say("bsearch finds %d: %s", key, found && *found == key ? "yes" : "NO");
    }
}

static void check_strings(void) {
    char buf[64], tok[] = "a,b,,c;d";
    char *t;
    char line[256] = "";
    say("strlen %d strcmp %d %d %d strncmp %d", (int)strlen("hello"), strcmp("a", "b") < 0, strcmp("b", "a") > 0, strcmp("x", "x"), strncmp("abcd", "abcx", 3));
    say("strchr %s strrchr %s strstr %s strstr-empty %s", strchr("a/b/c", '/'), strrchr("a/b/c", '/'), strstr("haystack", "st"), strstr("abc", ""));
    say("strspn %d strcspn %d strpbrk %s", (int)strspn("aabbc", "ab"), (int)strcspn("hello, world", ",;"), strpbrk("key=value", "=:"));
    strcpy(buf, "abc"); strcat(buf, "def"); strncat(buf, "ghijkl", 3);
    say("strcpy/strcat/strncat %s", buf);
    memset(buf, 'x', 5); buf[5] = 0;
    say("memset %s memcmp %d", buf, memcmp("abc", "abd", 3) < 0);
    memmove(buf + 1, buf, 4); buf[0] = 'y';
    say("memmove %s", buf);
    strncpy(buf, "ab", 5);
    say("strncpy pads: %d %d %d", buf[2], buf[3], buf[4]);
    for (t = strtok(tok, ",;"); t; t = strtok(NULL, ",;")) { strcat(line, "["); strcat(line, t); strcat(line, "]"); }
    say("strtok %s", line);
    say("ctype %d%d%d%d%d%d%d%d %c%c", isalpha('a') != 0, isdigit('5') != 0, isspace('\t') != 0, isupper('Q') != 0,
        isxdigit('f') != 0, ispunct('!') != 0, isalnum('_') != 0, isprint('\n') != 0, toupper('a'), tolower('Q'));
}

static void check_sscanf(void) {
    int a = 0, b = 0, n = 0;
    float f = 0;
    double d = 0;
    char word[32] = "", rest[32] = "";
    unsigned x = 0;
    int got = sscanf("12 -34 2.5 7.25 hello 0x1f tail", "%d %d %f %lf %31s %x %31[a-z]", &a, &b, &f, &d, word, &x, rest);
    say("sscanf %d: %d %d %g %g %s %u %s", got, a, b, (double)f, d, word, x, rest);
    got = sscanf("key=value;", "%31[^=]=%31[^;]%n", word, rest, &n);
    say("sscanf %d: %s %s %d", got, word, rest, n);
    got = sscanf("", "%d", &a);
    say("sscanf of nothing: %d", got);
    got = sscanf("abc", "%d", &a);
    say("sscanf of no number: %d", got);
}

static void check_math(void) {
    say("sqrt %.17g floor %g ceil %g round %g %g %g trunc %g fabs %g", sqrt(2.0), floor(-2.5), ceil(-2.5), round(2.5), round(-2.5), round(0.49999999999999994), trunc(-3.7), fabs(-0.0));
    say("fmod %.17g %.17g %g", fmod(10.0, 3.0), fmod(-7.5, 2.0), fmod(5.0, INFINITY));
    say("roundf %g %g lround %ld rint %g %g", (double)roundf(1.5f), (double)roundf(-0.5f), lround(-3.5), rint(2.5), rint(3.5));
    say("sin %.12g cos %.12g tan %.12g atan2 %.12g", sin(1.0), cos(1.0), tan(0.5), atan2(1.0, -1.0));
    say("exp %.12g log %.12g log10 %.12g pow %.12g %g nan %d", exp(1.0), log(10.0), log10(1000.0), pow(2.0, 0.5), pow(2.0, 10.0), isnan(pow(-8.0, 1.0 / 3.0)) != 0);
    say("hypot %.12g cbrt %.12g", hypot(3.0, 4.0), cbrt(27.0));
    {
        int e;
        double m = frexp(48.0, &e), ip, fr = modf(-3.25, &ip);
        say("frexp %g %d ldexp %g modf %g %g", m, e, ldexp(0.75, 4), fr, ip);
        m = frexp(1e-310, &e);
        say("frexp subnormal %.17g %d", m, e);
    }
    say("isnan %d isinf %d isfinite %d signbit %d", isnan(NAN) != 0, isinf(-INFINITY) != 0, isfinite(1.0) != 0, signbit(-0.0) != 0);
    say("fmin %g fmax %g copysign %g", fmin(NAN, 1.0), fmax(2.0, NAN), copysign(3.0, -0.0));
}

static jmp_buf g_outer, g_inner;
static int g_depth;
static void jump_deep(int n) {
    if (n == 0) longjmp(g_inner, 7);
    jump_deep(n - 1);
}
static void check_setjmp(void) {
    volatile int stage = 0;
    int r = setjmp(g_outer);
    if (r == 0) {
        int s = setjmp(g_inner);
        if (s == 0) { stage = 1; jump_deep(20); }
        say("setjmp inner returned %d after stage %d", s, stage);
        stage = 2;
        longjmp(g_outer, 0);                       /* 0 arrives as 1 */
    }
    say("setjmp outer returned %d, stage %d", r, stage);
    g_depth++;
    if (g_depth < 3) check_setjmp();               /* again, from inside: reused buffers */
}

static void check_files(const char *dir) {
    char path[512], path2[512], buf[64];
    FILE *f;
    size_t n;
    long size;
    snprintf(path, sizeof(path), "%s/data.txt", dir);
    snprintf(path2, sizeof(path2), "%s/moved.txt", dir);
    f = fopen(path, "w");
    say("fopen w: %s", f ? "ok" : "failed");
    if (!f) return;
    fprintf(f, "line one\nline %d\n", 2);
    fputs("third", f);
    fclose(f);
    f = fopen(path, "rb");
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    rewind(f);
    n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = 0;
    say("size %ld read %d eof %d: %s", size, (int)n, feof(f) != 0, buf);
    rewind(f);
    if (!fgets(buf, sizeof(buf), f)) buf[0] = 0;
    say("fgets: %s", buf);
    {
        int c1 = fgetc(f), c2 = ungetc('Q', f), c3 = fgetc(f), c4 = fgetc(f);
        say("fgetc %c ungetc %c fgetc %c then %c", c1, c2, c3, c4);
    }
    fseek(f, -5, SEEK_END);
    n = fread(buf, 1, 10, f);
    buf[n] = 0;
    say("fseek from the end: %s (%d)", buf, (int)n);
    fclose(f);
    f = fopen(path, "a");
    fputs("+appended", f);
    fclose(f);
    f = fopen(path, "r+");
    fseek(f, 5, SEEK_SET);
    fputs("ONE", f);
    fclose(f);
    f = fopen(path, "r");
    n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = 0;
    fclose(f);
    say("after append and r+: %s", buf);
    {
        int renamed = rename(path, path2), old_there, new_there, r1, r2;
        FILE *g = fopen(path, "r");
        old_there = g != NULL;
        if (g) fclose(g);
        g = fopen(path2, "r");
        new_there = g != NULL;
        if (g) fclose(g);
        say("rename %d, old there %d, new there %d", renamed, old_there, new_there);
        r1 = remove(path2);
        r2 = remove(path2);
        say("remove %d, then %d", r1, r2);
    }
    snprintf(path, sizeof(path), "%s/no/such/file", dir);
    {
        FILE *g;
        errno = 0;
        g = fopen(path, "r");
        say("fopen missing: %s, errno ENOENT %d", g ? "opened?!" : "NULL", errno == ENOENT);
    }
    say("fopen in a missing folder: %s", fopen(path, "w") ? "opened?!" : "NULL");
}

static void check_memory(void) {
    void *blocks[400];
    size_t i, ok = 1;
    unsigned seed = 99;
    for (i = 0; i < 400; ++i) {
        size_t sz;
        seed = seed * 1103515245u + 12345u;
        sz = 1 + (seed >> 8) % (i % 10 == 0 ? 200000 : 300);
        blocks[i] = malloc(sz);
        if (!blocks[i] || ((size_t)blocks[i] & 7)) ok = 0;
        else memset(blocks[i], (int)(i & 255), sz);
        if (i % 3 == 0 && i > 0) { free(blocks[i - 1]); blocks[i - 1] = NULL; }
    }
    for (i = 0; i < 400; i += 2) {
        if (blocks[i]) {
            unsigned char *p = (unsigned char *)blocks[i];
            if (p[0] != (unsigned char)(i & 255)) ok = 0;
            blocks[i] = realloc(blocks[i], 1000 + i);
            if (!blocks[i] || ((unsigned char *)blocks[i])[0] != (unsigned char)(i & 255)) ok = 0;
        }
    }
    for (i = 0; i < 400; ++i) free(blocks[i]);
    {
        int *z = (int *)calloc(1000, sizeof(int)), sum = 0;
        for (i = 0; i < 1000; ++i) sum += z[i];
        free(z);
        say("malloc/realloc/free keep data: %s, calloc zeroes: %s", ok ? "yes" : "NO", sum == 0 ? "yes" : "NO");
    }
    say("realloc(NULL) works: %s", (blocks[0] = realloc(NULL, 10)) != NULL ? "yes" : "NO");
    free(blocks[0]);
    free(NULL);
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: libc_check <scratch folder>\n"); return 2; }
    check_printf();
    check_strtod();
    check_strtol();
    check_sort();
    check_strings();
    check_sscanf();
    check_math();
    check_setjmp();
    check_files(argv[1]);
    check_memory();
    say("%d lines", g_lines + 1);
    return 0;
}
