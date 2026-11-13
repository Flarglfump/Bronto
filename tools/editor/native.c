/*
 * BRONTO EDITOR - the game's C code (game.h, docs/c-code.md).
 *
 * A project's C code is every .c file in its code folder (Build Settings;
 * src/ by default), with the .h files beside them. The editor compiles it
 * itself, with whatever C compiler the computer has:
 *
 *   - while you work: into a shared library (game_N.so / .dylib / .dll in
 *     build/native/editor/) that the editor loads, so the code's components
 *     are in the inspector and the scene files, and its systems and
 *     behaviours run in Play. It's compiled again (and loaded again, as a new
 *     copy) whenever a .c or .h file changes; the scene is made again with it.
 *     The library takes the engine's functions from the editor itself (Linux:
 *     the editor is linked with -rdynamic; macOS: -undefined dynamic_lookup;
 *     Windows: the editor exports them, with an import library beside it).
 *
 *   - for a game: into the player itself (tools/player/main.c, compiled with
 *     BR_PLAYER_NATIVE), with the whole engine (src/bronto.c, the single-file
 *     build). The engine and the player's main are compiled once per project
 *     and compiler and kept in build/native/<target>/; only the game's own
 *     code is compiled each time. The web player is made the same way, with
 *     clang's WebAssembly target and the engine's small C library.
 *
 * bronto_game_setup(), which runs the code's BR_GAME_SETUP functions, is
 * written by the editor (build/native/setup.c): it finds them in the sources.
 *
 * The engine's sources and headers (the "SDK") are found beside the editor:
 * sdk/ next to the program (an installed editor), Resources/sdk in a Mac app,
 * or the repository itself when the editor runs from its build/ folder.
 *
 * The compiler: the project's choice (Build Settings), or else the first one
 * found - Windows: cl (from the PATH, or Visual Studio found with vswhere) for
 * an editor built with MSVC, gcc or clang for one built with MinGW; elsewhere
 * cc, gcc or clang. Its messages go to the console, where a double-click opens
 * the file at the line, and the code tab marks the line.
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif
#include "editor.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <dlfcn.h>
#  include <sys/wait.h>
#endif

#define NP 1024                                   /* paths here: project + build/native/... */
#define MAX_SOURCES 256

/* ---- a growing string ------------------------------------------------------------------ */

typedef struct sbuf { char *s; size_t n, cap; bool bad; } sbuf;

static void sb_add(sbuf *b, const char *text) {
    size_t k = strlen(text);
    if (b->bad) return;
    if (b->n + k + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 1024;
        char *s;
        while (b->n + k + 1 > cap) cap *= 2;
        if (!(s = (char *)br_realloc(b->s, cap))) { b->bad = true; return; }
        b->s = s;
        b->cap = cap;
    }
    memcpy(b->s + b->n, text, k + 1);
    b->n += k;
}

static void sb_addf(sbuf *b, const char *fmt, ...) {
    char tmp[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    sb_add(b, tmp);
}

/* A path or word as one argument: in double quotes (both cmd.exe and sh take them). */
static void sb_arg(sbuf *b, const char *word) {
    sb_add(b, " \"");
    sb_add(b, word);
    sb_add(b, "\"");
}

/* An option and a path with no space between: /Fe"C:/games/x.exe" (cl's way). */
static void sb_opt(sbuf *b, const char *option, const char *path) {
    sb_add(b, " ");
    sb_add(b, option);
    sb_add(b, "\"");
    sb_add(b, path);
    sb_add(b, "\"");
}

/* Options as the user typed them (several, already split by spaces and quotes). */
static void sb_raw(sbuf *b, const char *options) {
    if (options && options[0]) { sb_add(b, " "); sb_add(b, options); }
}

static void sb_free(sbuf *b) { br_free(b->s); memset(b, 0, sizeof(*b)); }

/* ---- running a command ------------------------------------------------------------------ */

bool ed_run_command(const char *command, const char *cwd, char **output, int *status) {
    sbuf out = { 0 };
    *status = -1;
    if (output) *output = NULL;
#if defined(_WIN32)
    {
        sbuf line = { 0 };
        wchar_t *wide = NULL, wcwd[NP];
        int wn;
        SECURITY_ATTRIBUTES sa;
        HANDLE rd = NULL, wr = NULL;
        STARTUPINFOW si;
        PROCESS_INFORMATION pi;
        DWORD code = 1, got;
        char buf[4096];
        BOOL ok;
        sb_add(&line, "cmd.exe /d /s /c \"");
        sb_add(&line, command);
        sb_add(&line, "\"");
        if (line.bad || (wn = MultiByteToWideChar(CP_UTF8, 0, line.s, -1, NULL, 0)) <= 0 ||
            !(wide = (wchar_t *)br_alloc((size_t)wn * sizeof(wchar_t)))) { sb_free(&line); return false; }
        MultiByteToWideChar(CP_UTF8, 0, line.s, -1, wide, wn);
        sb_free(&line);
        if (cwd && cwd[0]) MultiByteToWideChar(CP_UTF8, 0, cwd, -1, wcwd, NP);
        sa.nLength = sizeof(sa);
        sa.lpSecurityDescriptor = NULL;
        sa.bInheritHandle = TRUE;
        if (!CreatePipe(&rd, &wr, &sa, 0)) { br_free(wide); return false; }
        SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
        memset(&si, 0, sizeof(si));
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = si.hStdError = wr;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        memset(&pi, 0, sizeof(pi));
        ok = CreateProcessW(NULL, wide, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, cwd && cwd[0] ? wcwd : NULL, &si, &pi);
        br_free(wide);
        CloseHandle(wr);
        if (!ok) { CloseHandle(rd); return false; }
        while (ReadFile(rd, buf, (DWORD)sizeof(buf) - 1, &got, NULL) && got > 0) { buf[got] = 0; sb_add(&out, buf); }
        CloseHandle(rd);
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        *status = (int)code;
    }
#else
    {
        sbuf line = { 0 };
        FILE *p;
        char buf[4096];
        size_t got;
        int rc;
        if (cwd && cwd[0]) { sb_add(&line, "cd"); sb_arg(&line, cwd); sb_add(&line, " && "); }
        sb_add(&line, "{ ");
        sb_add(&line, command);
        sb_add(&line, " ; } 2>&1");
        if (line.bad) { sb_free(&line); return false; }
        fflush(stdout);
        fflush(stderr);
        p = popen(line.s, "r");
        sb_free(&line);
        if (!p) return false;
        while ((got = fread(buf, 1, sizeof(buf) - 1, p)) > 0) { buf[got] = 0; sb_add(&out, buf); }
        rc = pclose(p);
        *status = rc == -1 ? -1 : WIFEXITED(rc) ? WEXITSTATUS(rc) : 128;
    }
#endif
    if (output) *output = out.s ? out.s : NULL;
    else sb_free(&out);
    if (output && !*output) { char *empty = (char *)br_alloc(1); if (empty) empty[0] = 0; *output = empty; }
    return true;
}

static bool command_exists(const char *name) {
    char cmd[600];
    int status;
#if defined(_WIN32)
    if (strchr(name, '\\') || strchr(name, '/')) return br_fs_exists(name);
    snprintf(cmd, sizeof(cmd), "where %s >nul 2>nul", name);
#else
    if (strchr(name, '/')) return br_fs_exists(name);
    snprintf(cmd, sizeof(cmd), "command -v '%s' >/dev/null 2>&1", name);
#endif
    return ed_run_command(cmd, NULL, NULL, &status) && status == 0;
}

/* ---- where things are ------------------------------------------------------------------- */

static bool editor_dir(char *out, size_t size) {
    char exe[NP];
    if (!br_fs_executable_path(exe, sizeof(exe))) return false;
    br_path_parent(out, size, exe);
    return true;
}

static bool is_sdk(const char *dir) {
    char a[NP], b[NP], c[NP];
    br_path_join(a, sizeof(a), dir, "include/bronto/bronto.h");
    br_path_join(b, sizeof(b), dir, "src/bronto.c");
    br_path_join(c, sizeof(c), dir, "tools/player/main.c");
    return br_fs_exists(a) && br_fs_exists(b) && br_fs_exists(c);
}

bool ed_native_sdk(char *out, size_t size) {
    static const char *const places[] = { "sdk", "../Resources/sdk", "..", "../.." };
    char dir[NP], candidate[NP], abs[NP];
    size_t i;
    if (!editor_dir(dir, sizeof(dir))) return false;
    for (i = 0; i < BR_ARRAY_COUNT(places); ++i) {
        br_path_join(candidate, sizeof(candidate), dir, places[i]);
        if (is_sdk(candidate) && br_fs_absolute(candidate, abs, sizeof(abs))) { snprintf(out, size, "%s", abs); return true; }
    }
    return false;
}

static const char *project_root(const editor *ed) { return ed->assets.root[0] ? ed->assets.root : "."; }

/* <project>/build/native/<sub>, made if needed. */
static bool native_dir(const editor *ed, const char *sub, char *out, size_t size) {
    char root[NP], a[NP], b[NP];
    if (!br_fs_absolute(project_root(ed), root, sizeof(root))) return false;
    br_path_join(a, sizeof(a), root, "build");
    br_fs_mkdir(a);
    br_path_join(b, sizeof(b), a, "native");
    br_fs_mkdir(b);
    if (!sub || !sub[0]) { snprintf(out, size, "%s", b); return true; }
    br_path_join(out, size, b, sub);
    br_fs_mkdir(out);
    return br_fs_is_dir(out);
}

/* ---- the sources ------------------------------------------------------------------------- */

typedef struct source_list {
    char rel[MAX_SOURCES][256];          /* .c files, relative to the project ('/' between parts) */
    int count;
    int64_t stamp;                       /* every .c and .h: names, sizes and times */
} source_list;

static void scan_dir(const char *root, const char *rel, int depth, source_list *out) {
    char full[NP];
    br_dir_list l;
    int i;
    br_path_join(full, sizeof(full), root, rel);
    if (depth > 8 || !br_fs_list(full, false, &l)) return;
    for (i = 0; i < l.count; ++i) {
        const br_dir_entry *e = &l.entries[i];
        char sub[NP];
        size_t k;
        snprintf(sub, sizeof(sub), "%s/%s", rel, e->name);
        if (e->is_dir) { scan_dir(root, sub, depth + 1, out); continue; }
        if (!br_path_has_ext(e->name, ".c") && !br_path_has_ext(e->name, ".h")) continue;
        {   /* (unsigned: the sum wraps around, as a hash does) */
            uint64_t h = (uint64_t)out->stamp * 31u + (uint64_t)e->modified * 7u + (uint64_t)e->size;
            for (k = 0; sub[k]; ++k) h = h * 3u + (unsigned char)sub[k];
            out->stamp = (int64_t)(h & 0x7fffffffffffffffull);
        }
        k = strlen(sub);
        if (br_path_has_ext(e->name, ".c") && out->count < MAX_SOURCES && k < sizeof(out->rel[0])) {
            memcpy(out->rel[out->count], sub, k + 1);
            out->count++;
        }
    }
    br_dir_list_free(&l);
}

static void list_sources(const editor *ed, source_list *out) {
    char dir[NP];
    const char *code = ed->project.code_dir[0] ? ed->project.code_dir : "src";
    memset(out, 0, sizeof(*out));
    br_path_join(dir, sizeof(dir), project_root(ed), BR_PROJECT_FILE);
    if (!br_fs_exists(dir)) return;                          /* only a project's code (not any folder's src/) */
    br_path_join(dir, sizeof(dir), project_root(ed), code);
    if (!br_fs_is_dir(dir)) return;
    scan_dir(project_root(ed), code, 0, out);
}

bool ed_native_has_code(editor *ed) {
    source_list *l = (source_list *)br_alloc(sizeof(source_list));
    bool has;
    if (!l) return false;
    list_sources(ed, l);
    has = l->count > 0;
    br_free(l);
    return has;
}

/* The BR_GAME_SETUP(name) functions in the sources, into build/native/setup.c. */
static int write_setup(const editor *ed, const source_list *l, char *path, size_t size) {
    char dir[NP], names[128][64];
    int count = 0, i, j;
    sbuf b = { 0 };
    bool ok;
    for (i = 0; i < l->count; ++i) {
        char full[NP], *text, *p;
        size_t n;
        br_path_join(full, sizeof(full), project_root(ed), l->rel[i]);
        if (!(text = (char *)br_file_read_all(full, &n))) continue;
        for (p = text; (p = strstr(p, "BR_GAME_SETUP(")) != NULL; ++p) {
            char name[64];
            size_t k = 0;
            const char *q = p + 14, *line = p;
            while (line > text && line[-1] != '\n') --line;     /* (not in a // comment or a #define) */
            while (line < p && (*line == ' ' || *line == '\t')) ++line;
            if (*line == '#' || (line[0] == '/' && (line[1] == '/' || line[1] == '*')) || *line == '*') continue;
            while (*q == ' ') ++q;
            while ((isalnum((unsigned char)*q) || *q == '_') && k + 1 < sizeof(name)) name[k++] = *q++;
            name[k] = 0;
            while (*q == ' ') ++q;
            if (!k || *q != ')') continue;
            for (j = 0; j < count; ++j) if (strcmp(names[j], name) == 0) break;
            if (j < count) {
                fprintf(stderr, "[ERROR] C: %s: BR_GAME_SETUP(%s) is in the project twice: setup names must be unique\n", l->rel[i], name);
                continue;
            }
            if (count < (int)BR_ARRAY_COUNT(names)) snprintf(names[count++], sizeof(names[0]), "%s", name);
        }
        br_free(text);
    }
    if (!native_dir(ed, NULL, dir, sizeof(dir))) return -1;
    br_path_join(path, size, dir, "setup.c");
    sb_add(&b, "/* Written by the Bronto editor from the BR_GAME_SETUP functions in the project's code. Don't edit it:\n"
               " * it's written again each time the code is compiled. */\n#include \"bronto/game.h\"\n\n");
    for (i = 0; i < count; ++i) sb_addf(&b, "void br_game_setup_%s(void);\n", names[i]);
    sb_add(&b, "\nBR_GAME_EXPORT void bronto_game_setup(void) {\n");
    for (i = 0; i < count; ++i) sb_addf(&b, "    br_game_setup_%s();\n", names[i]);
    sb_add(&b, "}\n");
    ok = !b.bad;
    if (ok) {                                                   /* (unchanged: left alone, so its time stays) */
        size_t n;
        char *old = (char *)br_file_read_all(path, &n);
        if (!old || n != b.n || memcmp(old, b.s, n) != 0) ok = br_file_write_all(path, b.s, b.n);
        br_free(old);
    }
    sb_free(&b);
    return ok ? count : -1;
}

/* ---- compilers ---------------------------------------------------------------------------- */

typedef enum cc_kind { CC_GNU = 0, CC_MSVC } cc_kind;

typedef struct ed_cc {
    cc_kind kind;
    char cmd[NP];                        /* the compiler, as a command ("cc", "C:/LLVM/bin/clang.exe") */
    char env[NP];                        /* run first, in the same shell: Visual Studio's vcvars ("" = none) */
    char name[64];                       /* for messages */
} ed_cc;

static cc_kind kind_of(const char *cmd) {
    char stem[128];
    br_path_stem(stem, sizeof(stem), cmd);
    return strcmp(stem, "cl") == 0 || strcmp(stem, "CL") == 0 ? CC_MSVC : CC_GNU;
}

static void set_cc(ed_cc *cc, const char *cmd, const char *env) {
    char stem[64];
    snprintf(cc->cmd, sizeof(cc->cmd), "%s", cmd);
    snprintf(cc->env, sizeof(cc->env), "%s", env ? env : "");
    cc->kind = kind_of(cmd);
    br_path_stem(stem, sizeof(stem), cmd);
    snprintf(cc->name, sizeof(cc->name), "%s", cc->kind == CC_MSVC ? "MSVC (cl)" : stem);
}

#if defined(_WIN32)
/* Visual Studio's C compiler, found with vswhere when cl isn't on the PATH (the
 * editor started from the Start menu): its vcvars batch file sets it up. */
static bool find_visual_studio(char *vcvars, size_t size) {
    const char *pf = getenv("ProgramFiles(x86)");
    char vswhere[NP], cmd[NP + 200], *out = NULL;
    int status;
    size_t n;
    if (!pf) pf = "C:\\Program Files (x86)";
    snprintf(vswhere, sizeof(vswhere), "%s\\Microsoft Visual Studio\\Installer\\vswhere.exe", pf);
    if (!br_fs_exists(vswhere)) return false;
    snprintf(cmd, sizeof(cmd), "\"%s\" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath", vswhere);
    if (!ed_run_command(cmd, NULL, &out, &status) || status != 0 || !out) { br_free(out); return false; }
    n = strlen(out);
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == ' ')) out[--n] = 0;
#  if defined(_M_ARM64)
    snprintf(vcvars, size, "%s\\VC\\Auxiliary\\Build\\vcvarsarm64.bat", out);
#  else
    snprintf(vcvars, size, "%s\\VC\\Auxiliary\\Build\\vcvars64.bat", out);
#  endif
    br_free(out);
    return br_fs_exists(vcvars);
}
#endif

/* The compiler for this computer's builds (and the code the editor loads). */
static bool find_host_cc(const editor *ed, ed_cc *cc, char *why, size_t why_size) {
    memset(cc, 0, sizeof(*cc));
    if (ed->project.compiler[0]) {
        set_cc(cc, ed->project.compiler, NULL);
#if defined(_WIN32)
        if (cc->kind == CC_MSVC && !command_exists(cc->cmd)) {      /* "cl", but not set up: Visual Studio's */
            char vcvars[NP];
            if (find_visual_studio(vcvars, sizeof(vcvars))) { snprintf(cc->env, sizeof(cc->env), "call \"%s\" >nul", vcvars); return true; }
        }
#endif
        if (!command_exists(cc->cmd)) { snprintf(why, why_size, "the compiler %.200s (Build Settings) isn't there", cc->cmd); return false; }
        return true;
    }
#if defined(_WIN32)
#  if defined(_MSC_VER)                                      /* an editor built with MSVC: code for it comes from cl */
    {
        char vcvars[NP];
        if (command_exists("cl")) { set_cc(cc, "cl", NULL); return true; }
        if (find_visual_studio(vcvars, sizeof(vcvars))) {
            char env[NP + 20];
            snprintf(env, sizeof(env), "call \"%s\" >nul", vcvars);
            set_cc(cc, "cl", env);
            return true;
        }
        snprintf(why, why_size, "no C compiler: install Visual Studio (or its Build Tools) with \"Desktop development with C++\"");
        return false;
    }
#  else
    if (command_exists("gcc")) { set_cc(cc, "gcc", NULL); return true; }
    if (command_exists("clang")) { set_cc(cc, "clang", NULL); return true; }
    snprintf(why, why_size, "no C compiler: put MinGW's gcc on the PATH (or name one in Build Settings)");
    return false;
#  endif
#else
    {
        static const char *const names[] = { "cc", "gcc", "clang" };
        size_t i;
        for (i = 0; i < BR_ARRAY_COUNT(names); ++i)
            if (command_exists(names[i])) { set_cc(cc, names[i], NULL); return true; }
#  if defined(__APPLE__)
        snprintf(why, why_size, "no C compiler: install Apple's command line tools (xcode-select --install)");
#  else
        snprintf(why, why_size, "no C compiler: install gcc or clang (or name one in Build Settings)");
#  endif
        return false;
    }
#endif
}

/* clang that can make WebAssembly, for web builds. */
static bool find_web_cc(const editor *ed, ed_cc *cc, char *why, size_t why_size) {
    static const char *const places[] = {
        "/opt/homebrew/opt/llvm/bin/clang", "/usr/local/opt/llvm/bin/clang",
        "C:/Program Files/LLVM/bin/clang.exe", "clang", "clang-18", "clang-19", "clang-17",
    };
    size_t i;
    memset(cc, 0, sizeof(*cc));
    if (ed->project.web_compiler[0]) {
        set_cc(cc, ed->project.web_compiler, NULL);
        if (command_exists(cc->cmd)) return true;
        snprintf(why, why_size, "the web compiler %.200s (Build Settings) isn't there", cc->cmd);
        return false;
    }
    for (i = 0; i < BR_ARRAY_COUNT(places); ++i) {
        if (!command_exists(places[i])) continue;
        set_cc(cc, places[i], NULL);
        cc->kind = CC_GNU;
        return true;
    }
    snprintf(why, why_size, "no clang for WebAssembly: install LLVM's clang and lld (macOS: brew install llvm lld)");
    return false;
}

/* The start of a command: the compiler, after its setup. */
static void sb_cc(sbuf *b, const ed_cc *cc) {
    if (cc->env[0]) { sb_add(b, cc->env); sb_add(b, " && "); }
    if (strchr(cc->cmd, ' ')) { sb_add(b, "\""); sb_add(b, cc->cmd); sb_add(b, "\""); }
    else sb_add(b, cc->cmd);
}

/* ---- what compilers say ------------------------------------------------------------------- */

static ed_native *g_native;               /* (for the code tab's marks) */

static void copy_text(char *out, size_t size, const char *text) {
    size_t k = strlen(text);
    if (k >= size) k = size - 1;
    memcpy(out, text, k);
    out[k] = 0;
}

static void add_diag(ed_native *n, const char *file, int line, bool error, const char *text) {
    ed_native_diag *d;
    if (n->diag_count >= ED_NATIVE_DIAG) return;
    d = &n->diag[n->diag_count++];
    copy_text(d->file, sizeof(d->file), file);
    d->line = line;
    d->error = error;
    snprintf(d->text, sizeof(d->text), "%s", text);
}

/* "file:12:5: error: ..." (gcc, clang) or "file(12): error C2065: ..." / "file(12,5): ..." (MSVC). */
static bool parse_location(char *s, char *file, size_t size, int *line, const char **rest) {
    char *p;
    for (p = s; *p; ++p) {
        if (p > s && *p == '(' && isdigit((unsigned char)p[1])) {      /* MSVC */
            char *q = p + 1;
            while (isdigit((unsigned char)*q) || *q == ',') ++q;
            if (q[0] == ')' && q[1] == ':') {
                size_t len = (size_t)(p - s);
                if (len >= size) return false;
                memcpy(file, s, len);
                file[len] = 0;
                *line = atoi(p + 1);
                *rest = q + 2;
                return true;
            }
        }
        if (*p == ':' && p > s && isdigit((unsigned char)p[1]) && !(p == s + 1 && isalpha((unsigned char)s[0]))) {   /* gcc (not C:\) */
            char *q = p + 1;
            size_t len = (size_t)(p - s);
            while (isdigit((unsigned char)*q)) ++q;
            if (*q != ':') continue;
            if (len >= size) return false;
            memcpy(file, s, len);
            file[len] = 0;
            *line = atoi(p + 1);
            ++q;
            while (isdigit((unsigned char)*q)) ++q;                  /* the column */
            if (*q == ':') ++q;
            *rest = q;
            return br_path_has_ext(file, ".c") || br_path_has_ext(file, ".h");
        }
    }
    return false;
}

/* The compiler's output, line by line, to the console: errors and warnings (with
 * their place, which opens the file), and the rest as it is. Returns the errors. */
static int report(editor *ed, const char *output, bool verbose) {
    ed_native *n = &ed->native;
    const char *p = output;
    int errors = 0;
    while (p && *p) {
        const char *end = strchr(p, '\n');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        char line[1024], file[NP];
        const char *rest = NULL;
        int at = 0;
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = 0;
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' ')) line[--len] = 0;
        p = end ? end + 1 : NULL;
        if (!line[0]) continue;
        if (parse_location(line, file, sizeof(file), &at, &rest)) {
            char *f;
            bool error, warning;
            while (*rest == ' ') ++rest;
            error = strncmp(rest, "error", 5) == 0 || strncmp(rest, "fatal error", 11) == 0;
            warning = strncmp(rest, "warning", 7) == 0;
            for (f = file; *f; ++f) if (*f == '\\') *f = '/';
            if (error || warning) {
                add_diag(n, file, at, error, rest);
                if (error) ++errors;
                fprintf(error ? stderr : stdout, "%s C: %s:%d: %s\n", error ? "[ERROR]" : "[WARN ]", file, at, rest);
            } else if (verbose) {
                printf("C: %s:%d: %s\n", file, at, rest);           /* (notes) */
            }
            continue;
        }
        if (strstr(line, "error") || strstr(line, "undefined reference") || strstr(line, "unresolved external") ||
            strstr(line, "Undefined symbols") || strstr(line, "ld returned")) {
            fprintf(stderr, "[ERROR] C: %s\n", line);
            ++errors;
        } else if (verbose && line[0] != ' ' && !strchr("|~^", line[0])) {
            printf("C: %s\n", line);
        }
    }
    return errors;
}

/* Runs a compile in the project folder (so messages name files as the project
 * does). False if it failed; its messages are in the console either way. */
static bool compile(editor *ed, const char *what, sbuf *cmd) {
    char *out = NULL;
    int status, errors;
    if (cmd->bad) { fprintf(stderr, "[ERROR] C: %s: out of memory\n", what); return false; }
    if (getenv("BRONTO_SHOW_COMMANDS")) printf("C: %s\n", cmd->s);
    if (!ed_run_command(cmd->s, project_root(ed), &out, &status)) {
        fprintf(stderr, "[ERROR] C: %s: couldn't run the compiler\n", what);
        return false;
    }
    errors = report(ed, out ? out : "", status != 0);
    br_free(out);
    if (status != 0 && errors == 0) fprintf(stderr, "[ERROR] C: %s failed (exit status %d)\n", what, status);
    return status == 0;
}

/* Common options: the language, the headers, the project's own. */
static void sb_common(sbuf *b, const editor *ed, const ed_cc *cc, const char *sdk, bool user_flags) {
    char inc[NP];
    br_path_join(inc, sizeof(inc), sdk, "include");
    if (cc->kind == CC_MSVC) {
        sb_add(b, " /nologo /W3 /fp:precise /D_CRT_SECURE_NO_WARNINGS /wd4100");
        sb_add(b, ed->project.debug_build ? " /Od /Zi" : " /O2");
        sb_add(b, " /I");
        sb_arg(b, inc);
    } else {
        sb_add(b, " -std=c99 -ffp-contract=off");
        sb_add(b, ed->project.debug_build ? " -O0 -g" : " -O2");
        sb_add(b, " -I");
        sb_arg(b, inc);
    }
    if (user_flags) {
        const char *code = ed->project.code_dir[0] ? ed->project.code_dir : "src";
        sb_add(b, cc->kind == CC_MSVC ? " /I" : " -I");
        sb_arg(b, code);
        sb_add(b, cc->kind == CC_MSVC ? " /W3" : " -Wall");
        sb_raw(b, ed->project.cflags);
    }
}

static void sb_sources(sbuf *b, const source_list *l, const char *setup) {
    int i;
    for (i = 0; i < l->count; ++i) sb_arg(b, l->rel[i]);
    sb_arg(b, setup);
}

/* ---- the code in the editor --------------------------------------------------------------- */

#if defined(_WIN32)
#  define MODULE_EXT ".dll"
#elif defined(__APPLE__)
#  define MODULE_EXT ".dylib"
#else
#  define MODULE_EXT ".so"
#endif

static void *module_open(const char *path, char *err, size_t size) {
#if defined(_WIN32)
    wchar_t wide[NP], *w;
    HMODULE h;
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, NP);
    for (w = wide; *w; ++w) if (*w == L'/') *w = L'\\';            /* (LoadLibrary wants backslashes) */
    h = LoadLibraryW(wide);
    if (!h) snprintf(err, size, "Windows error %lu", (unsigned long)GetLastError());
    return (void *)h;
#else
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) snprintf(err, size, "%s", dlerror());
    return h;
#endif
}

typedef void (*setup_fn)(void);

static setup_fn module_setup(void *lib) {
#if defined(_WIN32)
    FARPROC f = GetProcAddress((HMODULE)lib, "bronto_game_setup");
    setup_fn fn;
    memcpy(&fn, &f, sizeof(fn));
    return fn;
#else
    void *f = dlsym(lib, "bronto_game_setup");
    setup_fn fn;
    memcpy(&fn, &f, sizeof(fn));                     /* (a data pointer to a function pointer, the portable way) */
    return fn;
#endif
}

/* The import library for the engine's functions in the editor (Windows). */
static bool editor_import_lib(const ed_cc *cc, char *out, size_t size) {
    char dir[NP];
    if (!editor_dir(dir, sizeof(dir))) return false;
    br_path_join(out, size, dir, cc->kind == CC_MSVC ? "editor.lib" : "libeditor.a");
    if (br_fs_exists(out)) return true;
    {
        char sdk[NP], sub[NP];
        if (!ed_native_sdk(sdk, sizeof(sdk))) return false;
        br_path_join(sub, sizeof(sub), sdk, cc->kind == CC_MSVC ? "lib/editor.lib" : "lib/libeditor.a");
        snprintf(out, size, "%s", sub);
        return br_fs_exists(out);
    }
}

/* Removes the libraries compiled earlier (the one loaded stays: Windows keeps it locked). */
static void clean_modules(const char *dir, const char *keep) {
    br_dir_list l;
    int i;
    if (!br_fs_list(dir, false, &l)) return;
    for (i = 0; i < l.count; ++i) {
        char full[NP];
        if (l.entries[i].is_dir || strncmp(l.entries[i].name, "game_", 5) != 0) continue;
        br_path_join(full, sizeof(full), dir, l.entries[i].name);
        if (keep && strncmp(full, keep, strlen(full) - strlen(br_path_ext(full))) == 0) continue;
        br_fs_remove(full);
    }
    br_dir_list_free(&l);
}

static bool compile_module(editor *ed, const source_list *l, const char *setup, char *module, size_t size) {
    char dir[NP], sdk[NP], why[300], name[64];
    ed_cc cc;
    sbuf b = { 0 };
    bool ok;
    static bool cleaned;
    if (!ed_native_sdk(sdk, sizeof(sdk))) {
        fprintf(stderr, "[ERROR] C: the engine's headers and sources aren't beside the editor (sdk/): reinstall it with make install\n");
        return false;
    }
    if (!find_host_cc(ed, &cc, why, sizeof(why))) { fprintf(stderr, "[ERROR] C: %s\n", why); return false; }
    if (!native_dir(ed, "editor", dir, sizeof(dir))) { fprintf(stderr, "[ERROR] C: can't make build/native/editor in the project\n"); return false; }
    if (!cleaned) { clean_modules(dir, NULL); cleaned = true; }
    snprintf(name, sizeof(name), "game_%d%s", ed->native.generation + 1, MODULE_EXT);
    br_path_join(module, size, dir, name);
    sb_cc(&b, &cc);
    sb_common(&b, ed, &cc, sdk, true);
    if (cc.kind == CC_MSVC) {
        char lib[NP], obj[NP + 4];
        if (!editor_import_lib(&cc, lib, sizeof(lib))) {
            fprintf(stderr, "[ERROR] C: editor.lib (the editor's functions, for the game's code) isn't beside the editor: build it again with build.bat tools\n");
            sb_free(&b);
            return false;
        }
        snprintf(obj, sizeof(obj), "%s\\\\", dir);                  /* (a folder: \\ before the quote is one \) */
        sb_add(&b, " /LD /DBR_GAME_MODULE");
        sb_sources(&b, l, setup);
        sb_opt(&b, "/Fe", module);
        sb_opt(&b, "/Fo", obj);
        sb_add(&b, " /link");
        sb_arg(&b, lib);
        sb_raw(&b, ed->project.ldflags);
    } else {
        sb_add(&b, " -DBR_GAME_MODULE -fPIC");
#if defined(__APPLE__)
        sb_add(&b, " -dynamiclib -undefined dynamic_lookup");
#else
        sb_add(&b, " -shared");
#endif
        sb_sources(&b, l, setup);
        sb_add(&b, " -o");
        sb_arg(&b, module);
#if defined(_WIN32)
        {
            char lib[NP];
            if (!editor_import_lib(&cc, lib, sizeof(lib))) {
                fprintf(stderr, "[ERROR] C: libeditor.a (the editor's functions, for the game's code) isn't beside the editor: build it again with make tools\n");
                sb_free(&b);
                return false;
            }
            sb_arg(&b, lib);
        }
#endif
        sb_raw(&b, ed->project.ldflags);
        sb_add(&b, " -lm");
    }
    printf("C: compiling %d file%s with %s\n", l->count, l->count == 1 ? "" : "s", cc.name);
    ok = compile(ed, "compiling the game's code", &b);
    sb_free(&b);
    return ok;
}

/* Loads the newest code: forgets the old one's hooks, runs the new setups.
 * (The old library stays loaded: the scene's undo steps and anything still
 * pointing into it stay safe; it's one more small library per compile.) */
static bool load_module(editor *ed, const char *path) {
    char err[400];
    void *lib = module_open(path, err, sizeof(err));
    setup_fn setup;
    if (!lib) { fprintf(stderr, "[ERROR] C: couldn't load the compiled code: %s\n", err); return false; }
    if (!(setup = module_setup(lib))) { fprintf(stderr, "[ERROR] C: the compiled code has no bronto_game_setup\n"); return false; }
    br_game_clear();
    setup();
    ed->native.lib = lib;
    ed->native.generation++;
    ed->native.loaded = true;
    return true;
}

const ed_native_diag *ed_native_diag_for(const char *path) {
    int i;
    size_t n = strlen(path);
    if (!g_native) return NULL;
    for (i = 0; i < g_native->diag_count; ++i) {             /* the first error in the file, else the first warning */
        const ed_native_diag *d = &g_native->diag[i];
        size_t k = strlen(d->file);
        if (d->error && k <= n && strcmp(path + n - k, d->file) == 0 && (k == n || path[n - k - 1] == '/' || path[n - k - 1] == '\\')) return d;
    }
    for (i = 0; i < g_native->diag_count; ++i) {
        const ed_native_diag *d = &g_native->diag[i];
        size_t k = strlen(d->file);
        if (k <= n && strcmp(path + n - k, d->file) == 0 && (k == n || path[n - k - 1] == '/' || path[n - k - 1] == '\\')) return d;
    }
    return NULL;
}

bool ed_native_refresh(editor *ed, bool force) {
    ed_native *n = &ed->native;
    source_list *l;
    char setup[NP], module[NP];
    int setups;
    bool ok = false;
    g_native = n;
    if (ed->build_cli) return false;                         /* (editor --build compiles the game's own copy) */
    if (!(l = (source_list *)br_alloc(sizeof(source_list)))) return false;
    list_sources(ed, l);
    n->has_code = l->count > 0;
    if (!n->has_code) {
        if (n->loaded) {                                     /* the code is gone: so are its hooks */
            br_game_clear();
            n->loaded = false;
            n->stamp = 0;
            snprintf(n->summary, sizeof(n->summary), "No C code");
            br_free(l);
            return true;
        }
        br_free(l);
        return false;
    }
    if (!force && l->stamp == n->stamp) { br_free(l); return false; }
    n->stamp = l->stamp;
    n->diag_count = 0;
    if ((setups = write_setup(ed, l, setup, sizeof(setup))) < 0) {
        fprintf(stderr, "[ERROR] C: can't write build/native/setup.c in the project\n");
    } else if (compile_module(ed, l, setup, module, sizeof(module)) && load_module(ed, module)) {
        ok = true;
        n->failed = false;
        n->setups = setups;
        snprintf(n->summary, sizeof(n->summary), "C code: %d file%s, %d setup%s, %d world hook%s", l->count, l->count == 1 ? "" : "s",
                 setups, setups == 1 ? "" : "s", br_game_hook_count(), br_game_hook_count() == 1 ? "" : "s");
        printf("C: loaded the game's code (%d file%s, %d setup%s)\n", l->count, l->count == 1 ? "" : "s", setups, setups == 1 ? "" : "s");
    }
    if (!ok) {
        n->failed = true;
        snprintf(n->summary, sizeof(n->summary), "The C code didn't compile%s - see the console", n->loaded ? " (Play runs the last code that did)" : "");
    }
    br_free(l);
    return ok;
}

/* ---- the code in a game -------------------------------------------------------------------- */

/* The newest change among a folder's files (the engine's sources, to know when to compile it again). */
static int64_t newest(const char *dir, int depth) {
    br_dir_list l;
    int64_t best = 0;
    int i;
    if (depth > 6 || !br_fs_list(dir, false, &l)) return 0;
    for (i = 0; i < l.count; ++i) {
        char full[NP];
        int64_t t;
        br_path_join(full, sizeof(full), dir, l.entries[i].name);
        t = l.entries[i].is_dir ? newest(full, depth + 1) : l.entries[i].modified;
        if (t > best) best = t;
    }
    br_dir_list_free(&l);
    return best;
}

/* Compiles a file of the engine's (the single-file engine, the player's main...)
 * into `obj` unless the stamp says it's already there as asked. */
static bool engine_object(editor *ed, const ed_cc *cc, const char *sdk, const char *source, const char *obj, const char *extra,
                          bool web, const char *web_flags) {
    char stamp_path[NP + 8], stamp[6 * NP], src_dir[NP], inc_dir[NP];
    size_t n;
    char *old;
    sbuf b = { 0 };
    bool ok;
    br_path_join(src_dir, sizeof(src_dir), sdk, "src");
    br_path_join(inc_dir, sizeof(inc_dir), sdk, "include");
    snprintf(stamp_path, sizeof(stamp_path), "%s.stamp", obj);
    snprintf(stamp, sizeof(stamp), "%s|%s|%s|%s|%d|%lld|%lld|%s", cc->cmd, cc->env, source, extra ? extra : "", ed->project.debug_build ? 1 : 0,
             (long long)newest(src_dir, 0), (long long)newest(inc_dir, 0), web_flags ? web_flags : "");
    old = (char *)br_file_read_all(stamp_path, &n);
    if (old && br_fs_exists(obj) && n == strlen(stamp) && memcmp(old, stamp, n) == 0) { br_free(old); return true; }
    br_free(old);
    printf("C: compiling %s (once per project and compiler)\n", br_path_name(source));
    sb_cc(&b, cc);
    if (web) sb_raw(&b, web_flags);
    else sb_common(&b, ed, cc, sdk, false);
    sb_raw(&b, extra);
    sb_add(&b, cc->kind == CC_MSVC ? " /w /c" : " -w -c");          /* (the engine's own warnings aren't the game's business) */
    sb_arg(&b, source);
    if (cc->kind == CC_MSVC) sb_opt(&b, "/Fo", obj);
    else { sb_add(&b, " -o"); sb_arg(&b, obj); }
    ok = compile(ed, br_path_name(source), &b);
    sb_free(&b);
    if (ok) br_file_write_all(stamp_path, stamp, strlen(stamp));
    else br_fs_remove(stamp_path);
    return ok;
}

static bool native_player(editor *ed, const source_list *l, const char *setup, char *out, size_t size) {
    char sdk[NP], dir[NP], why[300], engine[NP], player_obj[NP], src[NP], main_c[NP];
    const char *ext = ed_host_target() == ED_TARGET_WINDOWS ? ".exe" : "";
    ed_cc cc;
    sbuf b = { 0 };
    bool ok;
    if (!ed_native_sdk(sdk, sizeof(sdk))) {
        fprintf(stderr, "[ERROR] C: the engine's headers and sources aren't beside the editor (sdk/): reinstall it with make install\n");
        return false;
    }
    if (!find_host_cc(ed, &cc, why, sizeof(why))) { fprintf(stderr, "[ERROR] C: %s\n", why); return false; }
    if (!native_dir(ed, ed_target_name(ed_host_target()), dir, sizeof(dir))) return false;
    br_path_join(src, sizeof(src), sdk, "src/bronto.c");
    br_path_join(main_c, sizeof(main_c), sdk, "tools/player/main.c");
    br_path_join(engine, sizeof(engine), dir, cc.kind == CC_MSVC ? "bronto.obj" : "bronto.o");
    br_path_join(player_obj, sizeof(player_obj), dir, cc.kind == CC_MSVC ? "player.obj" : "player.o");
    if (!engine_object(ed, &cc, sdk, src, engine, NULL, false, NULL)) return false;
    if (!engine_object(ed, &cc, sdk, main_c, player_obj, cc.kind == CC_MSVC ? "/DBR_PLAYER_NATIVE" : "-DBR_PLAYER_NATIVE", false, NULL)) return false;
    {
        char name[64];
        snprintf(name, sizeof(name), "bronto_player%s", ext);
        br_path_join(out, size, dir, name);
    }
    sb_cc(&b, &cc);
    sb_common(&b, ed, &cc, sdk, true);
    sb_sources(&b, l, setup);
    sb_arg(&b, player_obj);
    sb_arg(&b, engine);
    if (cc.kind == CC_MSVC) {
        char obj[NP + 4];
        snprintf(obj, sizeof(obj), "%s\\\\", dir);
        sb_opt(&b, "/Fe", out);
        sb_opt(&b, "/Fo", obj);
        sb_add(&b, " /link");
        sb_raw(&b, ed->project.ldflags);
    } else {
        sb_add(&b, " -o");
        sb_arg(&b, out);
        sb_raw(&b, ed->project.ldflags);
#if defined(__APPLE__)
        sb_add(&b, " -framework Cocoa -framework QuartzCore -framework AudioToolbox -framework GameController -lm");
#elif defined(_WIN32)
        sb_add(&b, " -luser32 -lgdi32 -lm");
#else
        sb_add(&b, " -lm -ldl -pthread");
#endif
    }
    printf("C: building the player with the game's code (%s)\n", cc.name);
    ok = compile(ed, "building the game", &b);
    sb_free(&b);
    return ok;
}

/* The web player with the game's code: the engine's C library and the engine
 * for WebAssembly (kept between builds), then the game. */
static bool native_web_player(editor *ed, const source_list *l, const char *setup, char *out, size_t size) {
    static const char *const libc_files[] = { "libc.c", "malloc.c", "math.c", "memfs.c", "sjlj.c", "start.c", "stdio.c" };
    char sdk[NP], dir[NP], why[300], flags[3 * NP], web_inc[NP], inc[NP], obj[NP], src[NP];
    ed_cc cc;
    sbuf objs = { 0 }, b = { 0 };
    size_t i;
    bool ok = true;
    if (!ed_native_sdk(sdk, sizeof(sdk))) {
        fprintf(stderr, "[ERROR] C: the engine's headers and sources aren't beside the editor (sdk/): reinstall it with make install\n");
        return false;
    }
    if (!find_web_cc(ed, &cc, why, sizeof(why))) { fprintf(stderr, "[ERROR] C: %s\n", why); return false; }
    if (!native_dir(ed, "Web", dir, sizeof(dir))) return false;
    br_path_join(inc, sizeof(inc), sdk, "include");
    br_path_join(web_inc, sizeof(web_inc), sdk, "src/platform/web/include");
    snprintf(flags, sizeof(flags),
             "--target=wasm32 -std=c99 %s -ffp-contract=off -I\"%s\" -nostdlibinc -isystem \"%s\" -mbulk-memory -mnontrapping-fptoint "
             "-msign-ext -mexception-handling -mllvm -wasm-enable-sjlj",
             ed->project.debug_build ? "-O0 -g" : "-O2", inc, web_inc);
    for (i = 0; ok && i < BR_ARRAY_COUNT(libc_files); ++i) {           /* the C library */
        char name[64];
        snprintf(name, sizeof(name), "src/platform/web/%s", libc_files[i]);
        br_path_join(src, sizeof(src), sdk, name);
        snprintf(name, sizeof(name), "libc_%s.o", libc_files[i]);
        br_path_join(obj, sizeof(obj), dir, name);
        ok = engine_object(ed, &cc, sdk, src, obj, "-fno-builtin", true, flags);
        sb_arg(&objs, obj);
    }
    if (ok) {                                                          /* longjmp's exception tag */
        br_path_join(src, sizeof(src), sdk, "src/platform/web/tag.s");
        br_path_join(obj, sizeof(obj), dir, "tag.o");
        ok = engine_object(ed, &cc, sdk, src, obj, NULL, true, "--target=wasm32 -mexception-handling");
        sb_arg(&objs, obj);
    }
    if (ok) {
        br_path_join(src, sizeof(src), sdk, "src/bronto.c");
        br_path_join(obj, sizeof(obj), dir, "bronto.o");
        ok = engine_object(ed, &cc, sdk, src, obj, NULL, true, flags);
        sb_arg(&objs, obj);
    }
    if (ok) {
        br_path_join(src, sizeof(src), sdk, "tools/player/main.c");
        br_path_join(obj, sizeof(obj), dir, "player.o");
        ok = engine_object(ed, &cc, sdk, src, obj, "-DBR_PLAYER_NATIVE", true, flags);
        sb_arg(&objs, obj);
    }
    if (ok) {
        br_path_join(out, size, dir, "bronto_player.wasm");
        sb_cc(&b, &cc);
        sb_raw(&b, flags);
        {
            const char *code = ed->project.code_dir[0] ? ed->project.code_dir : "src";
            sb_add(&b, " -I");
            sb_arg(&b, code);
            sb_add(&b, " -Wall");
            sb_raw(&b, ed->project.cflags);
        }
        sb_add(&b, " -nostdlib -Wl,--no-entry -Wl,--stack-first -Wl,-z,stack-size=4194304");
        if (!ed->project.debug_build) sb_add(&b, " -Wl,--strip-debug");
        sb_sources(&b, l, setup);
        sb_add(&b, objs.s ? objs.s : "");
        sb_add(&b, " -o");
        sb_arg(&b, out);
        printf("C: building the web player with the game's code (%s)\n", cc.name);
        ok = compile(ed, "building the web game", &b);
    }
    if (ok) {                                                          /* its page and runtime beside it */
        static const char *const runtime[] = { "bronto.js", "shell.html" };
        for (i = 0; ok && i < BR_ARRAY_COUNT(runtime); ++i) {
            char from[NP], to[NP], name[64];
            snprintf(name, sizeof(name), "tools/web/%s", runtime[i]);
            br_path_join(from, sizeof(from), sdk, name);
            br_path_join(to, sizeof(to), dir, runtime[i]);
            if (!br_fs_copy_file(from, to)) { fprintf(stderr, "[ERROR] C: can't copy %s\n", from); ok = false; }
        }
    }
    sb_free(&objs);
    sb_free(&b);
    return ok;
}

bool ed_native_player(editor *ed, ed_target t, char *out, size_t size) {
    source_list *l;
    char setup[NP];
    bool ok = false;
    g_native = &ed->native;
    if (!(l = (source_list *)br_alloc(sizeof(source_list)))) return false;
    list_sources(ed, l);
    ed->native.diag_count = 0;
    if (l->count == 0) fprintf(stderr, "[ERROR] C: no .c files in %s\n", ed->project.code_dir);
    else if (write_setup(ed, l, setup, sizeof(setup)) < 0) fprintf(stderr, "[ERROR] C: can't write build/native/setup.c in the project\n");
    else if (t == ED_TARGET_WEB) ok = native_web_player(ed, l, setup, out, size);
    else if (t == ed_host_target()) ok = native_player(ed, l, setup, out, size);
    else fprintf(stderr, "[ERROR] C: a game with C code for %s is built on %s: open the project in the editor there (or run editor --build %s there)\n",
                 ed_target_name(t), ed_target_name(t), t == ED_TARGET_MACOS ? "macos" : t == ED_TARGET_LINUX ? "linux" : "windows");
    br_free(l);
    return ok;
}

/* Describes the compilers for Build Settings. */
void ed_native_describe(editor *ed, char *host, size_t host_size, char *web, size_t web_size) {
    ed_cc cc;
    char why[300];
    if (find_host_cc(ed, &cc, why, sizeof(why))) snprintf(host, host_size, "%s%s", cc.name, cc.env[0] ? " (Visual Studio)" : "");
    else snprintf(host, host_size, "none: %s", why);
    if (find_web_cc(ed, &cc, why, sizeof(why))) snprintf(web, web_size, "%s", cc.cmd);
    else snprintf(web, web_size, "none: %s", why);
}

/* ---- Windows programs: console or window ------------------------------------------------------- */

/* The editor is a windowed program on Windows: with a job for the command line
 * (--build, --help...) it writes to the console it was started from, if that
 * didn't already give it somewhere to write (editor.com does). */
void ed_attach_console(void) {
#if defined(_WIN32)
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode, type = out && out != INVALID_HANDLE_VALUE ? GetFileType(out) : FILE_TYPE_UNKNOWN;
    if (type == FILE_TYPE_DISK || type == FILE_TYPE_PIPE) return;                 /* redirected: fine as it is */
    if (type == FILE_TYPE_CHAR && GetConsoleMode(out, &mode)) return;              /* a console it can write to */
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE *f;
        f = freopen("CONOUT$", "w", stdout);
        f = freopen("CONOUT$", "w", stderr);
        (void)f;
    }
#endif
}

/* A Windows program's header says whether it's a console program (3) or a
 * windowed one (2); Windows gives a console program a console window. Built
 * games are copies of the player, so the editor sets it as Build Settings say. */
bool ed_pe_set_subsystem(const char *exe, bool windowed) {
    FILE *f = fopen(exe, "r+b");
    unsigned char h[4];
    long pe;
    bool ok = false;
    if (!f) return false;
    if (fseek(f, 0x3c, SEEK_SET) == 0 && fread(h, 1, 4, f) == 4) {
        pe = (long)(h[0] | (h[1] << 8) | (h[2] << 16) | ((unsigned long)h[3] << 24));
        if (pe > 0 && fseek(f, pe, SEEK_SET) == 0 && fread(h, 1, 4, f) == 4 && memcmp(h, "PE\0\0", 4) == 0) {
            unsigned char sub[2];
            sub[0] = (unsigned char)(windowed ? 2 : 3);
            sub[1] = 0;
            /* the optional header starts after the 20-byte file header; its Subsystem is at 68 */
            ok = fseek(f, pe + 4 + 20 + 68, SEEK_SET) == 0 && fwrite(sub, 1, 2, f) == 2;
        }
    }
    fclose(f);
    return ok;
}
