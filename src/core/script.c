/*
 * Bronto Engine - BrontoScript: a small scripting language for entities
 * (see script.h for the overview, docs/scripting.md for the reference).
 *
 *   1. values and the heap   numbers, strings, vec2s, entities, lists; strings
 *                            and lists live on a per-world heap that is
 *                            garbage-collected (mark and sweep) between calls
 *   2. lexer                 source text -> tokens
 *   3. parser                tokens -> a syntax tree per function; local
 *                            variables get slots while parsing. Then the
 *                            type checker: optional annotations (var x: int,
 *                            func f(a: vec2) -> bool, :=), checked before the
 *                            script runs; @strict makes them required
 *   4. interpreter           walks the tree. Runtime errors longjmp back to
 *                            the callback that started the work, which is safe
 *                            because nothing on the C stack owns memory: the
 *                            collector only runs when no call is in progress
 *   5. the engine API        entity properties, components by name, input,
 *                            physics, sounds, spawning
 *   6. runtime               loaded files (re-read when they change),
 *                            instances per entity, the Script component's
 *                            hooks and systems, C behaviours
 */
#include "bronto/script.h"
#include "bronto/components.h"
#include "bronto/physics.h"
#include "bronto/hierarchy.h"
#include "bronto/serialize.h"
#include "bronto/assets.h"
#include "bronto/engine.h"
#include "bronto/fs.h"
#include "bronto/save.h"
#include "bronto/log.h"
#include "bronto/memory.h"

#include <ctype.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SC_MAX_DEPTH 200            /* nested calls */
#define SC_BUDGET 20000000L         /* tree nodes per callback before "took too long" */
#define SC_GC_BYTES (1u << 20)      /* collect after this much new heap */
#define SC_MAX_BEHAVIOURS 64

/* ======================================================================== */
/* 1. values and the heap                                                   */
/* ======================================================================== */

typedef enum { V_NIL = 0, V_BOOL, V_NUM, V_STR, V_VEC, V_ENT, V_LIST, V_COMP } sc_type;
enum { OBJ_STR = 1, OBJ_LIST };

typedef struct sc_obj { struct sc_obj *next; uint8_t kind, marked, pinned; } sc_obj;
typedef struct sc_str { sc_obj h; uint32_t len; char chars[1]; } sc_str;
struct sc_value;
typedef struct sc_list { sc_obj h; struct sc_value *items; uint32_t count, cap; } sc_list;

typedef struct sc_value {
    uint8_t t;
    union {
        bool b;
        double n;
        sc_str *s;
        struct { double x, y; } v;
        br_entity e;
        sc_list *l;
        struct { br_entity e; uint32_t c; } comp;
    } as;
} sc_value;

typedef struct sc_heap { sc_obj *objects; size_t bytes, since_gc; } sc_heap;

static sc_value sc_nil(void) { sc_value v; memset(&v, 0, sizeof(v)); return v; }
static sc_value sc_bool(bool b) { sc_value v = sc_nil(); v.t = V_BOOL; v.as.b = b; return v; }
static sc_value sc_num(double n) { sc_value v = sc_nil(); v.t = V_NUM; v.as.n = n; return v; }
static sc_value sc_vec(double x, double y) { sc_value v = sc_nil(); v.t = V_VEC; v.as.v.x = x; v.as.v.y = y; return v; }
static sc_value sc_ent(br_entity e) { sc_value v = sc_nil(); v.t = V_ENT; v.as.e = e; return v; }

static sc_str *sc_str_alloc(sc_heap *h, const char *s, size_t len, bool pinned) {
    sc_str *o = (sc_str *)br_alloc(sizeof(sc_str) + len);
    if (!o) return NULL;
    o->h.kind = OBJ_STR;
    o->h.marked = 0;
    o->h.pinned = pinned;
    o->len = (uint32_t)len;
    memcpy(o->chars, s, len);
    o->chars[len] = 0;
    if (!pinned) {
        o->h.next = h->objects;
        h->objects = &o->h;
        h->bytes += sizeof(sc_str) + len;
        h->since_gc += sizeof(sc_str) + len;
    } else {
        o->h.next = NULL;
    }
    return o;
}

static sc_list *sc_list_alloc(sc_heap *h) {
    sc_list *l = (sc_list *)br_alloc(sizeof(sc_list));
    if (!l) return NULL;
    memset(l, 0, sizeof(*l));
    l->h.kind = OBJ_LIST;
    l->h.next = h->objects;
    h->objects = &l->h;
    h->bytes += sizeof(sc_list);
    h->since_gc += sizeof(sc_list);
    return l;
}

static void sc_obj_free(sc_obj *o) {
    if (o->kind == OBJ_LIST) br_free(((sc_list *)o)->items);
    br_free(o);
}

static void sc_mark_value(const sc_value *v);
static void sc_mark_obj(sc_obj *o) {
    uint32_t i;
    if (!o || o->marked || o->pinned) return;
    o->marked = 1;
    if (o->kind == OBJ_LIST) {
        sc_list *l = (sc_list *)o;
        for (i = 0; i < l->count; ++i) sc_mark_value(&l->items[i]);
    }
}
static void sc_mark_value(const sc_value *v) {
    if (v->t == V_STR) sc_mark_obj(&v->as.s->h);
    else if (v->t == V_LIST) sc_mark_obj(&v->as.l->h);
}

static void sc_sweep(sc_heap *h) {
    sc_obj **p = &h->objects;
    while (*p) {
        sc_obj *o = *p;
        if (!o->marked) {
            *p = o->next;
            h->bytes -= o->kind == OBJ_STR ? sizeof(sc_str) + ((sc_str *)o)->len : sizeof(sc_list);
            sc_obj_free(o);
        } else {
            o->marked = 0;
            p = &o->next;
        }
    }
    h->since_gc = 0;
}

static void sc_heap_free(sc_heap *h) {
    while (h->objects) {
        sc_obj *o = h->objects;
        h->objects = o->next;
        sc_obj_free(o);
    }
    h->bytes = h->since_gc = 0;
}

/* ======================================================================== */
/* 2. lexer                                                                 */
/* ======================================================================== */

typedef enum {
    T_EOF = 0, T_NEWLINE, T_NUM, T_STR, T_NAME,
    T_VAR, T_FUNC, T_IF, T_ELIF, T_ELSE, T_WHILE, T_FOR, T_IN, T_RETURN, T_BREAK, T_CONTINUE,
    T_AND, T_OR, T_NOT, T_TRUE, T_FALSE, T_NIL,
    T_LPAREN, T_RPAREN, T_LBRACE, T_RBRACE, T_LBRACKET, T_RBRACKET, T_COMMA, T_DOT, T_SEMI,
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PERCENT,
    T_ASSIGN, T_PLUS_EQ, T_MINUS_EQ, T_STAR_EQ, T_SLASH_EQ,
    T_EQ, T_NE, T_LT, T_LE, T_GT, T_GE,
    T_COLON, T_DECLARE, T_ARROW, T_AT,
    T_ERROR
} sc_tok;

typedef struct sc_token { sc_tok t; const char *start; int len; int line; double num; } sc_token;

typedef struct sc_lexer { const char *p; int line; int depth; } sc_lexer;

static const struct { const char *word; sc_tok t; } k_keywords[] = {
    { "var", T_VAR }, { "func", T_FUNC }, { "if", T_IF }, { "elif", T_ELIF }, { "else", T_ELSE },
    { "while", T_WHILE }, { "for", T_FOR }, { "in", T_IN }, { "return", T_RETURN }, { "break", T_BREAK },
    { "continue", T_CONTINUE }, { "and", T_AND }, { "or", T_OR }, { "not", T_NOT }, { "true", T_TRUE },
    { "false", T_FALSE }, { "nil", T_NIL },
};

static sc_token sc_next(sc_lexer *L) {
    sc_token tk;
    const char *p;
    for (;;) {                                    /* spaces, comments, newlines inside brackets */
        while (*L->p == ' ' || *L->p == '\t' || *L->p == '\r' || (L->depth > 0 && *L->p == '\n')) {
            if (*L->p == '\n') L->line++;
            L->p++;
        }
        if (*L->p == '#') { while (*L->p && *L->p != '\n') L->p++; continue; }
        break;
    }
    p = L->p;
    tk.start = p;
    tk.line = L->line;
    tk.len = 1;
    tk.num = 0;
    if (!*p) { tk.t = T_EOF; tk.len = 0; return tk; }
    if (*p == '\n') { L->p++; L->line++; tk.t = T_NEWLINE; return tk; }
    if (isdigit((unsigned char)*p) || (*p == '.' && isdigit((unsigned char)p[1]))) {
        char *end;
        tk.num = strtod(p, &end);
        tk.len = (int)(end - p);
        L->p = end;
        tk.t = T_NUM;
        return tk;                                /* (written with . or e: a float, for the type checker) */
    }
    if (isalpha((unsigned char)*p) || *p == '_') {
        size_t i;
        while (isalnum((unsigned char)*L->p) || *L->p == '_') L->p++;
        tk.len = (int)(L->p - p);
        tk.t = T_NAME;
        for (i = 0; i < BR_ARRAY_COUNT(k_keywords); ++i)
            if ((int)strlen(k_keywords[i].word) == tk.len && strncmp(k_keywords[i].word, p, (size_t)tk.len) == 0) tk.t = k_keywords[i].t;
        return tk;
    }
    if (*p == '"' || *p == '\'') {
        char q = *p++;
        while (*p && *p != q && *p != '\n') { if (*p == '\\' && p[1]) p++; p++; }
        if (*p != q) { tk.t = T_ERROR; tk.start = "unfinished string"; tk.len = 17; L->p = p; return tk; }
        tk.t = T_STR;
        tk.start = L->p + 1;
        tk.len = (int)(p - L->p - 1);
        L->p = p + 1;
        return tk;
    }
    L->p++;
    switch (*p) {
    case '(': L->depth++; tk.t = T_LPAREN; return tk;
    case ')': if (L->depth) L->depth--; tk.t = T_RPAREN; return tk;
    case '[': L->depth++; tk.t = T_LBRACKET; return tk;
    case ']': if (L->depth) L->depth--; tk.t = T_RBRACKET; return tk;
    case '{': tk.t = T_LBRACE; return tk;
    case '}': tk.t = T_RBRACE; return tk;
    case ',': tk.t = T_COMMA; return tk;
    case '.': tk.t = T_DOT; return tk;
    case ';': tk.t = T_SEMI; return tk;
    case '%': tk.t = T_PERCENT; return tk;
    case '@': tk.t = T_AT; return tk;
    case ':':
        if (*L->p == '=') { L->p++; tk.len = 2; tk.t = T_DECLARE; return tk; }   /* x := 3 */
        tk.t = T_COLON;
        return tk;
    default: break;
    }
    tk.len = 2;
    if (*p == '-' && *L->p == '>') { L->p++; tk.t = T_ARROW; return tk; }
    if (*L->p == '=') {
        switch (*p) {
        case '+': L->p++; tk.t = T_PLUS_EQ; return tk;
        case '-': L->p++; tk.t = T_MINUS_EQ; return tk;
        case '*': L->p++; tk.t = T_STAR_EQ; return tk;
        case '/': L->p++; tk.t = T_SLASH_EQ; return tk;
        case '=': L->p++; tk.t = T_EQ; return tk;
        case '!': L->p++; tk.t = T_NE; return tk;
        case '<': L->p++; tk.t = T_LE; return tk;
        case '>': L->p++; tk.t = T_GE; return tk;
        default: break;
        }
    }
    tk.len = 1;
    switch (*p) {
    case '+': tk.t = T_PLUS; return tk;
    case '-': tk.t = T_MINUS; return tk;
    case '*': tk.t = T_STAR; return tk;
    case '/': tk.t = T_SLASH; return tk;
    case '=': tk.t = T_ASSIGN; return tk;
    case '<': tk.t = T_LT; return tk;
    case '>': tk.t = T_GT; return tk;
    default: break;
    }
    tk.t = T_ERROR;
    tk.start = "unexpected character";
    tk.len = 20;
    return tk;
}

/* ======================================================================== */
/* 3. parser                                                                */
/* ======================================================================== */

typedef enum {
    N_NUM, N_STR, N_TRUE, N_FALSE, N_NIL, N_NAME, N_SELF, N_LIST,
    N_NEG, N_NOT, N_BINARY, N_AND, N_OR, N_CALL, N_METHOD, N_MEMBER, N_INDEX,
    N_BLOCK, N_VAR, N_ASSIGN, N_EXPR, N_IF, N_WHILE, N_FOR, N_RETURN, N_BREAK, N_CONTINUE
} sc_kind;

/* what a name refers to, found by the resolver */
enum { REF_UNRESOLVED = 0, REF_LOCAL, REF_GLOBAL, REF_FUNC, REF_BUILTIN };

/* Static types. TY_NONE: not known until it runs (no annotation). */
typedef enum {
    TY_NONE = 0, TY_ANY, TY_NIL, TY_BOOL, TY_INT, TY_NUM, TY_STR, TY_VEC, TY_LIST, TY_ENT, TY_COMP, TY_VOID
} sc_tyk;
typedef struct sc_ty { uint8_t t, elem; } sc_ty;         /* elem: a list's items (TY_NONE: any) */

typedef struct sc_node {
    uint8_t kind, op;              /* op: a T_* operator token; N_VAR: 1 for := ; N_NUM: 1 if written as a float */
    sc_ty ty;                      /* N_VAR / N_FOR: the declared type; N_NAME: the variable's (runtime checks) */
    int line;
    struct sc_node *a, *b, *c;
    struct sc_node **items;
    int count;
    double num;
    const char *name;              /* names and string literals (in the program's arena) */
    sc_str *str;                   /* string literals, pinned */
    int ref, index;                /* resolved name */
} sc_node;

typedef struct sc_func {
    const char *name;
    int params, locals, line;
    sc_node *body;
    sc_ty *param_ty;               /* per parameter (arena) */
    const char **param_names;
    sc_ty ret;                     /* TY_NONE: not declared, TY_VOID: returns nothing */
} sc_func;
typedef struct sc_global { const char *name; sc_node *init; int line; sc_ty ty; bool infer; } sc_global;

typedef struct sc_chunk { struct sc_chunk *next; size_t used, size; } sc_chunk;

enum { CB_START, CB_UPDATE, CB_COLLISION, CB_COLLISION_END, CB_TRIGGER, CB_TRIGGER_EXIT, CB_COUNT };
static const char *const k_callbacks[CB_COUNT] = {
    "start", "update", "on_collision", "on_collision_end", "on_trigger", "on_trigger_exit"
};

typedef struct sc_program {
    char path[BR_PATH_MAX];
    char name[64];                 /* for messages: the file name */
    int64_t modified;
    sc_chunk *arena;
    sc_func *funcs;
    int func_count, func_cap;
    sc_global *globals;
    int global_count, global_cap;
    int callbacks[CB_COUNT];       /* function index, -1 = not defined */
    sc_str **pinned;
    int pinned_count, pinned_cap;
    bool ok;
    bool strict;                   /* @strict, or br_script_set_strict(): every declaration has a type */
    char error[256];
} sc_program;

typedef struct sc_parser {
    sc_lexer lex;
    sc_token cur, prev;
    sc_program *prog;
    jmp_buf fail;
    /* the function being parsed: its locals, innermost scope last */
    const char *locals[256];
    int local_count, max_locals;
    /* the type checker: the function being checked and its slots' types */
    const sc_func *fn;
    sc_ty slot_ty[256];
} sc_parser;

static void *sc_arena_alloc(sc_program *p, size_t size) {
    sc_chunk *c = p->arena;
    size = (size + 15) & ~(size_t)15;
    if (!c || c->used + size > c->size) {
        size_t cap = size > 16384 ? size : 16384;
        sc_chunk *n = (sc_chunk *)br_alloc(sizeof(sc_chunk) + cap);
        if (!n) return NULL;
        n->next = c;
        n->used = 0;
        n->size = cap;
        p->arena = c = n;
    }
    {
        void *mem = (char *)(c + 1) + c->used;
        c->used += size;
        memset(mem, 0, size);
        return mem;
    }
}

static void sc_parse_error(sc_parser *P, int line, const char *fmt, ...) {
    char msg[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    snprintf(P->prog->error, sizeof(P->prog->error), "%.60s:%d: %.180s", P->prog->name, line, msg);
    longjmp(P->fail, 1);
}

static void sc_advance(sc_parser *P) {
    P->prev = P->cur;
    P->cur = sc_next(&P->lex);
    if (P->cur.t == T_ERROR) sc_parse_error(P, P->cur.line, "%.*s", P->cur.len, P->cur.start);
}

static bool sc_accept(sc_parser *P, sc_tok t) { if (P->cur.t != t) return false; sc_advance(P); return true; }

static const char *sc_tok_text(sc_tok t) {
    switch (t) {
    case T_EOF: return "the end of the file";   case T_NEWLINE: return "a new line";
    case T_NAME: return "a name";               case T_LPAREN: return "'('";
    case T_RPAREN: return "')'";                case T_LBRACE: return "'{'";
    case T_RBRACE: return "'}'";                case T_RBRACKET: return "']'";
    case T_ASSIGN: return "'='";                case T_IN: return "'in'";
    default: return "something else";
    }
}

static void sc_expect(sc_parser *P, sc_tok t) {
    if (P->cur.t != t) {
        if (P->cur.t == T_NAME || P->cur.t == T_NUM)
            sc_parse_error(P, P->cur.line, "expected %s, found '%.*s'", sc_tok_text(t), P->cur.len, P->cur.start);
        sc_parse_error(P, P->cur.line, "expected %s, found %s", sc_tok_text(t), sc_tok_text(P->cur.t));
    }
    sc_advance(P);
}

static void sc_skip_newlines(sc_parser *P) { while (P->cur.t == T_NEWLINE || P->cur.t == T_SEMI) sc_advance(P); }

static sc_node *sc_node_new(sc_parser *P, sc_kind kind, int line) {
    sc_node *n = (sc_node *)sc_arena_alloc(P->prog, sizeof(sc_node));
    if (!n) sc_parse_error(P, line, "out of memory");
    n->kind = (uint8_t)kind;
    n->line = line;
    return n;
}

static const char *sc_intern(sc_parser *P, const char *s, int len) {
    char *c = (char *)sc_arena_alloc(P->prog, (size_t)len + 1);
    if (!c) sc_parse_error(P, P->prev.line, "out of memory");
    memcpy(c, s, (size_t)len);
    c[len] = 0;
    return c;
}

/* a growable list of nodes, copied into the arena when done */
typedef struct sc_nodes { sc_node *items[512]; int count; } sc_nodes;
static void sc_nodes_push(sc_parser *P, sc_nodes *l, sc_node *n) {
    if (l->count == 512) sc_parse_error(P, n->line, "too many items in one list or block");
    l->items[l->count++] = n;
}
static void sc_nodes_store(sc_parser *P, sc_node *into, const sc_nodes *l) {
    into->count = l->count;
    if (l->count) {
        into->items = (sc_node **)sc_arena_alloc(P->prog, sizeof(sc_node *) * (size_t)l->count);
        if (!into->items) sc_parse_error(P, into->line, "out of memory");
        memcpy(into->items, l->items, sizeof(sc_node *) * (size_t)l->count);
    }
}

static sc_str *sc_pin_string(sc_parser *P, const char *s, int len) {
    sc_program *prog = P->prog;
    char buf[1024];
    int i, n = 0;
    sc_str *str;
    for (i = 0; i < len && n < (int)sizeof(buf) - 1; ++i) {     /* escapes */
        char ch = s[i];
        if (ch == '\\' && i + 1 < len) {
            ch = s[++i];
            ch = ch == 'n' ? '\n' : ch == 't' ? '\t' : ch;
        }
        buf[n++] = ch;
    }
    if (prog->pinned_count == prog->pinned_cap) {
        int cap = prog->pinned_cap ? prog->pinned_cap * 2 : 32;
        sc_str **g = (sc_str **)br_realloc(prog->pinned, sizeof(sc_str *) * (size_t)cap);
        if (!g) sc_parse_error(P, P->prev.line, "out of memory");
        prog->pinned = g;
        prog->pinned_cap = cap;
    }
    str = sc_str_alloc(NULL, buf, (size_t)n, true);
    if (!str) sc_parse_error(P, P->prev.line, "out of memory");
    prog->pinned[prog->pinned_count++] = str;
    return str;
}

static sc_node *sc_expression(sc_parser *P);
static sc_node *sc_block(sc_parser *P);

/* ---- types --------------------------------------------------------------------- */

static sc_ty sc_ty_make(int t, int elem) { sc_ty ty; ty.t = (uint8_t)t; ty.elem = (uint8_t)elem; return ty; }

static const char *sc_tyk_name(int t) {
    static const char *const names[] = { "untyped", "any", "nil", "bool", "int", "number", "string", "vec2", "list",
                                         "entity", "component", "void" };
    return t >= 0 && t <= TY_VOID ? names[t] : "?";
}

static const char *sc_ty_name(sc_ty ty, char *buf, size_t size) {
    if (ty.t == TY_LIST && ty.elem != TY_NONE) snprintf(buf, size, "list[%s]", sc_tyk_name(ty.elem));
    else snprintf(buf, size, "%s", sc_tyk_name(ty.t));
    return buf;
}

static int sc_type_word(const char *s, int len) {
    static const struct { const char *word; int t; } words[] = {
        { "number", TY_NUM }, { "float", TY_NUM }, { "int", TY_INT }, { "string", TY_STR }, { "bool", TY_BOOL },
        { "vec2", TY_VEC }, { "list", TY_LIST }, { "entity", TY_ENT }, { "any", TY_ANY }, { "void", TY_VOID },
    };
    size_t i;
    for (i = 0; i < BR_ARRAY_COUNT(words); ++i)
        if ((int)strlen(words[i].word) == len && strncmp(words[i].word, s, (size_t)len) == 0) return words[i].t;
    return -1;
}

/* After ':' or '->': a type name, or list[T]. */
static sc_ty sc_parse_type(sc_parser *P, bool allow_void) {
    sc_token t = P->cur;
    int k = TY_NONE;
    sc_ty ty;
    if (t.t != T_NAME || (k = sc_type_word(t.start, t.len)) < 0) {
        if (t.t == T_NAME || t.t == T_NIL)
            sc_parse_error(P, t.line, "unknown type '%.*s' (the types: number, int, string, bool, vec2, list, entity, any)", t.len, t.start);
        sc_parse_error(P, t.line, "expected a type (number, int, string, bool, vec2, list, entity or any)");
    }
    if (k == TY_VOID && !allow_void) sc_parse_error(P, t.line, "void is only for what a function returns (-> void)");
    sc_advance(P);
    ty = sc_ty_make(k, TY_NONE);
    if (k == TY_LIST && P->cur.t == T_LBRACKET) {           /* list[number] */
        sc_token e;
        int ek = TY_NONE;
        sc_advance(P);
        e = P->cur;
        if (e.t != T_NAME || (ek = sc_type_word(e.start, e.len)) < 0 || ek == TY_VOID)
            sc_parse_error(P, e.line, "expected the type of the list's items, like list[number]");
        sc_advance(P);
        if (ek == TY_LIST && P->cur.t == T_LBRACKET) sc_parse_error(P, e.line, "a list's items can be lists, but not typed lists (use list[list])");
        sc_expect(P, T_RBRACKET);
        ty.elem = (uint8_t)(ek == TY_ANY ? TY_NONE : ek);
    }
    return ty;
}

static int sc_find_local(const sc_parser *P, const char *name, int len) {
    int i;
    for (i = P->local_count - 1; i >= 0; --i)
        if ((int)strlen(P->locals[i]) == len && strncmp(P->locals[i], name, (size_t)len) == 0) return i;
    return -1;
}

static int sc_add_local(sc_parser *P, const char *name, int line) {
    if (P->local_count == 256) sc_parse_error(P, line, "too many local variables");
    P->locals[P->local_count] = name;
    if (P->local_count + 1 > P->max_locals) P->max_locals = P->local_count + 1;
    return P->local_count++;
}

static sc_node *sc_call_args(sc_parser *P, sc_node *call) {
    sc_nodes args;
    args.count = 0;
    if (P->cur.t != T_RPAREN) {
        do { sc_nodes_push(P, &args, sc_expression(P)); } while (sc_accept(P, T_COMMA));
    }
    sc_expect(P, T_RPAREN);
    sc_nodes_store(P, call, &args);
    return call;
}

static sc_node *sc_primary(sc_parser *P) {
    sc_token t = P->cur;
    sc_node *n;
    switch (t.t) {
    case T_NUM: {
        int k;
        sc_advance(P);
        n = sc_node_new(P, N_NUM, t.line);
        n->num = t.num;
        for (k = 0; k < t.len; ++k) if (t.start[k] == '.' || t.start[k] == 'e' || t.start[k] == 'E') n->op = 1;
        return n;
    }
    case T_STR: sc_advance(P); n = sc_node_new(P, N_STR, t.line); n->str = sc_pin_string(P, t.start, t.len); return n;
    case T_TRUE: sc_advance(P); return sc_node_new(P, N_TRUE, t.line);
    case T_FALSE: sc_advance(P); return sc_node_new(P, N_FALSE, t.line);
    case T_NIL: sc_advance(P); return sc_node_new(P, N_NIL, t.line);
    case T_LPAREN:
        sc_advance(P);
        n = sc_expression(P);
        sc_expect(P, T_RPAREN);
        return n;
    case T_LBRACKET: {
        sc_nodes items;
        items.count = 0;
        sc_advance(P);
        n = sc_node_new(P, N_LIST, t.line);
        if (P->cur.t != T_RBRACKET) {
            do {
                if (P->cur.t == T_RBRACKET) break;           /* trailing comma */
                sc_nodes_push(P, &items, sc_expression(P));
            } while (sc_accept(P, T_COMMA));
        }
        sc_expect(P, T_RBRACKET);
        sc_nodes_store(P, n, &items);
        return n;
    }
    case T_NAME:
        sc_advance(P);
        if (t.len == 4 && strncmp(t.start, "self", 4) == 0) return sc_node_new(P, N_SELF, t.line);
        if (P->cur.t == T_LPAREN) {
            sc_advance(P);
            n = sc_node_new(P, N_CALL, t.line);
            n->name = sc_intern(P, t.start, t.len);
            return sc_call_args(P, n);
        }
        n = sc_node_new(P, N_NAME, t.line);
        n->name = sc_intern(P, t.start, t.len);
        {
            int slot = sc_find_local(P, t.start, t.len);
            if (slot >= 0) { n->ref = REF_LOCAL; n->index = slot; }
        }
        return n;
    default:
        if (t.t == T_NEWLINE || t.t == T_EOF) sc_parse_error(P, t.line, "the line ends too early: a value is missing");
        sc_parse_error(P, t.line, "expected a value, found '%.*s'", t.len, t.start);
    }
    return NULL;
}

static sc_node *sc_postfix(sc_parser *P) {
    sc_node *n = sc_primary(P);
    for (;;) {
        if (sc_accept(P, T_DOT)) {
            sc_token name = P->cur;
            sc_expect(P, T_NAME);
            if (P->cur.t == T_LPAREN) {                 /* other.hit(3): a function in that entity's script */
                sc_node *m = sc_node_new(P, N_METHOD, name.line);
                sc_advance(P);
                m->a = n;
                m->name = sc_intern(P, name.start, name.len);
                n = sc_call_args(P, m);
            } else {
                sc_node *m = sc_node_new(P, N_MEMBER, name.line);
                m->a = n;
                m->name = sc_intern(P, name.start, name.len);
                n = m;
            }
        } else if (P->cur.t == T_LBRACKET) {
            sc_node *ix = sc_node_new(P, N_INDEX, P->cur.line);
            sc_advance(P);
            ix->a = n;
            ix->b = sc_expression(P);
            sc_expect(P, T_RBRACKET);
            n = ix;
        } else {
            return n;
        }
    }
}

static sc_node *sc_unary(sc_parser *P) {
    int line = P->cur.line;
    if (sc_accept(P, T_MINUS)) { sc_node *n = sc_node_new(P, N_NEG, line); n->a = sc_unary(P); return n; }
    if (sc_accept(P, T_NOT)) { sc_node *n = sc_node_new(P, N_NOT, line); n->a = sc_unary(P); return n; }
    return sc_postfix(P);
}

static sc_node *sc_binary(sc_parser *P, sc_node *a, sc_tok op, sc_node *b, int line) {
    sc_node *n = sc_node_new(P, N_BINARY, line);
    n->op = (uint8_t)op;
    n->a = a;
    n->b = b;
    return n;
}

static sc_node *sc_factor(sc_parser *P) {
    sc_node *n = sc_unary(P);
    while (P->cur.t == T_STAR || P->cur.t == T_SLASH || P->cur.t == T_PERCENT) {
        sc_token op = P->cur;
        sc_advance(P);
        n = sc_binary(P, n, op.t, sc_unary(P), op.line);
    }
    return n;
}

static sc_node *sc_term(sc_parser *P) {
    sc_node *n = sc_factor(P);
    while (P->cur.t == T_PLUS || P->cur.t == T_MINUS) {
        sc_token op = P->cur;
        sc_advance(P);
        n = sc_binary(P, n, op.t, sc_factor(P), op.line);
    }
    return n;
}

static sc_node *sc_comparison(sc_parser *P) {
    sc_node *n = sc_term(P);
    while (P->cur.t >= T_EQ && P->cur.t <= T_GE) {
        sc_token op = P->cur;
        sc_advance(P);
        n = sc_binary(P, n, op.t, sc_term(P), op.line);
    }
    return n;
}

static sc_node *sc_and(sc_parser *P) {
    sc_node *n = sc_comparison(P);
    while (P->cur.t == T_AND) {
        sc_node *a = sc_node_new(P, N_AND, P->cur.line);
        sc_advance(P);
        a->a = n;
        a->b = sc_comparison(P);
        n = a;
    }
    return n;
}

static sc_node *sc_expression(sc_parser *P) {
    sc_node *n = sc_and(P);
    while (P->cur.t == T_OR) {
        sc_node *o = sc_node_new(P, N_OR, P->cur.line);
        sc_advance(P);
        o->a = n;
        o->b = sc_and(P);
        n = o;
    }
    return n;
}

static void sc_end_statement(sc_parser *P) {
    if (P->cur.t == T_RBRACE || P->cur.t == T_EOF) return;
    if (P->cur.t != T_NEWLINE && P->cur.t != T_SEMI)
        sc_parse_error(P, P->cur.line, "expected the end of the line, found '%.*s'", P->cur.len, P->cur.start);
    sc_skip_newlines(P);
}

static sc_node *sc_statement(sc_parser *P) {
    sc_token t = P->cur;
    sc_node *n;
    switch (t.t) {
    case T_VAR: {
        sc_token name;
        sc_advance(P);
        name = P->cur;
        sc_expect(P, T_NAME);
        n = sc_node_new(P, N_VAR, t.line);
        if (sc_accept(P, T_COLON)) n->ty = sc_parse_type(P, false);
        if (n->ty.t == TY_NONE && sc_accept(P, T_DECLARE)) {     /* var x := 3: the value's type */
            n->op = 1;
            n->a = sc_expression(P);
        } else if (sc_accept(P, T_ASSIGN)) {
            n->a = sc_expression(P);                          /* the value is evaluated before the name exists */
        } else if (P->cur.t == T_DECLARE) {
            sc_parse_error(P, P->cur.line, "':=' takes the value's type, so it can't follow a type: write var %.*s: type = value", name.len, name.start);
        }
        if (P->prog->strict && n->ty.t == TY_NONE && !n->op)
            sc_parse_error(P, name.line, "@strict: give '%.*s' a type (var %.*s: number = 0, or var %.*s := 0)",
                           name.len, name.start, name.len, name.start, name.len, name.start);
        n->index = sc_add_local(P, sc_intern(P, name.start, name.len), name.line);
        n->name = P->locals[n->index];
        sc_end_statement(P);
        return n;
    }
    case T_IF: {
        sc_advance(P);
        n = sc_node_new(P, N_IF, t.line);
        n->a = sc_expression(P);
        n->b = sc_block(P);
        {
            sc_node *tail = n;
            for (;;) {
                sc_skip_newlines(P);
                if (P->cur.t == T_ELIF) {
                    sc_node *e = sc_node_new(P, N_IF, P->cur.line);
                    sc_advance(P);
                    e->a = sc_expression(P);
                    e->b = sc_block(P);
                    tail->c = e;
                    tail = e;
                } else if (P->cur.t == T_ELSE) {
                    sc_advance(P);
                    tail->c = sc_block(P);
                    sc_skip_newlines(P);
                    break;
                } else {
                    break;
                }
            }
        }
        return n;
    }
    case T_WHILE:
        sc_advance(P);
        n = sc_node_new(P, N_WHILE, t.line);
        n->a = sc_expression(P);
        n->b = sc_block(P);
        sc_skip_newlines(P);
        return n;
    case T_FOR: {
        sc_token name;
        int saved = P->local_count;
        sc_advance(P);
        name = P->cur;
        sc_expect(P, T_NAME);
        n = sc_node_new(P, N_FOR, t.line);
        if (sc_accept(P, T_COLON)) n->ty = sc_parse_type(P, false);   /* for i: int in 10 */
        sc_expect(P, T_IN);
        n->a = sc_expression(P);
        n->index = sc_add_local(P, sc_intern(P, name.start, name.len), name.line);
        n->c = sc_node_new(P, N_NIL, t.line);            /* a hidden slot for the loop position */
        n->c->index = sc_add_local(P, " for", t.line);
        n->b = sc_block(P);
        P->local_count = saved;
        sc_skip_newlines(P);
        return n;
    }
    case T_RETURN:
        sc_advance(P);
        n = sc_node_new(P, N_RETURN, t.line);
        if (P->cur.t != T_NEWLINE && P->cur.t != T_SEMI && P->cur.t != T_RBRACE && P->cur.t != T_EOF) n->a = sc_expression(P);
        sc_end_statement(P);
        return n;
    case T_BREAK: sc_advance(P); n = sc_node_new(P, N_BREAK, t.line); sc_end_statement(P); return n;
    case T_CONTINUE: sc_advance(P); n = sc_node_new(P, N_CONTINUE, t.line); sc_end_statement(P); return n;
    case T_FUNC: sc_parse_error(P, t.line, "functions can only be defined at the top of a file");
        /* fall through: not reached */
    default: break;
    }
    {
        sc_node *e = sc_expression(P);
        sc_tok op = P->cur.t;
        if (op == T_ASSIGN || op == T_PLUS_EQ || op == T_MINUS_EQ || op == T_STAR_EQ || op == T_SLASH_EQ) {
            if (e->kind != N_NAME && e->kind != N_MEMBER && e->kind != N_INDEX)
                sc_parse_error(P, P->cur.line, "can't assign to this (only to variables, properties and list items)");
            sc_advance(P);
            n = sc_node_new(P, N_ASSIGN, e->line);
            n->op = (uint8_t)op;
            n->a = e;
            n->b = sc_expression(P);
        } else {
            n = sc_node_new(P, N_EXPR, e->line);
            n->a = e;
        }
        sc_end_statement(P);
        return n;
    }
}

static sc_node *sc_block(sc_parser *P) {
    sc_nodes body;
    sc_node *n = sc_node_new(P, N_BLOCK, P->cur.line);
    int saved = P->local_count;
    body.count = 0;
    sc_expect(P, T_LBRACE);
    sc_skip_newlines(P);
    while (P->cur.t != T_RBRACE) {
        if (P->cur.t == T_EOF) sc_parse_error(P, n->line, "this '{' is never closed");
        sc_nodes_push(P, &body, sc_statement(P));
    }
    sc_advance(P);
    P->local_count = saved;                              /* block-scoped names, slots keep growing */
    sc_nodes_store(P, n, &body);
    return n;
}

static int sc_find_func(const sc_program *p, const char *name) {
    int i;
    for (i = 0; i < p->func_count; ++i) if (strcmp(p->funcs[i].name, name) == 0) return i;
    return -1;
}

static int sc_find_global(const sc_program *p, const char *name) {
    int i;
    for (i = 0; i < p->global_count; ++i) if (strcmp(p->globals[i].name, name) == 0) return i;
    return -1;
}

static void sc_function(sc_parser *P) {
    sc_token name;
    sc_func f;
    int line = P->cur.line;
    sc_advance(P);
    name = P->cur;
    sc_expect(P, T_NAME);
    memset(&f, 0, sizeof(f));
    f.name = sc_intern(P, name.start, name.len);
    f.line = line;
    if (sc_find_func(P->prog, f.name) >= 0) sc_parse_error(P, line, "there already is a function '%s'", f.name);
    P->local_count = P->max_locals = 0;
    sc_expect(P, T_LPAREN);
    {
        sc_ty types[64];
        const char *names[64];
        if (P->cur.t != T_RPAREN) {
            do {
                sc_token pn = P->cur;
                sc_expect(P, T_NAME);
                if (f.params == 64) sc_parse_error(P, pn.line, "too many parameters");
                names[f.params] = sc_intern(P, pn.start, pn.len);
                sc_add_local(P, names[f.params], pn.line);
                types[f.params] = sc_ty_make(TY_NONE, TY_NONE);
                if (sc_accept(P, T_COLON)) types[f.params] = sc_parse_type(P, false);
                else if (P->prog->strict)
                    sc_parse_error(P, pn.line, "@strict: give the parameter '%.*s' a type (%.*s: number)", pn.len, pn.start, pn.len, pn.start);
                f.params++;
            } while (sc_accept(P, T_COMMA));
        }
        sc_expect(P, T_RPAREN);
        if (f.params) {
            f.param_ty = (sc_ty *)sc_arena_alloc(P->prog, sizeof(sc_ty) * (size_t)f.params);
            f.param_names = (const char **)sc_arena_alloc(P->prog, sizeof(char *) * (size_t)f.params);
            if (!f.param_ty || !f.param_names) sc_parse_error(P, line, "out of memory");
            memcpy(f.param_ty, types, sizeof(sc_ty) * (size_t)f.params);
            memcpy((void *)f.param_names, names, sizeof(char *) * (size_t)f.params);
        }
    }
    if (sc_accept(P, T_ARROW)) f.ret = sc_parse_type(P, true);
    else if (P->prog->strict)
        sc_parse_error(P, P->cur.line, "@strict: say what %s() returns (-> void if nothing)", f.name);
    f.body = sc_block(P);
    f.locals = P->max_locals;
    if (P->prog->func_count == P->prog->func_cap) {
        int cap = P->prog->func_cap ? P->prog->func_cap * 2 : 16;
        sc_func *g = (sc_func *)br_realloc(P->prog->funcs, sizeof(sc_func) * (size_t)cap);
        if (!g) sc_parse_error(P, line, "out of memory");
        P->prog->funcs = g;
        P->prog->func_cap = cap;
    }
    P->prog->funcs[P->prog->func_count++] = f;
}

static void sc_global_var(sc_parser *P) {
    sc_token name;
    sc_global g;
    int line = P->cur.line;
    sc_advance(P);
    name = P->cur;
    sc_expect(P, T_NAME);
    memset(&g, 0, sizeof(g));
    g.name = sc_intern(P, name.start, name.len);
    g.line = line;
    if (sc_find_global(P->prog, g.name) >= 0) sc_parse_error(P, line, "there already is a variable '%s'", g.name);
    P->local_count = P->max_locals = 0;
    if (sc_accept(P, T_COLON)) g.ty = sc_parse_type(P, false);
    if (g.ty.t == TY_NONE && sc_accept(P, T_DECLARE)) { g.infer = true; g.init = sc_expression(P); }
    else if (sc_accept(P, T_ASSIGN)) g.init = sc_expression(P);
    else if (P->cur.t == T_DECLARE) sc_parse_error(P, P->cur.line, "':=' takes the value's type, so it can't follow a type: write var %s: type = value", g.name);
    if (P->prog->strict && g.ty.t == TY_NONE && !g.infer)
        sc_parse_error(P, line, "@strict: give '%s' a type (var %s: number = 0, or var %s := 0)", g.name, g.name, g.name);
    sc_end_statement(P);
    if (P->prog->global_count == P->prog->global_cap) {
        int cap = P->prog->global_cap ? P->prog->global_cap * 2 : 16;
        sc_global *gg = (sc_global *)br_realloc(P->prog->globals, sizeof(sc_global) * (size_t)cap);
        if (!gg) sc_parse_error(P, line, "out of memory");
        P->prog->globals = gg;
        P->prog->global_cap = cap;
    }
    P->prog->globals[P->prog->global_count++] = g;
}

/* ---- the resolver: what each name refers to ------------------------------------ */

typedef struct sc_vm sc_vm;
typedef sc_value (*sc_native)(sc_vm *vm, sc_value *args, int argc, int line);
/* sig: the parameters' types, '>', the result's (see sc_sig_type), for the type checker */
typedef struct sc_builtin { const char *name; int min_args, max_args; sc_native fn; const char *sig; } sc_builtin;
static int sc_find_builtin(const char *name);              /* the table is in part 5 */
static const sc_builtin *sc_builtin_at(int index);

static void sc_resolve(sc_parser *P, sc_node *n) {
    int i;
    if (!n) return;
    if (n->kind == N_NAME && n->ref == REF_UNRESOLVED) {
        if ((n->index = sc_find_global(P->prog, n->name)) >= 0) n->ref = REF_GLOBAL;
        else if (sc_find_func(P->prog, n->name) >= 0 || sc_find_builtin(n->name) >= 0)
            sc_parse_error(P, n->line, "'%s' is a function: call it with ()", n->name);
        else sc_parse_error(P, n->line, "unknown name '%s' (declare it with var)", n->name);
    } else if (n->kind == N_CALL) {
        if ((n->index = sc_find_func(P->prog, n->name)) >= 0) {
            n->ref = REF_FUNC;
            if (n->count > P->prog->funcs[n->index].params)
                sc_parse_error(P, n->line, "%s() takes %d argument%s, not %d", n->name, P->prog->funcs[n->index].params,
                               P->prog->funcs[n->index].params == 1 ? "" : "s", n->count);
        } else if ((n->index = sc_find_builtin(n->name)) >= 0) {
            const sc_builtin *b = sc_builtin_at(n->index);
            n->ref = REF_BUILTIN;
            if (n->count < b->min_args || n->count > b->max_args) {
                if (b->min_args == b->max_args) sc_parse_error(P, n->line, "%s() takes %d argument%s, not %d", n->name, b->min_args, b->min_args == 1 ? "" : "s", n->count);
                sc_parse_error(P, n->line, "%s() takes %d to %d arguments, not %d", n->name, b->min_args, b->max_args, n->count);
            }
        } else {
            sc_parse_error(P, n->line, "unknown function '%s'", n->name);
        }
    }
    sc_resolve(P, n->a);
    sc_resolve(P, n->b);
    sc_resolve(P, n->c);
    for (i = 0; i < n->count; ++i) sc_resolve(P, n->items[i]);
}


/* ---- the type checker ---------------------------------------------------------------
 * Runs over the resolved program before it can run. Types come from
 * annotations, literals, built-in functions' signatures and the entity
 * properties; anything else is TY_NONE ("not known until it runs"), which
 * fits everywhere: annotated variables, parameters and results are checked
 * again when the script runs. */

static bool sc_numeric(int t) { return t == TY_INT || t == TY_NUM; }
static bool sc_known(sc_ty ty) { return ty.t != TY_NONE && ty.t != TY_ANY; }

/* Can a value of type `from` be stored where `to` is declared? */
static bool sc_fits(sc_ty to, sc_ty from) {
    if (!sc_known(to) || !sc_known(from)) return from.t != TY_VOID;
    if (to.t == from.t) {
        if (to.t != TY_LIST || to.elem == TY_NONE || from.elem == TY_NONE) return true;
        return to.elem == from.elem || (to.elem == TY_NUM && from.elem == TY_INT);
    }
    if (to.t == TY_NUM && from.t == TY_INT) return true;
    if ((to.t == TY_ENT || to.t == TY_COMP) && from.t == TY_NIL) return true;   /* entities may be nil */
    return false;
}

static void sc_need_fit(sc_parser *P, int line, sc_ty to, sc_ty from, const char *what) {
    char a[32], b[32];
    if (sc_fits(to, from)) return;
    if (from.t == TY_VOID) sc_parse_error(P, line, "%s: that call returns nothing (-> void)", what);
    if (to.t == TY_INT && from.t == TY_NUM)
        sc_parse_error(P, line, "%s is an int, and this number may have a fraction (floor() or round() make it whole)", what);
    sc_parse_error(P, line, "%s is %s %s, so it can't hold %s %s", what, to.t == TY_INT || to.t == TY_ENT || to.t == TY_ANY ? "an" : "a",
                   sc_ty_name(to, a, sizeof(a)), from.t == TY_INT || from.t == TY_ENT ? "an" : "a", sc_ty_name(from, b, sizeof(b)));
}

/* The type a built-in signature letter stands for. */
static sc_ty sc_sig_type(char c) {
    switch (c) {
    case 'n': return sc_ty_make(TY_NUM, 0);  case 'i': return sc_ty_make(TY_INT, 0);
    case 's': return sc_ty_make(TY_STR, 0);  case 'b': return sc_ty_make(TY_BOOL, 0);
    case 'v': return sc_ty_make(TY_VEC, 0);  case 'l': return sc_ty_make(TY_LIST, 0);
    case 'e': return sc_ty_make(TY_ENT, 0);  case 'c': return sc_ty_make(TY_COMP, 0);
    case 'L': return sc_ty_make(TY_LIST, TY_ENT);
    case 'R': return sc_ty_make(TY_LIST, TY_INT);
    case 'W': return sc_ty_make(TY_LIST, TY_STR);
    case '-': return sc_ty_make(TY_VOID, 0);
    default:  return sc_ty_make(TY_NONE, 0);   /* a (any), x / t / p / q: checked below */
    }
}

static sc_ty sc_expr_type(sc_parser *P, sc_node *n);

static const char *sc_article(int t) { return t == TY_INT || t == TY_ENT || t == TY_ANY ? "an" : "a"; }

static void sc_check_arg(sc_parser *P, const sc_node *call, int i, char code, sc_ty got) {
    char want[48], tmp[32], have[32];
    sc_ty need;
    if (!sc_known(got) || code == 'a') return;
    switch (code) {
    case 'x': if (sc_numeric(got.t) || got.t == TY_VEC) return; snprintf(want, sizeof(want), "a number or a vec2"); break;
    case 't': if (got.t == TY_LIST || got.t == TY_STR) return; snprintf(want, sizeof(want), "a list or a string"); break;
    case 'p': if (got.t == TY_VEC || got.t == TY_NIL) return; snprintf(want, sizeof(want), "a vec2 (or nil)"); break;
    case 'q': if (got.t == TY_ENT || got.t == TY_NIL) return; snprintf(want, sizeof(want), "an entity (or nil)"); break;
    default:
        need = sc_sig_type(code);
        if (sc_fits(need, got) && !(code == 'e' && got.t == TY_NIL)) return;
        snprintf(want, sizeof(want), "%s %s", sc_article(need.t), sc_ty_name(need, tmp, sizeof(tmp)));
        break;
    }
    sc_parse_error(P, call->items[i]->line, "%s() wants %s for argument %d, not %s %s", call->name, want, i + 1,
                   sc_article(got.t), sc_ty_name(got, have, sizeof(have)));
}

static sc_ty sc_builtin_type(sc_parser *P, sc_node *n) {
    const sc_builtin *b = sc_builtin_at(n->index);
    const char *sig = b->sig ? b->sig : "a*>a", *ret = strchr(sig, '>'), *q;
    sc_ty args[16], r;
    char codes[16], rest = 0;
    int i, nc = 0;
    for (q = sig; ret && q < ret; ++q) {                  /* "esa*>a": e, s, then any number of a */
        if (*q == '*') rest = nc ? codes[nc - 1] : 'a';
        else if (nc < 16) codes[nc++] = *q;
    }
    for (i = 0; i < n->count && i < 16; ++i) {
        args[i] = sc_expr_type(P, n->items[i]);
        sc_check_arg(P, n, i, i < nc ? codes[i] : rest ? rest : 'a', args[i]);
    }
    if (strcmp(n->name, "push") == 0 && n->count == 2 && args[0].t == TY_LIST && args[0].elem != TY_NONE) {
        char a[32], bb[32];
        if (!sc_fits(sc_ty_make(args[0].elem, 0), args[1]))
            sc_parse_error(P, n->line, "push(): the list holds %s, not %s", sc_ty_name(sc_ty_make(args[0].elem, 0), a, sizeof(a)), sc_ty_name(args[1], bb, sizeof(bb)));
    }
    if (!ret) return sc_ty_make(TY_NONE, 0);
    switch (ret[1]) {
    case 'S': {                                  /* like the arguments: abs, min, max, clamp, lerp, push */
        if (n->count == 0) return sc_ty_make(TY_NONE, 0);
        if (strcmp(n->name, "push") == 0) return args[0];
        if (args[0].t == TY_VEC) return args[0];
        for (i = 0, r = sc_ty_make(TY_INT, 0); i < n->count && i < 16; ++i) {
            if (strcmp(n->name, "lerp") == 0 && i == 2) break;
            if (!sc_known(args[i])) return sc_ty_make(TY_NONE, 0);
            if (args[i].t != TY_INT) r.t = TY_NUM;
        }
        if (strcmp(n->name, "lerp") == 0) r.t = TY_NUM;
        return r;
    }
    case 'E':                                    /* an item of the list: pop, remove */
        return n->count && args[0].t == TY_LIST ? sc_ty_make(args[0].elem, 0) : sc_ty_make(TY_NONE, 0);
    default:
        return sc_sig_type(ret[1]);
    }
}

/* Entity properties: their types, and what setting them takes. */
static const struct { const char *name; uint8_t t; bool settable; } k_props[] = {
    { "name", TY_STR, true }, { "id", TY_INT, false }, { "alive", TY_BOOL, false },
    { "position", TY_VEC, true }, { "x", TY_NUM, true }, { "y", TY_NUM, true }, { "rotation", TY_NUM, true },
    { "scale", TY_VEC, true }, { "global_position", TY_VEC, true }, { "global_rotation", TY_NUM, true },
    { "parent", TY_ENT, true }, { "velocity", TY_VEC, true }, { "angular_velocity", TY_NUM, true },
};

static sc_ty sc_member_type(sc_parser *P, sc_node *n, bool setting) {
    sc_ty obj = sc_expr_type(P, n->a);
    char a[32];
    size_t i;
    if (obj.t == TY_VEC) {
        if (strcmp(n->name, "x") == 0 || strcmp(n->name, "y") == 0) return sc_ty_make(TY_NUM, 0);
        if (strcmp(n->name, "length") == 0 && !setting) return sc_ty_make(TY_NUM, 0);
        sc_parse_error(P, n->line, "a vec2 has x, y and length, not '%s'", n->name);
    }
    if (obj.t == TY_ENT) {
        if (isupper((unsigned char)n->name[0])) return sc_ty_make(TY_COMP, 0);
        for (i = 0; i < BR_ARRAY_COUNT(k_props); ++i)
            if (strcmp(k_props[i].name, n->name) == 0) {
                if (setting && !k_props[i].settable) sc_parse_error(P, n->line, "%s can't be changed", n->name);
                return sc_ty_make(k_props[i].t, 0);
            }
        if (n->a->kind == N_SELF) {                    /* self.speed: this script's own variable */
            int g = sc_find_global(P->prog, n->name);
            if (g >= 0) return P->prog->globals[g].ty;
        }
        return sc_ty_make(TY_NONE, 0);
    }
    if (sc_known(obj) && obj.t != TY_COMP)
        sc_parse_error(P, n->line, "%s %s has no property '%s'", sc_article(obj.t), sc_ty_name(obj, a, sizeof(a)), n->name);
    return sc_ty_make(TY_NONE, 0);
}

static sc_ty sc_binary_type(sc_parser *P, int op, sc_ty a, sc_ty b, int line) {
    const char *sym = op == T_PLUS ? "+" : op == T_MINUS ? "-" : op == T_STAR ? "*" : op == T_SLASH ? "/" : op == T_PERCENT ? "%"
                    : op == T_LT ? "<" : op == T_LE ? "<=" : op == T_GT ? ">" : ">=";
    char an[32], bn[32];
    if (op == T_EQ || op == T_NE) return sc_ty_make(TY_BOOL, 0);
    if (op >= T_LT && op <= T_GE) {
        if (!sc_known(a) || !sc_known(b) || (sc_numeric(a.t) && sc_numeric(b.t)) || (a.t == TY_STR && b.t == TY_STR))
            return sc_ty_make(TY_BOOL, 0);
        sc_parse_error(P, line, "can't compare %s %s with %s %s", sc_article(a.t), sc_ty_name(a, an, sizeof(an)), sc_article(b.t), sc_ty_name(b, bn, sizeof(bn)));
    }
    if (op == T_PLUS && (a.t == TY_STR || b.t == TY_STR)) return sc_ty_make(TY_STR, 0);    /* "score: " + 5 */
    if (!sc_known(a) || !sc_known(b)) return sc_ty_make(TY_NONE, 0);
    if (sc_numeric(a.t) && sc_numeric(b.t)) {
        if (op == T_SLASH) return sc_ty_make(TY_NUM, 0);
        return sc_ty_make(a.t == TY_INT && b.t == TY_INT ? TY_INT : TY_NUM, 0);
    }
    if (a.t == TY_VEC && b.t == TY_VEC && (op == T_PLUS || op == T_MINUS || op == T_STAR)) return a;
    if (a.t == TY_VEC && sc_numeric(b.t) && (op == T_STAR || op == T_SLASH)) return a;
    if (sc_numeric(a.t) && b.t == TY_VEC && op == T_STAR) return b;
    if (a.t == TY_LIST && b.t == TY_LIST && op == T_PLUS) return sc_ty_make(TY_LIST, a.elem == b.elem ? a.elem : TY_NONE);
    sc_parse_error(P, line, "can't use %s on %s %s and %s %s", sym, sc_article(a.t), sc_ty_name(a, an, sizeof(an)), sc_article(b.t), sc_ty_name(b, bn, sizeof(bn)));
    return sc_ty_make(TY_NONE, 0);
}

static sc_ty sc_expr_type(sc_parser *P, sc_node *n) {
    sc_ty a, b;
    char an[32];
    int i;
    switch (n->kind) {
    case N_NUM: return sc_ty_make(!n->op && n->num == floor(n->num) && fabs(n->num) < 9e15 ? TY_INT : TY_NUM, 0);
    case N_STR: return sc_ty_make(TY_STR, 0);
    case N_TRUE: case N_FALSE: return sc_ty_make(TY_BOOL, 0);
    case N_NIL: return sc_ty_make(TY_NIL, 0);
    case N_SELF: return sc_ty_make(TY_ENT, 0);
    case N_NAME:
        n->ty = n->ref == REF_LOCAL ? P->slot_ty[n->index] : P->prog->globals[n->index].ty;
        return n->ty;
    case N_LIST: {
        int elem = -1;
        for (i = 0; i < n->count; ++i) {
            sc_ty it = sc_expr_type(P, n->items[i]);
            if (!sc_known(it) || it.t == TY_NIL || it.t == TY_LIST) elem = TY_NONE;
            else if (elem < 0) elem = it.t;
            else if (elem != it.t) elem = sc_numeric(elem) && sc_numeric(it.t) ? TY_NUM : TY_NONE;
        }
        return sc_ty_make(TY_LIST, elem < 0 ? TY_NONE : elem);
    }
    case N_NEG:
        a = sc_expr_type(P, n->a);
        if (!sc_known(a) || sc_numeric(a.t) || a.t == TY_VEC) return a;
        sc_parse_error(P, n->line, "can't negate %s %s", sc_article(a.t), sc_ty_name(a, an, sizeof(an)));
        return a;
    case N_NOT: sc_expr_type(P, n->a); return sc_ty_make(TY_BOOL, 0);
    case N_AND: case N_OR:                       /* one of the two */
        a = sc_expr_type(P, n->a);
        b = sc_expr_type(P, n->b);
        return a.t == b.t && a.elem == b.elem ? a : sc_ty_make(TY_NONE, 0);
    case N_BINARY:
        a = sc_expr_type(P, n->a);
        b = sc_expr_type(P, n->b);
        return sc_binary_type(P, n->op, a, b, n->line);
    case N_CALL:
        if (n->ref == REF_BUILTIN) return sc_builtin_type(P, n);
        {
            const sc_func *f = &P->prog->funcs[n->index];
            for (i = 0; i < n->count; ++i) {
                sc_ty got = sc_expr_type(P, n->items[i]);
                if (i < f->params && !sc_fits(f->param_ty[i], got)) {
                    char what[96];
                    snprintf(what, sizeof(what), "%s()'s parameter '%s'", f->name, f->param_names[i]);
                    sc_need_fit(P, n->items[i]->line, f->param_ty[i], got, what);
                }
            }
            return f->ret;
        }
    case N_METHOD:
        a = sc_expr_type(P, n->a);
        if (sc_known(a) && a.t != TY_ENT)
            sc_parse_error(P, n->line, "%s %s has no function %s()", sc_article(a.t), sc_ty_name(a, an, sizeof(an)), n->name);
        for (i = 0; i < n->count; ++i) sc_expr_type(P, n->items[i]);
        return sc_ty_make(TY_NONE, 0);
    case N_MEMBER: return sc_member_type(P, n, false);
    case N_INDEX:
        a = sc_expr_type(P, n->a);
        b = sc_expr_type(P, n->b);
        if (sc_known(b) && !sc_numeric(b.t)) sc_parse_error(P, n->b->line, "an index is a number, not %s %s", sc_article(b.t), sc_ty_name(b, an, sizeof(an)));
        if (a.t == TY_LIST) return sc_ty_make(a.elem, 0);
        if (a.t == TY_STR) return a;
        if (sc_known(a)) sc_parse_error(P, n->line, "can't index %s %s with []", sc_article(a.t), sc_ty_name(a, an, sizeof(an)));
        return sc_ty_make(TY_NONE, 0);
    default: return sc_ty_make(TY_NONE, 0);
    }
}

/* The declared type of what an assignment stores into. */
static sc_ty sc_target_type(sc_parser *P, sc_node *t) {
    if (t->kind == N_NAME) return sc_expr_type(P, t);
    if (t->kind == N_MEMBER) {
        sc_ty ty = sc_member_type(P, t, true);
        if (ty.t == TY_COMP) sc_parse_error(P, t->line, "set a component's fields, like self.%s.color", t->name);
        return ty;
    }
    if (t->kind == N_INDEX) {
        sc_ty list = sc_expr_type(P, t->a);
        sc_expr_type(P, t->b);
        if (list.t == TY_STR) sc_parse_error(P, t->line, "strings can't be changed (build a new one with +)");
        return list.t == TY_LIST ? sc_ty_make(list.elem, 0) : sc_ty_make(TY_NONE, 0);
    }
    return sc_ty_make(TY_NONE, 0);
}

static bool sc_always_returns(const sc_node *n) {
    int i;
    if (!n) return false;
    switch (n->kind) {
    case N_RETURN: return true;
    case N_BLOCK:
        for (i = 0; i < n->count; ++i) if (sc_always_returns(n->items[i])) return true;
        return false;
    case N_IF: return sc_always_returns(n->b) && sc_always_returns(n->c);
    default: return false;
    }
}

static void sc_check_stmt(sc_parser *P, sc_node *n) {
    sc_ty a, b;
    char what[96], an[32];
    int i;
    if (!n) return;
    switch (n->kind) {
    case N_BLOCK: for (i = 0; i < n->count; ++i) sc_check_stmt(P, n->items[i]); return;
    case N_EXPR: sc_expr_type(P, n->a); return;
    case N_VAR:
        a = n->a ? sc_expr_type(P, n->a) : sc_ty_make(TY_NONE, 0);
        if (n->op) {                                       /* := */
            if (a.t == TY_VOID) sc_parse_error(P, n->line, "that call returns nothing (-> void), so there is no type to take");
            if (!sc_known(a) || a.t == TY_NIL)
                sc_parse_error(P, n->line, "can't tell the type of %s's value before it runs: write var %s: type = ...", n->name, n->name);
            n->ty = a.t == TY_LIST ? sc_ty_make(TY_LIST, TY_NONE) : a;
        } else if (n->ty.t && n->a) {
            sc_need_fit(P, n->line, n->ty, a, n->name);
        }
        P->slot_ty[n->index] = n->ty;
        return;
    case N_ASSIGN:
        a = sc_target_type(P, n->a);
        b = sc_expr_type(P, n->b);
        if (n->op != T_ASSIGN) {
            int op = n->op == T_PLUS_EQ ? T_PLUS : n->op == T_MINUS_EQ ? T_MINUS : n->op == T_STAR_EQ ? T_STAR : T_SLASH;
            b = sc_binary_type(P, op, sc_expr_type(P, n->a), b, n->line);
        }
        if (n->a->kind == N_MEMBER && strcmp(n->a->name, "scale") == 0 && sc_numeric(b.t)) return;   /* scale = 2 */
        snprintf(what, sizeof(what), "%s", n->a->kind == N_NAME || n->a->kind == N_MEMBER ? n->a->name : "this list's item");
        if (a.t == TY_VOID) a.t = TY_NONE;
        sc_need_fit(P, n->line, a, b, what);
        return;
    case N_IF:
        sc_expr_type(P, n->a);
        sc_check_stmt(P, n->b);
        sc_check_stmt(P, n->c);
        return;
    case N_WHILE:
        sc_expr_type(P, n->a);
        sc_check_stmt(P, n->b);
        return;
    case N_FOR: {
        sc_ty item;
        a = sc_expr_type(P, n->a);
        if (sc_numeric(a.t)) item = sc_ty_make(TY_INT, 0);
        else if (a.t == TY_LIST) item = sc_ty_make(a.elem, 0);
        else if (!sc_known(a)) item = sc_ty_make(TY_NONE, 0);
        else { sc_parse_error(P, n->a->line, "for ... in needs a list or a number, not %s %s", sc_article(a.t), sc_ty_name(a, an, sizeof(an))); item = a; }
        if (n->ty.t) sc_need_fit(P, n->line, n->ty, item, "the loop variable");
        P->slot_ty[n->index] = n->ty.t ? n->ty : item;
        P->slot_ty[n->c->index] = sc_ty_make(TY_NONE, 0);
        sc_check_stmt(P, n->b);
        return;
    }
    case N_RETURN: {
        sc_ty ret = P->fn ? P->fn->ret : sc_ty_make(TY_NONE, 0);
        a = n->a ? sc_expr_type(P, n->a) : sc_ty_make(TY_NIL, 0);
        if (ret.t == TY_VOID && n->a) sc_parse_error(P, n->line, "%s() returns nothing (-> void), so return can't have a value", P->fn->name);
        if (sc_known(ret) && ret.t != TY_VOID) {
            if (!n->a && ret.t != TY_ENT) sc_parse_error(P, n->line, "%s() must return %s %s", P->fn->name, sc_article(ret.t), sc_ty_name(ret, an, sizeof(an)));
            snprintf(what, sizeof(what), "what %s() returns", P->fn->name);
            sc_need_fit(P, n->line, ret, a, what);
        }
        return;
    }
    default: return;
    }
}

/* What the engine passes to each callback. */
static const char *const k_callback_sig[CB_COUNT] = { "", "n", "evn", "e", "e", "e" };

#if defined(__GNUC__) || defined(__clang__)
#  define SC_NOINLINE __attribute__((noinline))       /* (kept out of sc_compile's setjmp frame) */
#else
#  define SC_NOINLINE
#endif
static SC_NOINLINE void sc_typecheck(sc_parser *P) {
    sc_program *prog = P->prog;
    int i, k;
    P->fn = NULL;
    for (i = 0; i < prog->global_count; ++i) {               /* in order: a := may use the ones above it */
        sc_global *g = &prog->globals[i];
        sc_ty t;
        memset(P->slot_ty, 0, sizeof(P->slot_ty));
        if (!g->init) continue;
        t = sc_expr_type(P, g->init);
        if (g->infer) {
            if (t.t == TY_VOID) sc_parse_error(P, g->line, "that call returns nothing (-> void), so there is no type to take");
            if (!sc_known(t) || t.t == TY_NIL) sc_parse_error(P, g->line, "can't tell the type of %s's value before it runs: write var %s: type = ...", g->name, g->name);
            g->ty = t.t == TY_LIST ? sc_ty_make(TY_LIST, TY_NONE) : t;
        } else if (g->ty.t) {
            sc_need_fit(P, g->line, g->ty, t, g->name);
        }
    }
    for (i = 0; i < prog->func_count; ++i) {
        sc_func *f = &prog->funcs[i];
        int cb;
        memset(P->slot_ty, 0, sizeof(P->slot_ty));
        for (k = 0; k < f->params; ++k) P->slot_ty[k] = f->param_ty[k];
        for (cb = 0; cb < CB_COUNT; ++cb) {                   /* the engine calls these: their types are fixed */
            if (strcmp(f->name, k_callbacks[cb]) != 0) continue;
            for (k = 0; k < f->params; ++k) {
                char want[32], have[32];
                sc_ty need = k < (int)strlen(k_callback_sig[cb]) ? sc_sig_type(k_callback_sig[cb][k]) : sc_ty_make(TY_NIL, 0);
                if (sc_known(f->param_ty[k]) && !sc_fits(f->param_ty[k], need))
                    sc_parse_error(P, f->line, "the engine passes %s %s as %s()'s parameter '%s', not %s %s", sc_article(need.t),
                                   sc_ty_name(need, want, sizeof(want)), f->name, f->param_names[k], sc_article(f->param_ty[k].t),
                                   sc_ty_name(f->param_ty[k], have, sizeof(have)));
            }
            if (sc_known(f->ret) && f->ret.t != TY_VOID) sc_parse_error(P, f->line, "the engine ignores what %s() returns: use -> void", f->name);
        }
        P->fn = f;
        sc_check_stmt(P, f->body);
        if (sc_known(f->ret) && f->ret.t != TY_VOID && f->ret.t != TY_ENT && !sc_always_returns(f->body))
            sc_parse_error(P, f->line, "not every way through %s() ends with a return (it must return %s %s)", f->name,
                           sc_article(f->ret.t), sc_tyk_name(f->ret.t));
        P->fn = NULL;
    }
}

static void sc_program_free(sc_program *p) {
    int i;
    if (!p) return;
    while (p->arena) { sc_chunk *c = p->arena; p->arena = c->next; br_free(c); }
    for (i = 0; i < p->pinned_count; ++i) br_free(p->pinned[i]);
    br_free(p->pinned);
    br_free(p->funcs);
    br_free(p->globals);
    br_free(p);
}

static bool g_strict_default;                 /* br_script_set_strict */

/* Parses `source`. Always returns a program; ok == false with `error` set if it has a mistake. */
static sc_program *sc_compile(const char *source, const char *path) {
    sc_parser P;
    sc_program *prog = (sc_program *)br_alloc(sizeof(sc_program));
    int i;
    if (!prog) return NULL;
    memset(prog, 0, sizeof(*prog));
    snprintf(prog->path, sizeof(prog->path), "%s", path);
    snprintf(prog->name, sizeof(prog->name), "%s", br_path_name(path));
    for (i = 0; i < CB_COUNT; ++i) prog->callbacks[i] = -1;
    memset(&P, 0, sizeof(P));
    P.prog = prog;
    P.lex.p = source;
    P.lex.line = 1;
    if (setjmp(P.fail)) { prog->ok = false; return prog; }
    sc_advance(&P);
    sc_skip_newlines(&P);
    prog->strict = g_strict_default;
    while (P.cur.t != T_EOF) {
        if (P.cur.t == T_AT) {                                   /* @strict */
            int line = P.cur.line;
            sc_advance(&P);
            if (P.cur.t != T_NAME || P.cur.len != 6 || strncmp(P.cur.start, "strict", 6) != 0)
                sc_parse_error(&P, line, "unknown annotation '@%.*s' (there is @strict)", P.cur.len, P.cur.start);
            if (prog->func_count || prog->global_count) sc_parse_error(&P, line, "@strict goes at the top of the file, before any var or func");
            sc_advance(&P);
            prog->strict = true;
            sc_end_statement(&P);
            continue;
        }
        if (P.cur.t == T_FUNC) sc_function(&P);
        else if (P.cur.t == T_VAR) sc_global_var(&P);
        else sc_parse_error(&P, P.cur.line, "only 'var' and 'func' can be at the top of a script (put code inside start())");
        sc_skip_newlines(&P);
    }
    for (i = 0; i < prog->global_count; ++i) {
        P.local_count = 0;
        sc_resolve(&P, prog->globals[i].init);
    }
    for (i = 0; i < prog->func_count; ++i) sc_resolve(&P, prog->funcs[i].body);
    for (i = 0; i < CB_COUNT; ++i) prog->callbacks[i] = sc_find_func(prog, k_callbacks[i]);
    sc_typecheck(&P);
    prog->ok = true;
    return prog;
}

/* ======================================================================== */
/* 4. interpreter                                                           */
/* ======================================================================== */

typedef struct sc_runtime sc_runtime;

typedef struct sc_instance {
    sc_runtime *rt;
    br_entity e;
    sc_program *prog;               /* NULL for a C behaviour */
    const br_behaviour *beh;
    void *state;                    /* the behaviour's */
    sc_value *globals;
    bool ready, started, failed;    /* ready: its variables are set (others can use it); started: start() ran */
    int slot;                       /* in rt->instances */
} sc_instance;

typedef struct sc_behaviour_entry { char name[96]; const br_behaviour *b; } sc_behaviour_entry;
static sc_behaviour_entry g_behaviours[SC_MAX_BEHAVIOURS];
static int g_behaviour_count;
static void (*g_print)(const char *, void *);
static void *g_print_user;

struct sc_runtime {
    br_world *w;
    sc_heap heap;
    sc_program **programs;
    int program_count, program_cap;
    sc_instance **instances;
    int instance_count, instance_cap;
    br_component script_id;
    br_builtin_ids ids;
    br_component body_id, collider_id;
    char last_error[300];
    uint32_t error_count;
    char scene_request[BR_PATH_MAX];  /* load_scene(), for the game to act on */
    bool quit_request;                /* quit() */
    br_rng rng;
    int busy;                       /* calls in progress: no collecting */
};

struct sc_vm {
    sc_runtime *rt;
    sc_instance *inst;
    sc_value *stack;
    int base, top, cap;
    int depth;
    long budget;
    sc_value ret;
    jmp_buf *fail;
};

enum { FLOW_NORMAL = 0, FLOW_BREAK, FLOW_CONTINUE, FLOW_RETURN };

static void sc_report(sc_runtime *rt, const char *msg) {
    snprintf(rt->last_error, sizeof(rt->last_error), "%s", msg);
    rt->error_count++;
    BR_ERROR("script: %s", msg);
}

static void sc_fail(sc_vm *vm, int line, const char *fmt, ...) {
    char msg[200], full[300];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    snprintf(full, sizeof(full), "%s:%d: %s", vm->inst && vm->inst->prog ? vm->inst->prog->name : "script", line, msg);
    sc_report(vm->rt, full);
    if (vm->inst) vm->inst->failed = true;                     /* that script stops; the others go on */
    longjmp(*vm->fail, 1);
}

static const char *sc_type_name(const sc_value *v) {
    switch (v->t) {
    case V_NIL: return "nil";       case V_BOOL: return "a true/false";
    case V_NUM: return "a number";  case V_STR: return "a string";
    case V_VEC: return "a vec2";    case V_ENT: return "an entity";
    case V_LIST: return "a list";   default: return "a component";
    }
}

static bool sc_truthy(const sc_value *v) {
    return !(v->t == V_NIL || (v->t == V_BOOL && !v->as.b) || (v->t == V_NUM && v->as.n == 0));
}

static sc_value sc_string(sc_vm *vm, const char *s, size_t len) {
    sc_value v = sc_nil();
    v.t = V_STR;
    v.as.s = sc_str_alloc(&vm->rt->heap, s, len, false);
    if (!v.as.s) sc_fail(vm, 0, "out of memory");
    return v;
}

static const char *sc_entity_name(sc_runtime *rt, br_entity e) {
    const br_name *n = br_entity_alive(rt->w, e) ? (const br_name *)br_get(rt->w, e, rt->ids.name) : NULL;
    return n ? n->text : "";
}

/* A value as text, appended to buf. */
static void sc_append(sc_vm *vm, char *buf, size_t size, const sc_value *v, int depth) {
    size_t n = strlen(buf);
    char *p = buf + n;
    size_t left = size - n;
    uint32_t i;
    if (left <= 1) return;
    switch (v->t) {
    case V_NIL: snprintf(p, left, "nil"); break;
    case V_BOOL: snprintf(p, left, "%s", v->as.b ? "true" : "false"); break;
    case V_NUM: snprintf(p, left, "%.10g", v->as.n); break;
    case V_STR: snprintf(p, left, "%s", v->as.s->chars); break;
    case V_VEC: snprintf(p, left, "(%.10g, %.10g)", v->as.v.x, v->as.v.y); break;
    case V_ENT:
        if (!br_entity_alive(vm->rt->w, v->as.e)) snprintf(p, left, "<destroyed entity>");
        else snprintf(p, left, "<%s>", sc_entity_name(vm->rt, v->as.e)[0] ? sc_entity_name(vm->rt, v->as.e) : "entity");
        break;
    case V_COMP: snprintf(p, left, "<%s>", br_component_name(vm->rt->w, v->as.comp.c)); break;
    case V_LIST:
        if (depth > 3) { snprintf(p, left, "[...]"); break; }
        snprintf(p, left, "[");
        for (i = 0; i < v->as.l->count; ++i) {
            if (i) strncat(buf, ", ", size - strlen(buf) - 1);
            sc_append(vm, buf, size, &v->as.l->items[i], depth + 1);
        }
        strncat(buf, "]", size - strlen(buf) - 1);
        break;
    default: break;
    }
}

static sc_value sc_to_string(sc_vm *vm, const sc_value *v) {
    char buf[1024];
    if (v->t == V_STR) return *v;
    buf[0] = 0;
    sc_append(vm, buf, sizeof(buf), v, 0);
    return sc_string(vm, buf, strlen(buf));
}

static bool sc_equal(const sc_value *a, const sc_value *b) {
    if (a->t != b->t) return false;
    switch (a->t) {
    case V_NIL: return true;
    case V_BOOL: return a->as.b == b->as.b;
    case V_NUM: return a->as.n == b->as.n;
    case V_STR: return a->as.s->len == b->as.s->len && memcmp(a->as.s->chars, b->as.s->chars, a->as.s->len) == 0;
    case V_VEC: return a->as.v.x == b->as.v.x && a->as.v.y == b->as.v.y;
    case V_ENT: return a->as.e == b->as.e;
    case V_LIST: return a->as.l == b->as.l;
    case V_COMP: return a->as.comp.e == b->as.comp.e && a->as.comp.c == b->as.comp.c;
    default: return false;
    }
}

/* ---- declared types, when it runs ---- */

static bool sc_value_is(const sc_value *v, sc_ty ty) {
    uint32_t i;
    switch (ty.t) {
    case TY_NONE: case TY_ANY: return true;
    case TY_NIL: return v->t == V_NIL;
    case TY_BOOL: return v->t == V_BOOL;
    case TY_INT: return v->t == V_NUM && v->as.n == floor(v->as.n);
    case TY_NUM: return v->t == V_NUM;
    case TY_STR: return v->t == V_STR;
    case TY_VEC: return v->t == V_VEC;
    case TY_ENT: return v->t == V_ENT || v->t == V_NIL;
    case TY_COMP: return v->t == V_COMP || v->t == V_NIL;
    case TY_LIST:
        if (v->t != V_LIST) return false;
        if (ty.elem == TY_NONE) return true;
        for (i = 0; i < v->as.l->count; ++i) if (!sc_value_is(&v->as.l->items[i], sc_ty_make(ty.elem, 0))) return false;
        return true;
    default: return false;
    }
}

/* Where a typed variable starts without a value: GDScript-like defaults. */
static sc_value sc_default(sc_vm *vm, sc_ty ty, int line);

static void sc_check_value(sc_vm *vm, sc_ty ty, const sc_value *v, int line, const char *what) {
    char tn[32];
    if (sc_value_is(v, ty)) return;
    if (ty.t == TY_INT && v->t == V_NUM) sc_fail(vm, line, "%s is an int, and %g isn't a whole number", what, v->as.n);
    if (ty.t == TY_LIST && v->t == V_LIST) sc_fail(vm, line, "%s is a %s, and this list holds something else", what, sc_ty_name(ty, tn, sizeof(tn)));
    sc_fail(vm, line, "%s is %s %s, so it can't hold %s", what, ty.t == TY_INT || ty.t == TY_ENT || ty.t == TY_ANY ? "an" : "a",
            sc_ty_name(ty, tn, sizeof(tn)), sc_type_name(v));
}

static double sc_need_num(sc_vm *vm, const sc_value *v, int line, const char *what) {
    if (v->t != V_NUM) sc_fail(vm, line, "%s needs a number, not %s", what, sc_type_name(v));
    return v->as.n;
}

static sc_value sc_eval(sc_vm *vm, sc_node *n);
static int sc_exec(sc_vm *vm, sc_node *n);
static sc_value sc_get_member(sc_vm *vm, const sc_value *obj, const char *name, int line);
static void sc_set_member(sc_vm *vm, const sc_value *obj, const char *name, const sc_value *val, int line);
static sc_value sc_call_func(sc_vm *vm, sc_instance *inst, int fn, const sc_value *args, int argc, int line);

static sc_value sc_arith(sc_vm *vm, int op, sc_value a, sc_value b, int line) {
    if (op == T_PLUS && (a.t == V_STR || b.t == V_STR)) {            /* "score: " + 5 */
        sc_value sa = sc_to_string(vm, &a), sb = sc_to_string(vm, &b);
        size_t la = sa.as.s->len, lb = sb.as.s->len;
        char small[512], *buf = la + lb < sizeof(small) ? small : (char *)br_alloc(la + lb + 1);
        sc_value r;
        if (!buf) sc_fail(vm, line, "out of memory");
        memcpy(buf, sa.as.s->chars, la);
        memcpy(buf + la, sb.as.s->chars, lb);
        r = sc_string(vm, buf, la + lb);
        if (buf != small) br_free(buf);
        return r;
    }
    if (a.t == V_NUM && b.t == V_NUM) {
        double x = a.as.n, y = b.as.n;
        switch (op) {
        case T_PLUS: return sc_num(x + y);
        case T_MINUS: return sc_num(x - y);
        case T_STAR: return sc_num(x * y);
        case T_SLASH: if (y == 0) sc_fail(vm, line, "division by zero"); return sc_num(x / y);
        case T_PERCENT: if (y == 0) sc_fail(vm, line, "division by zero"); return sc_num(x - y * floor(x / y));
        default: break;
        }
    }
    if (a.t == V_VEC && b.t == V_VEC && (op == T_PLUS || op == T_MINUS || op == T_STAR)) {
        if (op == T_PLUS) return sc_vec(a.as.v.x + b.as.v.x, a.as.v.y + b.as.v.y);
        if (op == T_MINUS) return sc_vec(a.as.v.x - b.as.v.x, a.as.v.y - b.as.v.y);
        return sc_vec(a.as.v.x * b.as.v.x, a.as.v.y * b.as.v.y);
    }
    if (a.t == V_VEC && b.t == V_NUM && (op == T_STAR || op == T_SLASH)) {
        if (op == T_SLASH && b.as.n == 0) sc_fail(vm, line, "division by zero");
        return op == T_STAR ? sc_vec(a.as.v.x * b.as.n, a.as.v.y * b.as.n) : sc_vec(a.as.v.x / b.as.n, a.as.v.y / b.as.n);
    }
    if (a.t == V_NUM && b.t == V_VEC && op == T_STAR) return sc_vec(a.as.n * b.as.v.x, a.as.n * b.as.v.y);
    if (a.t == V_LIST && b.t == V_LIST && op == T_PLUS) {
        sc_list *l = sc_list_alloc(&vm->rt->heap);
        sc_value r = sc_nil();
        uint32_t n = a.as.l->count + b.as.l->count;
        if (!l || (n && !(l->items = (sc_value *)br_alloc(sizeof(sc_value) * n)))) sc_fail(vm, line, "out of memory");
        if (a.as.l->count) memcpy(l->items, a.as.l->items, sizeof(sc_value) * a.as.l->count);
        if (b.as.l->count) memcpy(l->items + a.as.l->count, b.as.l->items, sizeof(sc_value) * b.as.l->count);
        l->count = l->cap = n;
        r.t = V_LIST;
        r.as.l = l;
        return r;
    }
    {
        const char *sym = op == T_PLUS ? "+" : op == T_MINUS ? "-" : op == T_STAR ? "*" : op == T_SLASH ? "/" : "%";
        sc_fail(vm, line, "can't use %s on %s and %s", sym, sc_type_name(&a), sc_type_name(&b));
    }
    return sc_nil();
}

static sc_value sc_compare(sc_vm *vm, int op, sc_value a, sc_value b, int line) {
    double x, y;
    if (op == T_EQ) return sc_bool(sc_equal(&a, &b));
    if (op == T_NE) return sc_bool(!sc_equal(&a, &b));
    if (a.t == V_STR && b.t == V_STR) { x = strcmp(a.as.s->chars, b.as.s->chars); y = 0; }
    else if (a.t == V_NUM && b.t == V_NUM) { x = a.as.n; y = b.as.n; }
    else { sc_fail(vm, line, "can't compare %s with %s", sc_type_name(&a), sc_type_name(&b)); return sc_nil(); }
    switch (op) {
    case T_LT: return sc_bool(x < y);
    case T_LE: return sc_bool(x <= y);
    case T_GT: return sc_bool(x > y);
    default: return sc_bool(x >= y);
    }
}

static sc_value *sc_local(sc_vm *vm, int slot) { return &vm->stack[vm->base + slot]; }

static sc_value sc_new_list(sc_vm *vm, int line);

static sc_value sc_default(sc_vm *vm, sc_ty ty, int line) {
    switch (ty.t) {
    case TY_INT: case TY_NUM: return sc_num(0);
    case TY_BOOL: return sc_bool(false);
    case TY_STR: return sc_string(vm, "", 0);
    case TY_VEC: return sc_vec(0, 0);
    case TY_LIST: return sc_new_list(vm, line);
    default: return sc_nil();
    }
}

static sc_value sc_index_get(sc_vm *vm, sc_value obj, sc_value ix, int line) {
    double i;
    if (obj.t == V_LIST) {
        i = sc_need_num(vm, &ix, line, "a list index");
        if (i < 0) i += obj.as.l->count;                         /* list[-1]: the last item */
        if (i < 0 || i >= obj.as.l->count || i != floor(i)) sc_fail(vm, line, "list index %g is out of range (the list has %u items)", ix.as.n, obj.as.l->count);
        return obj.as.l->items[(uint32_t)i];
    }
    if (obj.t == V_STR) {
        i = sc_need_num(vm, &ix, line, "a string index");
        if (i < 0) i += obj.as.s->len;
        if (i < 0 || i >= obj.as.s->len) sc_fail(vm, line, "string index %g is out of range", ix.as.n);
        return sc_string(vm, obj.as.s->chars + (uint32_t)i, 1);
    }
    sc_fail(vm, line, "can't index %s with []", sc_type_name(&obj));
    return sc_nil();
}

/* Stores `val` into the place `target` describes (a variable, a property, a list item). */
static void sc_assign(sc_vm *vm, sc_node *target, sc_value val) {
    switch (target->kind) {
    case N_NAME:
        if (target->ty.t) sc_check_value(vm, target->ty, &val, target->line, target->name);
        if (target->ref == REF_LOCAL) *sc_local(vm, target->index) = val;
        else vm->inst->globals[target->index] = val;
        return;
    case N_MEMBER: {
        sc_value obj = sc_eval(vm, target->a);
        if (obj.t == V_VEC) {                                    /* a copy: change it, store it back */
            if (strcmp(target->name, "x") == 0) obj.as.v.x = sc_need_num(vm, &val, target->line, "x");
            else if (strcmp(target->name, "y") == 0) obj.as.v.y = sc_need_num(vm, &val, target->line, "y");
            else sc_fail(vm, target->line, "a vec2 has only x and y");
            sc_assign(vm, target->a, obj);
            return;
        }
        sc_set_member(vm, &obj, target->name, &val, target->line);
        return;
    }
    case N_INDEX: {
        sc_value obj = sc_eval(vm, target->a), ix = sc_eval(vm, target->b);
        double i;
        if (obj.t != V_LIST) sc_fail(vm, target->line, "can only assign items of a list");
        i = sc_need_num(vm, &ix, target->line, "a list index");
        if (i < 0) i += obj.as.l->count;
        if (i < 0 || i >= obj.as.l->count || i != floor(i)) sc_fail(vm, target->line, "list index %g is out of range (the list has %u items)", ix.as.n, obj.as.l->count);
        obj.as.l->items[(uint32_t)i] = val;
        return;
    }
    case N_SELF: sc_fail(vm, target->line, "can't assign to self");
        break;
    default: sc_fail(vm, target->line, "can't assign to this");
    }
}

static sc_value sc_eval_call(sc_vm *vm, sc_node *n) {
    sc_value args[16];
    int i;
    if (n->count > 16) sc_fail(vm, n->line, "too many arguments");
    for (i = 0; i < n->count; ++i) args[i] = sc_eval(vm, n->items[i]);
    if (n->ref == REF_FUNC) return sc_call_func(vm, vm->inst, n->index, args, n->count, n->line);
    return sc_builtin_at(n->index)->fn(vm, args, n->count, n->line);
}

static sc_instance *sc_instance_of(sc_runtime *rt, br_entity e) {
    const br_script *s = br_entity_alive(rt->w, e) ? (const br_script *)br_get(rt->w, e, rt->script_id) : NULL;
    return s ? (sc_instance *)s->instance : NULL;
}

static sc_value sc_eval_method(sc_vm *vm, sc_node *n) {
    sc_value obj = sc_eval(vm, n->a), args[16];
    sc_instance *target;
    int i, fn;
    if (n->count > 16) sc_fail(vm, n->line, "too many arguments");
    if (obj.t != V_ENT) sc_fail(vm, n->line, "%s has no function %s()", sc_type_name(&obj), n->name);
    target = sc_instance_of(vm->rt, obj.as.e);
    if (!target || !target->prog || target->failed || !target->ready)
        sc_fail(vm, n->line, "%s has no running script to call %s() on", sc_entity_name(vm->rt, obj.as.e)[0] ? sc_entity_name(vm->rt, obj.as.e) : "that entity", n->name);
    if ((fn = sc_find_func(target->prog, n->name)) < 0) sc_fail(vm, n->line, "%s has no function %s()", target->prog->name, n->name);
    for (i = 0; i < n->count; ++i) args[i] = sc_eval(vm, n->items[i]);
    return sc_call_func(vm, target, fn, args, n->count, n->line);
}

static sc_value sc_eval(sc_vm *vm, sc_node *n) {
    if (--vm->budget < 0) sc_fail(vm, n->line, "this took too long (an endless loop?)");
    switch (n->kind) {
    case N_NUM: return sc_num(n->num);
    case N_STR: { sc_value v = sc_nil(); v.t = V_STR; v.as.s = n->str; return v; }
    case N_TRUE: return sc_bool(true);
    case N_FALSE: return sc_bool(false);
    case N_NIL: return sc_nil();
    case N_SELF: return sc_ent(vm->inst->e);
    case N_NAME: return n->ref == REF_LOCAL ? *sc_local(vm, n->index) : vm->inst->globals[n->index];
    case N_LIST: {
        sc_list *l = sc_list_alloc(&vm->rt->heap);
        sc_value v = sc_nil();
        int i;
        if (!l) sc_fail(vm, n->line, "out of memory");
        v.t = V_LIST;
        v.as.l = l;
        if (n->count) {
            if (!(l->items = (sc_value *)br_alloc(sizeof(sc_value) * (size_t)n->count))) sc_fail(vm, n->line, "out of memory");
            l->cap = (uint32_t)n->count;
            for (i = 0; i < n->count; ++i) { l->items[i] = sc_eval(vm, n->items[i]); l->count++; }
        }
        return v;
    }
    case N_NEG: {
        sc_value a = sc_eval(vm, n->a);
        if (a.t == V_NUM) return sc_num(-a.as.n);
        if (a.t == V_VEC) return sc_vec(-a.as.v.x, -a.as.v.y);
        sc_fail(vm, n->line, "can't negate %s", sc_type_name(&a));
        return sc_nil();
    }
    case N_NOT: { sc_value a = sc_eval(vm, n->a); return sc_bool(!sc_truthy(&a)); }
    case N_AND: { sc_value a = sc_eval(vm, n->a); return sc_truthy(&a) ? sc_eval(vm, n->b) : a; }
    case N_OR: { sc_value a = sc_eval(vm, n->a); return sc_truthy(&a) ? a : sc_eval(vm, n->b); }
    case N_BINARY: {
        sc_value a = sc_eval(vm, n->a), b = sc_eval(vm, n->b);
        if (n->op >= T_EQ && n->op <= T_GE) return sc_compare(vm, n->op, a, b, n->line);
        return sc_arith(vm, n->op, a, b, n->line);
    }
    case N_CALL: return sc_eval_call(vm, n);
    case N_METHOD: return sc_eval_method(vm, n);
    case N_MEMBER: { sc_value o = sc_eval(vm, n->a); return sc_get_member(vm, &o, n->name, n->line); }
    case N_INDEX: { sc_value o = sc_eval(vm, n->a), ix = sc_eval(vm, n->b); return sc_index_get(vm, o, ix, n->line); }
    default: sc_fail(vm, n->line, "internal error: not an expression");
    }
    return sc_nil();
}

static int sc_exec_block(sc_vm *vm, sc_node *n) {
    int i, flow;
    for (i = 0; i < n->count; ++i)
        if ((flow = sc_exec(vm, n->items[i])) != FLOW_NORMAL) return flow;
    return FLOW_NORMAL;
}

static int sc_exec(sc_vm *vm, sc_node *n) {
    if (--vm->budget < 0) sc_fail(vm, n->line, "this took too long (an endless loop?)");
    switch (n->kind) {
    case N_BLOCK: return sc_exec_block(vm, n);
    case N_EXPR: sc_eval(vm, n->a); return FLOW_NORMAL;
    case N_VAR: {
        sc_value v = n->a ? sc_eval(vm, n->a) : sc_default(vm, n->ty, n->line);   /* first: a call may move the stack */
        if (n->ty.t && n->a) sc_check_value(vm, n->ty, &v, n->line, n->name);
        *sc_local(vm, n->index) = v;
        return FLOW_NORMAL;
    }
    case N_ASSIGN: {
        sc_value v = sc_eval(vm, n->b);
        if (n->op != T_ASSIGN) {
            int op = n->op == T_PLUS_EQ ? T_PLUS : n->op == T_MINUS_EQ ? T_MINUS : n->op == T_STAR_EQ ? T_STAR : T_SLASH;
            v = sc_arith(vm, op, sc_eval(vm, n->a), v, n->line);
        }
        sc_assign(vm, n->a, v);
        return FLOW_NORMAL;
    }
    case N_IF: {
        sc_value c = sc_eval(vm, n->a);
        if (sc_truthy(&c)) return sc_exec(vm, n->b);
        return n->c ? sc_exec(vm, n->c) : FLOW_NORMAL;
    }
    case N_WHILE:
        for (;;) {
            sc_value c = sc_eval(vm, n->a);
            int flow;
            if (!sc_truthy(&c)) return FLOW_NORMAL;
            flow = sc_exec(vm, n->b);
            if (flow == FLOW_BREAK) return FLOW_NORMAL;
            if (flow == FLOW_RETURN) return flow;
        }
    case N_FOR: {
        sc_value seq = sc_eval(vm, n->a);
        uint32_t i;
        if (seq.t == V_NUM) {                                     /* for i in 5: 0, 1, 2, 3, 4 */
            double count = seq.as.n, k;
            for (k = 0; k < count; k += 1) {
                int flow;
                *sc_local(vm, n->index) = sc_num(k);
                flow = sc_exec(vm, n->b);
                if (flow == FLOW_BREAK) break;
                if (flow == FLOW_RETURN) return flow;
            }
            return FLOW_NORMAL;
        }
        if (seq.t != V_LIST) sc_fail(vm, n->line, "for ... in needs a list or a number, not %s", sc_type_name(&seq));
        *sc_local(vm, n->c->index) = seq;                         /* keeps the list alive */
        for (i = 0; i < seq.as.l->count; ++i) {
            int flow;
            if (n->ty.t) sc_check_value(vm, n->ty, &seq.as.l->items[i], n->line, "the loop variable");
            *sc_local(vm, n->index) = seq.as.l->items[i];
            flow = sc_exec(vm, n->b);
            if (flow == FLOW_BREAK) break;
            if (flow == FLOW_RETURN) return flow;
        }
        return FLOW_NORMAL;
    }
    case N_RETURN: vm->ret = n->a ? sc_eval(vm, n->a) : sc_nil(); return FLOW_RETURN;
    case N_BREAK: return FLOW_BREAK;
    case N_CONTINUE: return FLOW_CONTINUE;
    default: sc_fail(vm, n->line, "internal error: not a statement");
    }
    return FLOW_NORMAL;
}

static sc_value sc_call_func(sc_vm *vm, sc_instance *inst, int fn, const sc_value *args, int argc, int line) {
    const sc_func *f = &inst->prog->funcs[fn];
    sc_instance *saved_inst = vm->inst;
    int saved_base = vm->base, saved_top = vm->top, i;
    sc_value result;
    if (vm->depth >= SC_MAX_DEPTH) sc_fail(vm, line, "too many nested calls (endless recursion?)");
    if (vm->top + f->locals > vm->cap) {
        int cap = vm->cap ? vm->cap * 2 : 256;
        sc_value *g;
        while (cap < vm->top + f->locals) cap *= 2;
        if (!(g = (sc_value *)br_realloc(vm->stack, sizeof(sc_value) * (size_t)cap))) sc_fail(vm, line, "out of memory");
        vm->stack = g;
        vm->cap = cap;
    }
    vm->base = vm->top;
    vm->top += f->locals;
    for (i = 0; i < f->locals; ++i) vm->stack[vm->base + i] = i < argc && i < f->params ? args[i] : sc_nil();
    vm->inst = inst;
    for (i = 0; i < f->params; ++i) {                          /* typed parameters */
        sc_ty ty = f->param_ty[i];
        if (!ty.t) continue;
        if (i >= argc) { sc_value d = sc_default(vm, ty, line); vm->stack[vm->base + i] = d; continue; }
        {
            char what[96];
            snprintf(what, sizeof(what), "%s()'s parameter '%s'", f->name, f->param_names[i]);
            sc_check_value(vm, ty, &args[i], line, what);
        }
    }
    vm->depth++;
    vm->ret = sc_nil();
    result = sc_exec(vm, f->body) == FLOW_RETURN ? vm->ret : sc_nil();
    if (f->ret.t && f->ret.t != TY_VOID) {
        char what[96];
        snprintf(what, sizeof(what), "what %s() returns", f->name);
        sc_check_value(vm, f->ret, &result, f->line, what);
    }
    vm->depth--;
    vm->inst = saved_inst;
    vm->base = saved_base;
    vm->top = saved_top;
    return result;
}

/* ======================================================================== */
/* 5. the engine API                                                        */
/* ======================================================================== */

static br_engine *sc_engine(sc_vm *vm) { return (br_engine *)vm->rt->w->context; }

static int clamp_byte(double v) { return v < 0 ? 0 : v > 255 ? 255 : (int)(v + 0.5); }

static bool sc_strieq(const char *a, const char *b) {
    for (; *a && *b; ++a, ++b) if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    return *a == *b;
}

static br_entity sc_need_entity(sc_vm *vm, const sc_value *v, int line, const char *what) {
    if (v->t != V_ENT) sc_fail(vm, line, "%s needs an entity, not %s", what, sc_type_name(v));
    if (!br_entity_alive(vm->rt->w, v->as.e)) sc_fail(vm, line, "%s: that entity was destroyed", what);
    return v->as.e;
}

static const char *sc_need_str(sc_vm *vm, const sc_value *v, int line, const char *what) {
    if (v->t != V_STR) sc_fail(vm, line, "%s needs a string, not %s", what, sc_type_name(v));
    return v->as.s->chars;
}

static br_vec2 sc_need_vec(sc_vm *vm, const sc_value *v, int line, const char *what) {
    if (v->t != V_VEC) sc_fail(vm, line, "%s needs a vec2, not %s", what, sc_type_name(v));
    return br_v2((float)v->as.v.x, (float)v->as.v.y);
}

static br_transform *sc_transform(sc_vm *vm, br_entity e, int line) {
    br_transform *t = (br_transform *)br_get(vm->rt->w, e, vm->rt->ids.transform);
    if (!t) sc_fail(vm, line, "%s has no Transform", sc_entity_name(vm->rt, e)[0] ? sc_entity_name(vm->rt, e) : "this entity");
    return t;
}

/* RigidBody.velocity if it has one, else Motion.velocity. */
static br_vec2 *sc_velocity(sc_vm *vm, br_entity e, int line) {
    br_rigidbody *rb = (br_rigidbody *)br_get(vm->rt->w, e, vm->rt->body_id);
    br_motion *m;
    if (rb) return &rb->velocity;
    if ((m = (br_motion *)br_get(vm->rt->w, e, vm->rt->ids.motion)) != NULL) return &m->velocity;
    sc_fail(vm, line, "velocity needs a RigidBody or a Motion component");
    return NULL;
}

static sc_value sc_field_get(sc_vm *vm, const br_field *f, const void *data) {
    const unsigned char *p = (const unsigned char *)data + f->offset;
    switch (f->type) {
    case BR_FIELD_INT: return sc_num(*(const int *)p);
    case BR_FIELD_FLOAT: return sc_num(*(const float *)p);
    case BR_FIELD_BOOL: return sc_bool(*(const bool *)p);
    case BR_FIELD_VEC2: return sc_vec(((const br_vec2 *)p)->x, ((const br_vec2 *)p)->y);
    case BR_FIELD_STRING: return sc_string(vm, (const char *)p, strlen((const char *)p));
    case BR_FIELD_ENTITY: {
        br_entity e;
        memcpy(&e, p, sizeof(e));
        return br_entity_alive(vm->rt->w, e) ? sc_ent(e) : sc_nil();
    }
    default: {                                             /* colours ("#rrggbbaa") and enum names */
        char text[64];
        br_field_format(f, data, text, sizeof(text));
        if (f->type == BR_FIELD_ENUM && text[0] == '"') { size_t n = strlen(text); memmove(text, text + 1, n); if (n > 1) text[n - 2] = 0; }
        return sc_string(vm, text, strlen(text));
    }
    }
}

static void sc_field_set(sc_vm *vm, const br_field *f, void *data, const sc_value *v, int line) {
    unsigned char *p = (unsigned char *)data + f->offset;
    switch (f->type) {
    case BR_FIELD_INT: *(int *)p = (int)sc_need_num(vm, v, line, f->name); return;
    case BR_FIELD_FLOAT: *(float *)p = (float)sc_need_num(vm, v, line, f->name); return;
    case BR_FIELD_BOOL: *(bool *)p = sc_truthy(v); return;
    case BR_FIELD_VEC2: *(br_vec2 *)p = sc_need_vec(vm, v, line, f->name); return;
    case BR_FIELD_STRING: snprintf((char *)p, f->size, "%s", sc_need_str(vm, v, line, f->name)); return;
    case BR_FIELD_ENTITY: {
        br_entity e = v->t == V_NIL ? BR_ENTITY_NULL : sc_need_entity(vm, v, line, f->name);
        memcpy(p, &e, sizeof(e));
        return;
    }
    case BR_FIELD_ENUM:
        if (v->t == V_NUM) {
            int i = (int)v->as.n;
            if (i < 0 || i >= f->enum_count) sc_fail(vm, line, "%s has no choice %d", f->name, i);
            *(int *)p = i;
            return;
        }
        if (!br_field_parse(f, data, sc_need_str(vm, v, line, f->name))) sc_fail(vm, line, "%s has no choice \"%s\"", f->name, v->as.s->chars);
        return;
    case BR_FIELD_COLOR:
        if (v->t == V_LIST && (v->as.l->count == 3 || v->as.l->count == 4)) {
            br_color *c = (br_color *)p;
            uint8_t ch[4];
            uint32_t i;
            ch[3] = 255;
            for (i = 0; i < v->as.l->count; ++i) ch[i] = (uint8_t)clamp_byte(sc_need_num(vm, &v->as.l->items[i], line, "a colour channel"));
            c->r = ch[0]; c->g = ch[1]; c->b = ch[2]; c->a = ch[3];
            return;
        }
        if (!br_field_parse(f, data, sc_need_str(vm, v, line, f->name))) sc_fail(vm, line, "\"%s\" isn't a colour (use \"#rrggbb\" or rgb())", v->as.s->chars);
        return;
    default: return;
    }
}

static sc_value sc_get_member(sc_vm *vm, const sc_value *obj, const char *name, int line) {
    sc_runtime *rt = vm->rt;
    if (obj->t == V_VEC) {
        if (strcmp(name, "x") == 0) return sc_num(obj->as.v.x);
        if (strcmp(name, "y") == 0) return sc_num(obj->as.v.y);
        if (strcmp(name, "length") == 0) return sc_num(sqrt(obj->as.v.x * obj->as.v.x + obj->as.v.y * obj->as.v.y));
        sc_fail(vm, line, "a vec2 has x, y and length, not '%s'", name);
    }
    if (obj->t == V_COMP) {
        const br_component_desc *d = br_component_get_desc(rt->w, obj->as.comp.c);
        const void *data = br_get(rt->w, obj->as.comp.e, obj->as.comp.c);
        int i;
        if (!data) sc_fail(vm, line, "that %s was removed", br_component_name(rt->w, obj->as.comp.c));
        for (i = 0; d && i < d->field_count; ++i)
            if (strcmp(d->fields[i].name, name) == 0) return sc_field_get(vm, &d->fields[i], data);
        sc_fail(vm, line, "%s has no field '%s'", br_component_name(rt->w, obj->as.comp.c), name);
    }
    if (obj->t == V_ENT) {
        br_entity e = obj->as.e;
        sc_instance *other;
        if (strcmp(name, "alive") == 0) return sc_bool(br_entity_alive(rt->w, e));
        if (!br_entity_alive(rt->w, e)) sc_fail(vm, line, "can't read .%s: that entity was destroyed", name);
        if (strcmp(name, "name") == 0) { const char *s = sc_entity_name(rt, e); return sc_string(vm, s, strlen(s)); }
        if (strcmp(name, "id") == 0) return sc_num((double)e);
        if (strcmp(name, "position") == 0) { br_transform *t = sc_transform(vm, e, line); return sc_vec(t->position.x, t->position.y); }
        if (strcmp(name, "x") == 0) return sc_num(sc_transform(vm, e, line)->position.x);
        if (strcmp(name, "y") == 0) return sc_num(sc_transform(vm, e, line)->position.y);
        if (strcmp(name, "rotation") == 0) return sc_num(sc_transform(vm, e, line)->rotation);
        if (strcmp(name, "scale") == 0) { br_transform *t = sc_transform(vm, e, line); return sc_vec(t->scale.x, t->scale.y); }
        if (strcmp(name, "global_position") == 0) { br_transform wt = br_world_transform(rt->w, e); return sc_vec(wt.position.x, wt.position.y); }
        if (strcmp(name, "global_rotation") == 0) return sc_num(br_world_transform(rt->w, e).rotation);
        if (strcmp(name, "parent") == 0) { br_entity p = br_parent_of(rt->w, e); return p == BR_ENTITY_NULL ? sc_nil() : sc_ent(p); }
        if (strcmp(name, "velocity") == 0) { br_vec2 *v = sc_velocity(vm, e, line); return sc_vec(v->x, v->y); }
        if (strcmp(name, "angular_velocity") == 0) {
            br_rigidbody *rb = (br_rigidbody *)br_get(rt->w, e, rt->body_id);
            if (!rb) sc_fail(vm, line, "angular_velocity needs a RigidBody");
            return sc_num(rb->angular_velocity);
        }
        if (isupper((unsigned char)name[0])) {                  /* self.Shape: a component */
            br_component c = br_component_find(rt->w, name);
            sc_value v = sc_nil();
            if (c == BR_COMPONENT_INVALID) sc_fail(vm, line, "there is no component called %s", name);
            if (!br_has(rt->w, e, c)) return sc_nil();
            v.t = V_COMP;
            v.as.comp.e = e;
            v.as.comp.c = c;
            return v;
        }
        other = sc_instance_of(rt, e);                         /* other.health: its script's variable */
        if (other && other->prog && other->ready && !other->failed) {
            int g = sc_find_global(other->prog, name);
            if (g >= 0) return other->globals[g];
        }
        sc_fail(vm, line, "%s has no property '%s'", sc_entity_name(rt, e)[0] ? sc_entity_name(rt, e) : "the entity", name);
    }
    sc_fail(vm, line, "%s has no property '%s'", sc_type_name(obj), name);
    return sc_nil();
}

static void sc_set_member(sc_vm *vm, const sc_value *obj, const char *name, const sc_value *val, int line) {
    sc_runtime *rt = vm->rt;
    if (obj->t == V_COMP) {
        const br_component_desc *d = br_component_get_desc(rt->w, obj->as.comp.c);
        void *data = br_get(rt->w, obj->as.comp.e, obj->as.comp.c);
        int i;
        if (!data) sc_fail(vm, line, "that %s was removed", br_component_name(rt->w, obj->as.comp.c));
        for (i = 0; d && i < d->field_count; ++i)
            if (strcmp(d->fields[i].name, name) == 0) { sc_field_set(vm, &d->fields[i], data, val, line); return; }
        sc_fail(vm, line, "%s has no field '%s'", br_component_name(rt->w, obj->as.comp.c), name);
    }
    if (obj->t == V_ENT) {
        br_entity e = sc_need_entity(vm, obj, line, "setting a property");
        sc_instance *other;
        if (strcmp(name, "name") == 0) {
            br_name *n = (br_name *)br_get(rt->w, e, rt->ids.name);
            if (!n) n = (br_name *)br_add(rt->w, e, rt->ids.name, NULL);
            if (n) snprintf(n->text, sizeof(n->text), "%s", sc_need_str(vm, val, line, "name"));
            return;
        }
        if (strcmp(name, "position") == 0) { sc_transform(vm, e, line)->position = sc_need_vec(vm, val, line, "position"); return; }
        if (strcmp(name, "x") == 0) { sc_transform(vm, e, line)->position.x = (float)sc_need_num(vm, val, line, "x"); return; }
        if (strcmp(name, "y") == 0) { sc_transform(vm, e, line)->position.y = (float)sc_need_num(vm, val, line, "y"); return; }
        if (strcmp(name, "rotation") == 0) { sc_transform(vm, e, line)->rotation = (float)sc_need_num(vm, val, line, "rotation"); return; }
        if (strcmp(name, "scale") == 0) {
            br_transform *t = sc_transform(vm, e, line);
            if (val->t == V_NUM) t->scale = br_v2((float)val->as.n, (float)val->as.n);
            else t->scale = sc_need_vec(vm, val, line, "scale");
            return;
        }
        if (strcmp(name, "velocity") == 0) { *sc_velocity(vm, e, line) = sc_need_vec(vm, val, line, "velocity"); return; }
        if (strcmp(name, "global_position") == 0) {
            sc_transform(vm, e, line);
            br_set_world_position(rt->w, e, sc_need_vec(vm, val, line, "global_position"));
            return;
        }
        if (strcmp(name, "global_rotation") == 0) {
            sc_transform(vm, e, line);
            br_set_world_rotation(rt->w, e, (float)sc_need_num(vm, val, line, "global_rotation"));
            return;
        }
        if (strcmp(name, "parent") == 0) {                 /* stays where it is on screen */
            br_entity p = BR_ENTITY_NULL;
            if (val->t != V_NIL) p = sc_need_entity(vm, val, line, "parent");
            if (!br_set_parent(rt->w, e, p, true)) sc_fail(vm, line, "can't make an entity a child of itself or of its own child");
            return;
        }
        if (strcmp(name, "angular_velocity") == 0) {
            br_rigidbody *rb = (br_rigidbody *)br_get(rt->w, e, rt->body_id);
            if (!rb) sc_fail(vm, line, "angular_velocity needs a RigidBody");
            rb->angular_velocity = (float)sc_need_num(vm, val, line, "angular_velocity");
            return;
        }
        if (strcmp(name, "alive") == 0 || strcmp(name, "id") == 0) sc_fail(vm, line, "%s can't be changed", name);
        if (isupper((unsigned char)name[0])) sc_fail(vm, line, "set a component's fields, like self.%s.color", name);
        other = sc_instance_of(rt, e);
        if (other && other->prog && other->ready && !other->failed) {
            int g = sc_find_global(other->prog, name);
            if (g >= 0) {
                if (other->prog->globals[g].ty.t) sc_check_value(vm, other->prog->globals[g].ty, val, line, name);
                other->globals[g] = *val;
                return;
            }
        }
        sc_fail(vm, line, "%s has no property '%s'", sc_entity_name(rt, e)[0] ? sc_entity_name(rt, e) : "the entity", name);
    }
    sc_fail(vm, line, "can't set '%s' on %s", name, sc_type_name(obj));
}

/* ---- built-in functions --------------------------------------------------------- */

#define ARGS sc_vm *vm, sc_value *a, int n, int line
#define UNUSED_ARGS BR_UNUSED(vm); BR_UNUSED(a); BR_UNUSED(n); BR_UNUSED(line)
#define NUM(i, what) sc_need_num(vm, &a[i], line, what)

static sc_value bi_print(ARGS) {
    char buf[1024];
    int i;
    buf[0] = 0;
    for (i = 0; i < n; ++i) {
        if (i) strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
        sc_append(vm, buf, sizeof(buf), &a[i], 0);
    }
    if (g_print) g_print(buf, g_print_user);
    else BR_INFO("[%s] %s", vm->inst && vm->inst->prog ? vm->inst->prog->name : "script", buf);
    BR_UNUSED(line);
    return sc_nil();
}
static sc_value bi_str(ARGS) { BR_UNUSED(n); BR_UNUSED(line); return sc_to_string(vm, &a[0]); }
static sc_value bi_num(ARGS) {
    char *end;
    double v;
    BR_UNUSED(n);
    if (a[0].t == V_NUM) return a[0];
    v = strtod(sc_need_str(vm, &a[0], line, "num()"), &end);
    return end == a[0].as.s->chars ? sc_nil() : sc_num(v);
}
static sc_value bi_floor(ARGS) { BR_UNUSED(n); return sc_num(floor(NUM(0, "floor()"))); }
static sc_value bi_ceil(ARGS) { BR_UNUSED(n); return sc_num(ceil(NUM(0, "ceil()"))); }
static sc_value bi_round(ARGS) { BR_UNUSED(n); return sc_num(floor(NUM(0, "round()") + 0.5)); }
static sc_value bi_abs(ARGS) { BR_UNUSED(n); return sc_num(fabs(NUM(0, "abs()"))); }
static sc_value bi_sqrt(ARGS) { double v; BR_UNUSED(n); v = NUM(0, "sqrt()"); if (v < 0) sc_fail(vm, line, "sqrt() of a negative number"); return sc_num(sqrt(v)); }
static sc_value bi_sin(ARGS) { BR_UNUSED(n); return sc_num(sin(NUM(0, "sin()") * (BR_PI / 180.0))); }
static sc_value bi_cos(ARGS) { BR_UNUSED(n); return sc_num(cos(NUM(0, "cos()") * (BR_PI / 180.0))); }
static sc_value bi_atan2(ARGS) { BR_UNUSED(n); return sc_num(atan2(NUM(0, "atan2()"), NUM(1, "atan2()")) * (180.0 / BR_PI)); }
static sc_value bi_pow(ARGS) { BR_UNUSED(n); return sc_num(pow(NUM(0, "pow()"), NUM(1, "pow()"))); }
static sc_value bi_sign(ARGS) { double v; BR_UNUSED(n); v = NUM(0, "sign()"); return sc_num(v > 0 ? 1 : v < 0 ? -1 : 0); }
static sc_value bi_min(ARGS) { int i; double m = NUM(0, "min()"); for (i = 1; i < n; ++i) if (NUM(i, "min()") < m) m = a[i].as.n; return sc_num(m); }
static sc_value bi_max(ARGS) { int i; double m = NUM(0, "max()"); for (i = 1; i < n; ++i) if (NUM(i, "max()") > m) m = a[i].as.n; return sc_num(m); }
static sc_value bi_clamp(ARGS) { double v = NUM(0, "clamp()"), lo = NUM(1, "clamp()"), hi = NUM(2, "clamp()"); BR_UNUSED(n); return sc_num(v < lo ? lo : v > hi ? hi : v); }
static sc_value bi_lerp(ARGS) {
    double t = NUM(2, "lerp()");
    BR_UNUSED(n);
    if (a[0].t == V_VEC && a[1].t == V_VEC)
        return sc_vec(a[0].as.v.x + (a[1].as.v.x - a[0].as.v.x) * t, a[0].as.v.y + (a[1].as.v.y - a[0].as.v.y) * t);
    return sc_num(NUM(0, "lerp()") + (NUM(1, "lerp()") - a[0].as.n) * t);
}
static sc_value bi_random(ARGS) {
    double u = (double)br_rng_float(&vm->rt->rng);
    if (n == 0) return sc_num(u);
    if (n == 1) return sc_num(u * NUM(0, "random()"));
    return sc_num(NUM(0, "random()") + u * (NUM(1, "random()") - a[0].as.n));
}
static sc_value bi_vec2(ARGS) { BR_UNUSED(n); return sc_vec(NUM(0, "vec2()"), NUM(1, "vec2()")); }
static sc_value bi_length(ARGS) { br_vec2 v = sc_need_vec(vm, &a[0], line, "length()"); BR_UNUSED(n); return sc_num(sqrt((double)v.x * v.x + (double)v.y * v.y)); }
static sc_value bi_normalize(ARGS) {
    double x, y, l;
    BR_UNUSED(n);
    sc_need_vec(vm, &a[0], line, "normalize()");
    x = a[0].as.v.x; y = a[0].as.v.y; l = sqrt(x * x + y * y);
    return l > 0 ? sc_vec(x / l, y / l) : sc_vec(0, 0);
}
static sc_value bi_dot(ARGS) {
    BR_UNUSED(n);
    sc_need_vec(vm, &a[0], line, "dot()"); sc_need_vec(vm, &a[1], line, "dot()");
    return sc_num(a[0].as.v.x * a[1].as.v.x + a[0].as.v.y * a[1].as.v.y);
}
static sc_value bi_distance(ARGS) {
    double dx, dy;
    BR_UNUSED(n);
    sc_need_vec(vm, &a[0], line, "distance()"); sc_need_vec(vm, &a[1], line, "distance()");
    dx = a[1].as.v.x - a[0].as.v.x; dy = a[1].as.v.y - a[0].as.v.y;
    return sc_num(sqrt(dx * dx + dy * dy));
}
static sc_value bi_angle(ARGS) { BR_UNUSED(n); sc_need_vec(vm, &a[0], line, "angle()"); return sc_num(atan2(a[0].as.v.y, a[0].as.v.x) * (180.0 / BR_PI)); }
static sc_value bi_from_angle(ARGS) { double d = NUM(0, "from_angle()") * (BR_PI / 180.0), l = n > 1 ? NUM(1, "from_angle()") : 1; return sc_vec(cos(d) * l, sin(d) * l); }
static sc_value bi_rgb(ARGS) {
    char text[16];
    int ch[4], i;
    ch[3] = 255;
    for (i = 0; i < n; ++i) ch[i] = clamp_byte(NUM(i, "rgb()"));
    snprintf(text, sizeof(text), "#%02x%02x%02x%02x", ch[0], ch[1], ch[2], ch[3]);
    return sc_string(vm, text, strlen(text));
}

/* ---- lists ---- */
static void sc_list_push(sc_vm *vm, sc_list *l, sc_value v, int line) {
    if (l->count == l->cap) {
        uint32_t cap = l->cap ? l->cap * 2 : 8;
        sc_value *g = (sc_value *)br_realloc(l->items, sizeof(sc_value) * cap);
        if (!g) sc_fail(vm, line, "out of memory");
        vm->rt->heap.bytes += sizeof(sc_value) * (cap - l->cap);
        vm->rt->heap.since_gc += sizeof(sc_value) * (cap - l->cap);
        l->items = g;
        l->cap = cap;
    }
    l->items[l->count++] = v;
}
static sc_value sc_new_list(sc_vm *vm, int line) {
    sc_value v = sc_nil();
    v.t = V_LIST;
    if (!(v.as.l = sc_list_alloc(&vm->rt->heap))) sc_fail(vm, line, "out of memory");
    return v;
}
static sc_list *sc_need_list(sc_vm *vm, const sc_value *v, int line, const char *what) {
    if (v->t != V_LIST) sc_fail(vm, line, "%s needs a list, not %s", what, sc_type_name(v));
    return v->as.l;
}
static sc_value bi_len(ARGS) {
    BR_UNUSED(n);
    if (a[0].t == V_LIST) return sc_num(a[0].as.l->count);
    if (a[0].t == V_STR) return sc_num(a[0].as.s->len);
    sc_fail(vm, line, "len() needs a list or a string, not %s", sc_type_name(&a[0]));
    return sc_nil();
}
static sc_value bi_push(ARGS) { BR_UNUSED(n); sc_list_push(vm, sc_need_list(vm, &a[0], line, "push()"), a[1], line); return a[0]; }
static sc_value bi_pop(ARGS) {
    sc_list *l = sc_need_list(vm, &a[0], line, "pop()");
    BR_UNUSED(n);
    if (!l->count) sc_fail(vm, line, "pop() from an empty list");
    return l->items[--l->count];
}
static sc_value bi_remove(ARGS) {
    sc_list *l = sc_need_list(vm, &a[0], line, "remove()");
    double i = NUM(1, "remove()");
    sc_value v;
    BR_UNUSED(n);
    if (i < 0) i += l->count;
    if (i < 0 || i >= l->count || i != floor(i)) sc_fail(vm, line, "remove(): index %g is out of range", a[1].as.n);
    v = l->items[(uint32_t)i];
    memmove(l->items + (uint32_t)i, l->items + (uint32_t)i + 1, sizeof(sc_value) * (l->count - (uint32_t)i - 1));
    l->count--;
    return v;
}
static sc_value bi_contains(ARGS) {
    uint32_t i;
    sc_list *l = sc_need_list(vm, &a[0], line, "contains()");
    BR_UNUSED(n);
    for (i = 0; i < l->count; ++i) if (sc_equal(&l->items[i], &a[1])) return sc_bool(true);
    return sc_bool(false);
}
static sc_value bi_range(ARGS) {
    double from = n > 1 ? NUM(0, "range()") : 0, to = n > 1 ? NUM(1, "range()") : NUM(0, "range()"), step = n > 2 ? NUM(2, "range()") : 1, k;
    sc_value v = sc_new_list(vm, line);
    if (step == 0) sc_fail(vm, line, "range() step can't be 0");
    if (fabs((to - from) / step) > 1000000) sc_fail(vm, line, "range() is too long");
    for (k = from; step > 0 ? k < to : k > to; k += step) sc_list_push(vm, v.as.l, sc_num(k), line);
    return v;
}

/* ---- input ---- */
static br_key sc_key_named(const char *name) {
    static const struct { const char *alias; br_key k; } aliases[] = {
        { "space", BR_KEY_SPACE }, { "enter", BR_KEY_ENTER }, { "return", BR_KEY_ENTER }, { "escape", BR_KEY_ESCAPE },
        { "esc", BR_KEY_ESCAPE }, { "shift", BR_KEY_LEFT_SHIFT }, { "ctrl", BR_KEY_LEFT_CONTROL }, { "alt", BR_KEY_LEFT_ALT },
        { "tab", BR_KEY_TAB }, { "backspace", BR_KEY_BACKSPACE },
    };
    size_t i;
    int k;
    for (i = 0; i < BR_ARRAY_COUNT(aliases); ++i) if (sc_strieq(name, aliases[i].alias)) return aliases[i].k;
    for (k = 1; k < BR_KEY_COUNT; ++k) {
        const char *kn = br_key_name((br_key)k);
        if (kn && kn[0] && sc_strieq(kn, name)) return (br_key)k;
    }
    return BR_KEY_UNKNOWN;
}
static bool sc_key_state(sc_vm *vm, const sc_value *a, int line, int which) {
    br_engine *e = sc_engine(vm);
    const char *name = sc_need_str(vm, a, line, "a key");
    br_key k = sc_key_named(name);
    bool r;
    if (k == BR_KEY_UNKNOWN) sc_fail(vm, line, "there is no key called \"%s\"", name);
    if (!e) return false;
    r = which == 0 ? br_key_down(&e->input, k) : which == 1 ? br_key_pressed(&e->input, k) : br_key_released(&e->input, k);
    if (!r && (k == BR_KEY_LEFT_SHIFT || k == BR_KEY_LEFT_CONTROL || k == BR_KEY_LEFT_ALT)) {   /* either side */
        br_key other = k == BR_KEY_LEFT_SHIFT ? BR_KEY_RIGHT_SHIFT : k == BR_KEY_LEFT_CONTROL ? BR_KEY_RIGHT_CONTROL : BR_KEY_RIGHT_ALT;
        r = which == 0 ? br_key_down(&e->input, other) : which == 1 ? br_key_pressed(&e->input, other) : br_key_released(&e->input, other);
    }
    return r;
}
static sc_value bi_key_down(ARGS) { BR_UNUSED(n); return sc_bool(sc_key_state(vm, &a[0], line, 0)); }
/* last_key(): the name of a key pressed this step ("" if none), as key_down() takes it. */
static sc_value bi_last_key(ARGS) {
    br_engine *e = sc_engine(vm);
    char name[32];
    size_t i;
    int k;
    UNUSED_ARGS;
    if (!e) return sc_string(vm, "", 0);
    for (k = 1; k < BR_KEY_COUNT; ++k) {
        const char *kn;
        if (!br_key_pressed(&e->input, (br_key)k) || !(kn = br_key_name((br_key)k)) || strcmp(kn, "Unknown") == 0) continue;
        snprintf(name, sizeof(name), "%s", kn);
        for (i = 0; name[i]; ++i) name[i] = (char)tolower((unsigned char)name[i]);
        return sc_string(vm, name, strlen(name));
    }
    return sc_string(vm, "", 0);
}
static sc_value bi_key_pressed(ARGS) { BR_UNUSED(n); return sc_bool(sc_key_state(vm, &a[0], line, 1)); }
static sc_value bi_key_released(ARGS) { BR_UNUSED(n); return sc_bool(sc_key_state(vm, &a[0], line, 2)); }
static sc_value bi_mouse_down(ARGS) {
    br_engine *e = sc_engine(vm);
    int b = n ? (int)NUM(0, "mouse_down()") : 0;
    if (b < 0 || b >= BR_MOUSE_BUTTON_COUNT) sc_fail(vm, line, "mouse buttons are 0 (left), 1 (right) and 2 (middle)");
    return sc_bool(e && br_mouse_down(&e->input, (br_mouse_button)b));
}
static sc_value bi_mouse_pressed(ARGS) {
    br_engine *e = sc_engine(vm);
    int b = n ? (int)NUM(0, "mouse_pressed()") : 0;
    if (b < 0 || b >= BR_MOUSE_BUTTON_COUNT) sc_fail(vm, line, "mouse buttons are 0 (left), 1 (right) and 2 (middle)");
    return sc_bool(e && br_mouse_pressed(&e->input, (br_mouse_button)b));
}
static sc_value bi_mouse_position(ARGS) {
    br_engine *e = sc_engine(vm);
    UNUSED_ARGS;
    return e ? sc_vec(e->input.mouse_x, e->input.mouse_y) : sc_vec(0, 0);
}
/* Gamepads: every controller in the same Xbox-style layout (input.h). Buttons
 * by name - "a" "b" "x" "y", "lb" "rb", "lt" "rt" (the triggers, as buttons),
 * "back" "start" "guide", "ls" "rs" (the sticks, pressed in), "up" "down"
 * "left" "right" (the d-pad) - with the other usual names too (PlayStation's
 * "cross" "circle"..., "l1" "r2", "select" "menu"...). Without a pad number,
 * any pad. */
#define SC_PAD_LT BR_PAD_BUTTON_COUNT                   /* the triggers, after the real buttons */
#define SC_PAD_RT (BR_PAD_BUTTON_COUNT + 1)
#define SC_PAD_ANY_COUNT (BR_PAD_BUTTON_COUNT + 2)
static const char *const k_pad_button_names[SC_PAD_ANY_COUNT] = {
    "a", "b", "x", "y", "lb", "rb", "back", "start", "guide", "ls", "rs", "up", "right", "down", "left", "lt", "rt"
};
static int sc_pad_button_named(const char *name) {
    static const struct { const char *alias; int b; } aliases[] = {
        { "cross", BR_PAD_A }, { "circle", BR_PAD_B }, { "square", BR_PAD_X }, { "triangle", BR_PAD_Y },
        { "left_bumper", BR_PAD_LEFT_BUMPER }, { "right_bumper", BR_PAD_RIGHT_BUMPER }, { "l1", BR_PAD_LEFT_BUMPER },
        { "r1", BR_PAD_RIGHT_BUMPER }, { "left_trigger", SC_PAD_LT }, { "right_trigger", SC_PAD_RT }, { "l2", SC_PAD_LT },
        { "r2", SC_PAD_RT }, { "select", BR_PAD_BACK }, { "view", BR_PAD_BACK }, { "share", BR_PAD_BACK },
        { "menu", BR_PAD_START }, { "options", BR_PAD_START }, { "home", BR_PAD_GUIDE }, { "left_stick", BR_PAD_LEFT_STICK },
        { "right_stick", BR_PAD_RIGHT_STICK }, { "l3", BR_PAD_LEFT_STICK }, { "r3", BR_PAD_RIGHT_STICK },
        { "dpad_up", BR_PAD_DPAD_UP }, { "dpad_right", BR_PAD_DPAD_RIGHT }, { "dpad_down", BR_PAD_DPAD_DOWN },
        { "dpad_left", BR_PAD_DPAD_LEFT },
    };
    size_t i;
    for (i = 0; i < BR_ARRAY_COUNT(k_pad_button_names); ++i) if (sc_strieq(name, k_pad_button_names[i])) return (int)i;
    for (i = 0; i < BR_ARRAY_COUNT(aliases); ++i) if (sc_strieq(name, aliases[i].alias)) return aliases[i].b;
    return -1;
}
/* The pad argument at a[i] (-1: none given, any pad). */
static int sc_pad_arg(sc_vm *vm, sc_value *a, int n, int i, int line, const char *what) {
    int pad;
    if (n <= i || a[i].t == V_NIL) return -1;
    pad = (int)sc_need_num(vm, &a[i], line, what);
    if (pad < 0 || pad >= BR_MAX_GAMEPADS) sc_fail(vm, line, "gamepads are numbered 0 to %d", BR_MAX_GAMEPADS - 1);
    return pad;
}
/* which: 0 down, 1 pressed this step, 2 released this step */
static bool sc_pad_button_on(const br_input *in, int pad, int b, int which) {
    const br_gamepad_state *p = &in->pads[pad];
    if (b >= BR_PAD_BUTTON_COUNT) {
        int t = b - SC_PAD_LT;
        return which == 0 ? p->triggers[t] : which == 1 ? p->trigger_pressed[t] : p->trigger_released[t];
    }
    return which == 0 ? p->buttons[b] : which == 1 ? p->pressed[b] : p->released[b];
}
static bool sc_pad_button_state(sc_vm *vm, sc_value *a, int n, int line, int which, const char *what) {
    br_engine *e = sc_engine(vm);
    const char *name = sc_need_str(vm, &a[0], line, "a gamepad button");
    int b = sc_pad_button_named(name), pad = sc_pad_arg(vm, a, n, 1, line, what), p;
    if (b < 0) sc_fail(vm, line, "there is no gamepad button called \"%s\" (a b x y, lb rb lt rt, back start guide, ls rs, up down left right)", name);
    if (!e) return false;
    for (p = 0; p < BR_MAX_GAMEPADS; ++p)
        if ((pad < 0 || pad == p) && sc_pad_button_on(&e->input, p, b, which)) return true;
    return false;
}
static sc_value bi_button_down(ARGS) { return sc_bool(sc_pad_button_state(vm, a, n, line, 0, "button_down()")); }
static sc_value bi_button_pressed(ARGS) { return sc_bool(sc_pad_button_state(vm, a, n, line, 1, "button_pressed()")); }
static sc_value bi_button_released(ARGS) { return sc_bool(sc_pad_button_state(vm, a, n, line, 2, "button_released()")); }
/* axis(name[, pad]): a stick's "left_x" "left_y" "right_x" "right_y" (-1 to 1; +y is down), or a trigger's
 * "lt" "rt" (0 to 1), past the dead zone. Without a pad, the one pushed furthest. */
static sc_value bi_axis(ARGS) {
    static const struct { const char *name; int axis; } names[] = {
        { "left_x", BR_PAD_AXIS_LEFT_X }, { "left_y", BR_PAD_AXIS_LEFT_Y }, { "right_x", BR_PAD_AXIS_RIGHT_X },
        { "right_y", BR_PAD_AXIS_RIGHT_Y }, { "lt", BR_PAD_AXIS_LEFT_TRIGGER }, { "rt", BR_PAD_AXIS_RIGHT_TRIGGER },
        { "left_trigger", BR_PAD_AXIS_LEFT_TRIGGER }, { "right_trigger", BR_PAD_AXIS_RIGHT_TRIGGER },
        { "l2", BR_PAD_AXIS_LEFT_TRIGGER }, { "r2", BR_PAD_AXIS_RIGHT_TRIGGER },
    };
    br_engine *e = sc_engine(vm);
    const char *name = sc_need_str(vm, &a[0], line, "an axis");
    int ax = -1, pad = sc_pad_arg(vm, a, n, 1, line, "axis()"), p;
    float best = 0;
    size_t i;
    for (i = 0; i < BR_ARRAY_COUNT(names); ++i) if (sc_strieq(name, names[i].name)) ax = names[i].axis;
    if (ax < 0) sc_fail(vm, line, "there is no axis called \"%s\" (left_x left_y right_x right_y, lt rt)", name);
    if (!e) return sc_num(0);
    for (p = 0; p < BR_MAX_GAMEPADS; ++p) {
        float v;
        if (pad >= 0 && pad != p) continue;
        v = br_pad_axis(&e->input, p, (br_gamepad_axis)ax);
        if (fabsf(v) > fabsf(best)) best = v;
    }
    return sc_num(best);
}
/* stick([which[, pad]]): "left" (the default) or "right" stick as a vec2, each part -1 to 1 (+y down). */
static sc_value bi_stick(ARGS) {
    br_engine *e = sc_engine(vm);
    const char *which = n > 0 ? sc_need_str(vm, &a[0], line, "\"left\" or \"right\"") : "left";
    int base = BR_PAD_AXIS_LEFT_X, pad = sc_pad_arg(vm, a, n, 1, line, "stick()"), p;
    float bx = 0, by = 0;
    if (sc_strieq(which, "left")) base = BR_PAD_AXIS_LEFT_X;
    else if (sc_strieq(which, "right")) base = BR_PAD_AXIS_RIGHT_X;
    else sc_fail(vm, line, "stick() takes \"left\" or \"right\", not \"%s\"", which);
    if (!e) return sc_vec(0, 0);
    for (p = 0; p < BR_MAX_GAMEPADS; ++p) {
        float x, y;
        if (pad >= 0 && pad != p) continue;
        x = br_pad_axis(&e->input, p, (br_gamepad_axis)base);
        y = br_pad_axis(&e->input, p, (br_gamepad_axis)(base + 1));
        if (x * x + y * y > bx * bx + by * by) { bx = x; by = y; }
    }
    return sc_vec(bx, by);
}
/* gamepad_connected([pad]): that pad, or (without one) any. */
static sc_value bi_gamepad_connected(ARGS) {
    br_engine *e = sc_engine(vm);
    int pad = sc_pad_arg(vm, a, n, 0, line, "gamepad_connected()"), p;
    if (!e) return sc_bool(false);
    for (p = 0; p < BR_MAX_GAMEPADS; ++p)
        if ((pad < 0 || pad == p) && br_pad_connected(&e->input, p)) return sc_bool(true);
    return sc_bool(false);
}
static sc_value bi_gamepad_count(ARGS) {
    br_engine *e = sc_engine(vm);
    int p, count = 0;
    UNUSED_ARGS;
    if (e) for (p = 0; p < BR_MAX_GAMEPADS; ++p) count += br_pad_connected(&e->input, p);
    return sc_num(count);
}
/* gamepad_name([pad]): what the controller calls itself ("" if none); without a pad, the first connected. */
static sc_value bi_gamepad_name(ARGS) {
    br_engine *e = sc_engine(vm);
    int pad = sc_pad_arg(vm, a, n, 0, line, "gamepad_name()"), p;
    if (e) for (p = 0; p < BR_MAX_GAMEPADS; ++p) {
        if ((pad < 0 || pad == p) && br_pad_connected(&e->input, p)) {
            const char *name = br_pad_name(&e->input, p);
            return sc_string(vm, name, strlen(name));
        }
    }
    return sc_string(vm, "", 0);
}
/* last_button(): the name of a gamepad button pressed this step on any pad ("" if none), as
 * button_down() takes it: for letting players choose their buttons. */
static sc_value bi_last_button(ARGS) {
    br_engine *e = sc_engine(vm);
    int p, b;
    UNUSED_ARGS;
    if (e) for (p = 0; p < BR_MAX_GAMEPADS; ++p)
        for (b = 0; b < SC_PAD_ANY_COUNT; ++b)
            if (sc_pad_button_on(&e->input, p, b, 1)) return sc_string(vm, k_pad_button_names[b], strlen(k_pad_button_names[b]));
    return sc_string(vm, "", 0);
}
/* last_input(): what the player touched last - "keyboard", "mouse" or "gamepad" ("" before anything) -
 * kept across scenes: for showing a key's name or a button's. */
static sc_value bi_last_input(ARGS) {
    br_engine *e = sc_engine(vm);
    const char *name;
    UNUSED_ARGS;
    name = e ? br_input_device_name(e->input.last_device) : "";
    return sc_string(vm, name, strlen(name));
}
/* rumble(strength, seconds[, pad]): shakes the pad (or every pad): strength 0 to 1; 0 stops it.
 * True if a pad could. */
static sc_value bi_rumble(ARGS) {
    br_engine *e = sc_engine(vm);
    double strength = NUM(0, "rumble()'s strength"), seconds = NUM(1, "rumble()'s seconds");
    int pad = sc_pad_arg(vm, a, n, 2, line, "rumble()"), p;
    bool any = false;
    if (!e) return sc_bool(false);
    for (p = 0; p < BR_MAX_GAMEPADS; ++p) {
        if (pad >= 0 && pad != p) continue;
        if (pad < 0 && !br_pad_connected(&e->input, p)) continue;
        if (br_gamepad_rumble(e, p, (float)strength, (float)strength * 0.6f, (float)seconds)) any = true;
    }
    return sc_bool(any);
}

static sc_value bi_time(ARGS) { br_engine *e = sc_engine(vm); UNUSED_ARGS; return sc_num(e ? e->time : 0); }

/* ---- the world ---- */
static sc_value bi_find(ARGS) {
    const char *name = sc_need_str(vm, &a[0], line, "find()");
    const br_component_pool *p = &vm->rt->w->pools[vm->rt->ids.name];
    uint32_t i;
    BR_UNUSED(n);
    for (i = 0; i < p->count; ++i)
        if (strcmp(((const br_name *)(p->data + (size_t)i * p->stride))->text, name) == 0) return sc_ent(p->entities[i]);
    return sc_nil();
}
static sc_value bi_find_all(ARGS) {
    const char *name = sc_need_str(vm, &a[0], line, "find_all()");
    const br_component_pool *p = &vm->rt->w->pools[vm->rt->ids.name];
    sc_value list = sc_new_list(vm, line);
    uint32_t i;
    BR_UNUSED(n);
    for (i = 0; i < p->count; ++i)
        if (strcmp(((const br_name *)(p->data + (size_t)i * p->stride))->text, name) == 0) sc_list_push(vm, list.as.l, sc_ent(p->entities[i]), line);
    return list;
}
static sc_value bi_destroy(ARGS) {
    BR_UNUSED(n);
    if (a[0].t == V_ENT && br_entity_alive(vm->rt->w, a[0].as.e)) br_entity_destroy_deferred(vm->rt->w, a[0].as.e);
    else if (a[0].t != V_ENT) sc_fail(vm, line, "destroy() needs an entity, not %s", sc_type_name(&a[0]));
    return sc_nil();
}
static sc_instance *sc_instance_create(sc_runtime *rt, br_entity e, const br_script *s);
static void sc_make_ready(sc_runtime *rt, sc_instance *inst, const br_script *s);

static sc_value bi_spawn(ARGS) {
    sc_runtime *rt = vm->rt;
    const char *path = sc_need_str(vm, &a[0], line, "spawn()");
    char full[BR_PATH_MAX * 2], err[200];
    br_entity *made;
    sc_value first;
    size_t size;
    char *text;
    const char *p;
    int count, i, cap = 1;
    snprintf(full, sizeof(full), "%s%s", path[0] == '/' ? "" : br_assets_root(), path);
    if (!(text = (char *)br_file_read_all(full, &size))) sc_fail(vm, line, "spawn(): can't read %s", path);
    for (p = text; (p = strstr(p, "entity")) != NULL; p += 6) ++cap;   /* at least one per entity in the file */
    if (!(made = (br_entity *)br_alloc((size_t)cap * sizeof(br_entity)))) { br_free(text); sc_fail(vm, line, "out of memory"); }
    count = br_world_from_text(rt->w, text, made, (uint32_t)cap, err, sizeof(err));
    br_free(text);
    if (count < 0) { br_free(made); sc_fail(vm, line, "spawn(): %s: %s", path, err); }
    if (count == 0) { br_free(made); return sc_nil(); }
    if (count > cap) count = cap;
    if (n > 2 && a[2].t != V_NIL) {                        /* under a parent: pos is in its space */
        br_entity parent = sc_need_entity(vm, &a[2], line, "spawn() parent");
        for (i = 0; i < count; ++i)
            if (br_parent_of(rt->w, made[i]) == BR_ENTITY_NULL) br_set_parent(rt->w, made[i], parent, false);
    }
    if (n > 1 && a[1].t != V_NIL) {                       /* move it so the first entity lands at pos */
        br_vec2 at = sc_need_vec(vm, &a[1], line, "spawn() position");
        br_transform *t0 = (br_transform *)br_get(rt->w, made[0], rt->ids.transform);
        br_vec2 shift = t0 ? br_v2_sub(at, t0->position) : br_v2(0, 0);
        for (i = 0; i < count; ++i) {                      /* the file's roots; their children follow */
            br_transform *t = (br_transform *)br_get(rt->w, made[i], rt->ids.transform);
            br_entity pe = br_parent_of(rt->w, made[i]);
            bool root = pe == BR_ENTITY_NULL || (n > 2 && a[2].t == V_ENT && pe == a[2].as.e);
            if (t && root) t->position = br_v2_add(t->position, shift);
        }
    }
    if (rt->script_id != BR_COMPONENT_INVALID) {           /* their variables now, so the caller can set them */
        for (i = 0; i < count; ++i) {
            br_script *sc = (br_script *)br_get(rt->w, made[i], rt->script_id);
            if (!sc) continue;
            if (!sc->instance) sc->instance = sc_instance_create(rt, made[i], sc);
            if (sc->instance) sc_make_ready(rt, (sc_instance *)sc->instance, sc);
        }
    }
    first = sc_ent(made[0]);
    br_free(made);
    return first;
}
static sc_value bi_children(ARGS) {
    br_entity e = sc_need_entity(vm, &a[0], line, "children()"), kids[256];
    uint32_t count = br_children(vm->rt->w, e, kids, 256), i;
    sc_value list = sc_new_list(vm, line);
    BR_UNUSED(n);
    for (i = 0; i < count && i < 256; ++i) sc_list_push(vm, list.as.l, sc_ent(kids[i]), line);
    return list;
}
static sc_value bi_child(ARGS) {
    br_entity e = sc_need_entity(vm, &a[0], line, "child()");
    br_entity c = br_child_named(vm->rt->w, e, sc_need_str(vm, &a[1], line, "child()"));
    BR_UNUSED(n);
    return c == BR_ENTITY_NULL ? sc_nil() : sc_ent(c);
}
static sc_value bi_has(ARGS) {
    br_entity e = sc_need_entity(vm, &a[0], line, "has()");
    br_component c = br_component_find(vm->rt->w, sc_need_str(vm, &a[1], line, "has()"));
    BR_UNUSED(n);
    return sc_bool(c != BR_COMPONENT_INVALID && br_has(vm->rt->w, e, c));
}
static sc_value bi_add(ARGS) {
    br_entity e = sc_need_entity(vm, &a[0], line, "add()");
    const char *name = sc_need_str(vm, &a[1], line, "add()");
    br_component c = br_component_find(vm->rt->w, name);
    sc_value v = sc_nil();
    BR_UNUSED(n);
    if (c == BR_COMPONENT_INVALID || !br_component_get_desc(vm->rt->w, c)) sc_fail(vm, line, "there is no component called %s", name);
    if (!br_has(vm->rt->w, e, c)) br_add(vm->rt->w, e, c, NULL);
    v.t = V_COMP;
    v.as.comp.e = e;
    v.as.comp.c = c;
    return v;
}
static sc_value bi_remove_component(ARGS) {
    br_entity e = sc_need_entity(vm, &a[0], line, "remove_component()");
    br_component c = br_component_find(vm->rt->w, sc_need_str(vm, &a[1], line, "remove_component()"));
    BR_UNUSED(n);
    if (c == vm->rt->script_id && e == vm->inst->e) sc_fail(vm, line, "a script can't remove its own Script");
    if (c != BR_COMPONENT_INVALID) br_remove(vm->rt->w, e, c);
    return sc_nil();
}
static sc_value bi_apply_force(ARGS) {
    br_entity e = sc_need_entity(vm, &a[0], line, "apply_force()");
    br_vec2 f = sc_need_vec(vm, &a[1], line, "apply_force()"), p;
    if (n > 2) { p = sc_need_vec(vm, &a[2], line, "apply_force() point"); br_physics_apply_force(vm->rt->w, e, f, &p); }
    else br_physics_apply_force(vm->rt->w, e, f, NULL);
    return sc_nil();
}
static sc_value bi_apply_impulse(ARGS) {
    br_entity e = sc_need_entity(vm, &a[0], line, "apply_impulse()");
    br_vec2 f = sc_need_vec(vm, &a[1], line, "apply_impulse()"), p;
    if (n > 2) { p = sc_need_vec(vm, &a[2], line, "apply_impulse() point"); br_physics_apply_impulse(vm->rt->w, e, f, &p); }
    else br_physics_apply_impulse(vm->rt->w, e, f, NULL);
    return sc_nil();
}
static sc_value bi_raycast(ARGS) {
    br_vec2 from = sc_need_vec(vm, &a[0], line, "raycast()"), to = sc_need_vec(vm, &a[1], line, "raycast()");
    br_entity ignore = n > 2 && a[2].t == V_ENT ? a[2].as.e : BR_ENTITY_NULL;
    br_raycast_hit hit;
    sc_value list;
    if (!br_physics_raycast(vm->rt->w, from, to, -1, ignore, &hit)) return sc_nil();
    list = sc_new_list(vm, line);                         /* [entity, point, normal] */
    sc_list_push(vm, list.as.l, sc_ent(hit.entity), line);
    sc_list_push(vm, list.as.l, sc_vec(hit.point.x, hit.point.y), line);
    sc_list_push(vm, list.as.l, sc_vec(hit.normal.x, hit.normal.y), line);
    return list;
}
static sc_value bi_entity_at(ARGS) {
    br_entity e = br_physics_point(vm->rt->w, sc_need_vec(vm, &a[0], line, "entity_at()"), -1);
    BR_UNUSED(n);
    return e == BR_ENTITY_NULL ? sc_nil() : sc_ent(e);
}
/* Tilemaps: the map entity and a world point. */
static sc_value bi_tile_at(ARGS) {
    br_entity e = sc_need_entity(vm, &a[0], line, "tile_at()");
    char c = br_tilemap_get(vm->rt->w, e, sc_need_vec(vm, &a[1], line, "tile_at() point"), NULL, NULL);
    BR_UNUSED(n);
    return sc_string(vm, &c, c ? 1 : 0);
}
static sc_value bi_set_tile(ARGS) {
    br_entity e = sc_need_entity(vm, &a[0], line, "set_tile()");
    br_vec2 p = sc_need_vec(vm, &a[1], line, "set_tile() point");
    const char *t = sc_need_str(vm, &a[2], line, "set_tile()");
    BR_UNUSED(n);
    if (strlen(t) != 1) sc_fail(vm, line, "set_tile() wants one character, not \"%s\"", t);
    return sc_bool(br_tilemap_set(vm->rt->w, e, p, t[0]));
}
static sc_value bi_tile_solid(ARGS) {
    br_entity e = sc_need_entity(vm, &a[0], line, "tile_solid()");
    BR_UNUSED(n);
    return sc_bool(br_tilemap_solid(vm->rt->w, e, sc_need_vec(vm, &a[1], line, "tile_solid() point")));
}
static sc_value bi_tile_center(ARGS) {
    br_entity e = sc_need_entity(vm, &a[0], line, "tile_center()");
    br_vec2 p = sc_need_vec(vm, &a[1], line, "tile_center() point"), c;
    int col, row;
    BR_UNUSED(n);
    br_tilemap_get(vm->rt->w, e, p, &col, &row);
    if (!br_tilemap_cell_center(vm->rt->w, e, col, row, &c)) sc_fail(vm, line, "tile_center(): the entity has no Tilemap");
    return sc_vec(c.x, c.y);
}
/* substr(text, start[, count]): part of a string; a negative start counts from the end. */
static sc_value bi_substr(ARGS) {
    const char *t = sc_need_str(vm, &a[0], line, "substr()");
    double len = (double)strlen(t), start = NUM(1, "substr() start"), count = n > 2 ? NUM(2, "substr() count") : len;
    start = floor(start);
    if (start < 0) start += len;
    if (start < 0) start = 0;
    if (start > len) start = len;
    count = floor(count);
    if (count < 0) count = 0;
    if (start + count > len) count = len - start;
    return sc_string(vm, t + (size_t)start, (size_t)count);
}

static sc_value bi_play_sound(ARGS) {
    br_engine *e = sc_engine(vm);
    const br_sound *s;
    br_play_params pp;
    if (!e) return sc_nil();
    if (!(s = br_asset_sound(sc_need_str(vm, &a[0], line, "play_sound()")))) sc_fail(vm, line, "play_sound(): can't load %s", a[0].as.s->chars);
    pp = br_play_defaults();
    if (n > 1) pp.volume = (float)NUM(1, "play_sound() volume");
    br_play(&e->mixer, s, &pp);
    return sc_nil();
}
/* set_volume("music", 0.5) / volume("music"): the mixer's buses ("sfx",
 * "music", "ui", "ambient") and "master", 0 (silent) to 1 (full; up to 2). */
static int sc_bus_named(sc_vm *vm, const sc_value *v, int line, const char *what) {
    static const char *const names[] = { "sfx", "music", "ui", "ambient" };
    const char *name = sc_need_str(vm, v, line, what);
    int i;
    if (sc_strieq(name, "master")) return -1;
    for (i = 0; i < (int)BR_ARRAY_COUNT(names); ++i) if (sc_strieq(name, names[i])) return i;
    sc_fail(vm, line, "%s: there is no \"%s\" (master, sfx, music, ui or ambient)", what, name);
    return 0;
}
static sc_value bi_set_volume(ARGS) {
    br_engine *e = sc_engine(vm);
    int bus = sc_bus_named(vm, &a[0], line, "set_volume()");
    float v = (float)NUM(1, "set_volume() volume");
    BR_UNUSED(n);
    v = v < 0 ? 0 : v > 2 ? 2 : v;
    if (e && bus < 0) br_mixer_set_master(&e->mixer, v);
    else if (e) br_mixer_set_bus_volume(&e->mixer, bus, v);
    return sc_nil();
}
static sc_value bi_volume(ARGS) {
    br_engine *e = sc_engine(vm);
    int bus = sc_bus_named(vm, &a[0], line, "volume()");
    BR_UNUSED(n);
    if (!e) return sc_num(1);
    return sc_num(bus < 0 ? e->mixer.master_volume : e->mixer.bus_volume[bus]);
}
static sc_value bi_load_scene(ARGS) {
    BR_UNUSED(n);
    snprintf(vm->rt->scene_request, sizeof(vm->rt->scene_request), "%s", sc_need_str(vm, &a[0], line, "load_scene()"));
    return sc_nil();
}
static sc_value bi_quit(ARGS) { UNUSED_ARGS; vm->rt->quit_request = true; return sc_nil(); }
static sc_value bi_send(ARGS) {
    sc_instance *target;
    const char *fn_name;
    int fn;
    br_entity e = sc_need_entity(vm, &a[0], line, "send()");
    fn_name = sc_need_str(vm, &a[1], line, "send()");
    target = sc_instance_of(vm->rt, e);
    if (!target || !target->prog || target->failed || !target->ready || (fn = sc_find_func(target->prog, fn_name)) < 0) return sc_nil();
    return sc_call_func(vm, target, fn, a + 2, n - 2, line);
}

/* ---- values as text: saved data, and Script.vars ----
 * Written as BrontoScript literals, so a saved file reads like code:
 * [3, "ow_7_7", vec2(128, 152), true, nil]. Entities and components can't
 * be written (they mean nothing in another run). */
#define SC_TEXT_DEPTH 64

typedef struct sc_text { char *p; size_t len, cap; bool failed; } sc_text;

static void sc_text_add(sc_text *t, const char *s, size_t n) {
    if (t->failed) return;
    if (t->len + n + 1 > t->cap) {
        size_t cap = t->cap ? t->cap * 2 : 256;
        char *g;
        while (cap < t->len + n + 1) cap *= 2;
        if (!(g = (char *)br_realloc(t->p, cap))) { t->failed = true; return; }
        t->p = g;
        t->cap = cap;
    }
    memcpy(t->p + t->len, s, n);
    t->len += n;
    t->p[t->len] = 0;
}
static void sc_text_str(sc_text *t, const char *s) { sc_text_add(t, s, strlen(s)); }

/* NULL when written, else why it can't be. */
static const char *sc_write_value(sc_text *t, const sc_value *v, int depth) {
    char num[64];
    uint32_t i;
    if (depth > SC_TEXT_DEPTH) return "lists inside lists too deeply (or a list inside itself)";
    switch (v->t) {
    case V_NIL: sc_text_str(t, "nil"); return NULL;
    case V_BOOL: sc_text_str(t, v->as.b ? "true" : "false"); return NULL;
    case V_NUM:
        if (!isfinite(v->as.n)) return "a number that isn't finite";
        snprintf(num, sizeof(num), "%.17g", v->as.n);
        sc_text_str(t, num);
        return NULL;
    case V_VEC:
        if (!isfinite(v->as.v.x) || !isfinite(v->as.v.y)) return "a vec2 that isn't finite";
        snprintf(num, sizeof(num), "vec2(%.17g, %.17g)", v->as.v.x, v->as.v.y);
        sc_text_str(t, num);
        return NULL;
    case V_STR:
        sc_text_str(t, "\"");
        for (i = 0; i < v->as.s->len; ++i) {
            char c = v->as.s->chars[i];
            if (c == '"' || c == '\\') { sc_text_str(t, "\\"); sc_text_add(t, &c, 1); }
            else if (c == '\n') sc_text_str(t, "\\n");
            else if (c == '\t') sc_text_str(t, "\\t");
            else sc_text_add(t, &c, 1);
        }
        sc_text_str(t, "\"");
        return NULL;
    case V_LIST:
        sc_text_str(t, "[");
        for (i = 0; i < v->as.l->count; ++i) {
            const char *why;
            if (i) sc_text_str(t, ", ");
            if ((why = sc_write_value(t, &v->as.l->items[i], depth + 1)) != NULL) return why;
        }
        sc_text_str(t, "]");
        return NULL;
    case V_ENT: return "an entity";
    case V_COMP: return "a component";
    default: return "something unknown";
    }
}

static bool sc_heap_string(sc_runtime *rt, const char *s, size_t len, sc_value *out) {   /* unescaping \n \t \" \\ */
    char *buf = (char *)br_alloc(len + 1);
    size_t i, n = 0;
    if (!buf) return false;
    for (i = 0; i < len; ++i) {
        char c = s[i];
        if (c == '\\' && i + 1 < len) {
            c = s[++i];
            c = c == 'n' ? '\n' : c == 't' ? '\t' : c;
        }
        buf[n++] = c;
    }
    *out = sc_nil();
    out->t = V_STR;
    out->as.s = sc_str_alloc(&rt->heap, buf, n, false);
    br_free(buf);
    return out->as.s != NULL;
}

static bool sc_read_value(sc_runtime *rt, sc_lexer *L, sc_token t, sc_value *out, int depth);

static bool sc_read_number(sc_lexer *L, sc_token t, double *out) {
    double sign = 1;
    if (t.t == T_MINUS) { sign = -1; t = sc_next(L); }
    if (t.t != T_NUM) return false;
    *out = sign * t.num;
    return true;
}

static bool sc_read_list(sc_runtime *rt, sc_lexer *L, sc_value *out, int depth) {
    sc_list *l = sc_list_alloc(&rt->heap);
    if (!l) return false;
    *out = sc_nil();
    out->t = V_LIST;
    out->as.l = l;
    for (;;) {
        sc_token t = sc_next(L);
        sc_value item;
        if (t.t == T_RBRACKET) return true;              /* [] and [a, b,] */
        if (!sc_read_value(rt, L, t, &item, depth + 1)) return false;
        if (l->count == l->cap) {
            uint32_t cap = l->cap ? l->cap * 2 : 8;
            sc_value *g = (sc_value *)br_realloc(l->items, sizeof(sc_value) * cap);
            if (!g) return false;
            rt->heap.bytes += sizeof(sc_value) * (cap - l->cap);
            rt->heap.since_gc += sizeof(sc_value) * (cap - l->cap);
            l->items = g;
            l->cap = cap;
        }
        l->items[l->count++] = item;
        t = sc_next(L);
        if (t.t == T_RBRACKET) return true;
        if (t.t != T_COMMA) return false;
    }
}

/* One literal, starting with the token `t`. */
static bool sc_read_value(sc_runtime *rt, sc_lexer *L, sc_token t, sc_value *out, int depth) {
    double x, y;
    if (depth > SC_TEXT_DEPTH) return false;
    switch (t.t) {
    case T_NIL: *out = sc_nil(); return true;
    case T_TRUE: case T_FALSE: *out = sc_bool(t.t == T_TRUE); return true;
    case T_NUM: case T_MINUS:
        if (!sc_read_number(L, t, &x)) return false;
        *out = sc_num(x);
        return true;
    case T_STR: return sc_heap_string(rt, t.start, (size_t)t.len, out);
    case T_LBRACKET: return sc_read_list(rt, L, out, depth);
    case T_NAME:
        if (t.len != 4 || strncmp(t.start, "vec2", 4) != 0 || sc_next(L).t != T_LPAREN) return false;
        if (!sc_read_number(L, sc_next(L), &x) || sc_next(L).t != T_COMMA) return false;
        if (!sc_read_number(L, sc_next(L), &y) || sc_next(L).t != T_RPAREN) return false;
        *out = sc_vec(x, y);
        return true;
    default: return false;
    }
}

/* ---- saved data: save_data("slot1", value) keeps it in slot1.txt in the
 * game's folder for saved data (save.h) ---- */
static const char *sc_data_file(sc_vm *vm, const sc_value *name, int line, const char *what, char *out, size_t size) {
    const char *n = sc_need_str(vm, name, line, what);
    snprintf(out, size, "%s.txt", n);
    if (strlen(n) + 4 > BR_SAVE_NAME_MAX || !br_save_name_ok(out))
        sc_fail(vm, line, "%s: \"%s\" can't name saved data (use up to 60 letters, digits, spaces and _-.())", what, n);
    return out;
}
static sc_value bi_save_data(ARGS) {
    char file[BR_SAVE_NAME_MAX + 8];
    sc_text t;
    const char *why;
    bool ok;
    BR_UNUSED(n);
    sc_data_file(vm, &a[0], line, "save_data()", file, sizeof(file));
    memset(&t, 0, sizeof(t));
    if ((why = sc_write_value(&t, &a[1], 0)) != NULL) {
        br_free(t.p);
        sc_fail(vm, line, "save_data(): can't save %s", why);
    }
    sc_text_str(&t, "\n");
    if (t.failed) { br_free(t.p); sc_fail(vm, line, "out of memory"); }
    ok = br_save_write(file, t.p, t.len);
    br_free(t.p);
    return sc_bool(ok);
}
static sc_value bi_load_data(ARGS) {
    char file[BR_SAVE_NAME_MAX + 8], msg[200];
    size_t size;
    char *text;
    sc_lexer L;
    sc_value v;
    sc_data_file(vm, &a[0], line, "load_data()", file, sizeof(file));
    if (!(text = (char *)br_save_read(file, &size))) return n > 1 ? a[1] : sc_nil();
    L.p = text;
    L.line = 1;
    L.depth = 0;
    {
        sc_token t;
        do { t = sc_next(&L); } while (t.t == T_NEWLINE);
        if (sc_read_value(vm->rt, &L, t, &v, 0)) {
            do { t = sc_next(&L); } while (t.t == T_NEWLINE);
            if (t.t == T_EOF) { br_free(text); return v; }
        }
    }
    br_free(text);
    snprintf(msg, sizeof(msg), "load_data(): %s is damaged, so it was left alone (line %d)", file, L.line);
    BR_WARN("script: %s", msg);
    return n > 1 ? a[1] : sc_nil();
}
static sc_value bi_has_data(ARGS) {
    char file[BR_SAVE_NAME_MAX + 8];
    BR_UNUSED(n);
    return sc_bool(br_save_exists(sc_data_file(vm, &a[0], line, "has_data()", file, sizeof(file))));
}
static sc_value bi_delete_data(ARGS) {
    char file[BR_SAVE_NAME_MAX + 8];
    BR_UNUSED(n);
    return sc_bool(br_save_delete(sc_data_file(vm, &a[0], line, "delete_data()", file, sizeof(file))));
}
static sc_value bi_data_names(ARGS) {
    sc_value out = sc_new_list(vm, line);
    br_dir_list files;
    int i;
    BR_UNUSED(a); BR_UNUSED(n);
    if (!br_save_list(&files)) return out;
    for (i = 0; i < files.count; ++i) {
        size_t len = strlen(files.entries[i].name);
        if (len > 4 && strcmp(files.entries[i].name + len - 4, ".txt") == 0)
            sc_list_push(vm, out.as.l, sc_string(vm, files.entries[i].name, len - 4), line);
    }
    br_dir_list_free(&files);
    return out;
}

/* Signatures (for the type checker): a letter per parameter, '*' repeats the
 * last for the rest, then '>' and the result. n number, i int, s string,
 * b bool, v vec2, l list, e entity, a anything, x number or vec2, t list or
 * string, p vec2 or nil, q entity or nil; results also: - nothing, c a
 * component, L list[entity], R list[int], W list[string], S like the
 * arguments, E an item of the list argument. */
static const sc_builtin k_builtin_table[] = {
    { "print", 0, 16, bi_print, "a*>-" }, { "str", 1, 1, bi_str, "a>s" }, { "num", 1, 1, bi_num, "a>a" },
    { "floor", 1, 1, bi_floor, "n>i" }, { "ceil", 1, 1, bi_ceil, "n>i" }, { "round", 1, 1, bi_round, "n>i" },
    { "abs", 1, 1, bi_abs, "n>S" }, { "sqrt", 1, 1, bi_sqrt, "n>n" }, { "sin", 1, 1, bi_sin, "n>n" },
    { "cos", 1, 1, bi_cos, "n>n" }, { "atan2", 2, 2, bi_atan2, "nn>n" }, { "pow", 2, 2, bi_pow, "nn>n" },
    { "sign", 1, 1, bi_sign, "n>i" }, { "min", 1, 16, bi_min, "n*>S" }, { "max", 1, 16, bi_max, "n*>S" },
    { "clamp", 3, 3, bi_clamp, "nnn>S" }, { "lerp", 3, 3, bi_lerp, "xxn>S" }, { "random", 0, 2, bi_random, "nn>n" },
    { "vec2", 2, 2, bi_vec2, "nn>v" }, { "length", 1, 1, bi_length, "v>n" }, { "normalize", 1, 1, bi_normalize, "v>v" },
    { "dot", 2, 2, bi_dot, "vv>n" }, { "distance", 2, 2, bi_distance, "vv>n" }, { "angle", 1, 1, bi_angle, "v>n" },
    { "from_angle", 1, 2, bi_from_angle, "nn>v" }, { "rgb", 3, 4, bi_rgb, "nnnn>s" },
    { "len", 1, 1, bi_len, "t>i" }, { "push", 2, 2, bi_push, "la>S" }, { "pop", 1, 1, bi_pop, "l>E" },
    { "remove", 2, 2, bi_remove, "ln>E" }, { "contains", 2, 2, bi_contains, "la>b" }, { "range", 1, 3, bi_range, "nnn>R" },
    { "key_down", 1, 1, bi_key_down, "s>b" }, { "key_pressed", 1, 1, bi_key_pressed, "s>b" },
    { "key_released", 1, 1, bi_key_released, "s>b" },
    { "mouse_down", 0, 1, bi_mouse_down, "n>b" }, { "mouse_pressed", 0, 1, bi_mouse_pressed, "n>b" },
    { "mouse_position", 0, 0, bi_mouse_position, ">v" }, { "time", 0, 0, bi_time, ">n" },
    { "button_down", 1, 2, bi_button_down, "sn>b" }, { "button_pressed", 1, 2, bi_button_pressed, "sn>b" },
    { "button_released", 1, 2, bi_button_released, "sn>b" }, { "axis", 1, 2, bi_axis, "sn>n" },
    { "stick", 0, 2, bi_stick, "sn>v" }, { "gamepad_connected", 0, 1, bi_gamepad_connected, "n>b" },
    { "gamepad_count", 0, 0, bi_gamepad_count, ">n" }, { "gamepad_name", 0, 1, bi_gamepad_name, "n>s" },
    { "last_button", 0, 0, bi_last_button, ">s" }, { "last_input", 0, 0, bi_last_input, ">s" }, { "rumble", 2, 3, bi_rumble, "nnn>b" },
    { "find", 1, 1, bi_find, "s>e" }, { "find_all", 1, 1, bi_find_all, "s>L" }, { "destroy", 1, 1, bi_destroy, "e>-" },
    { "spawn", 1, 3, bi_spawn, "spq>e" }, { "children", 1, 1, bi_children, "e>L" }, { "child", 2, 2, bi_child, "es>e" },
    { "has", 2, 2, bi_has, "es>b" }, { "add", 2, 2, bi_add, "es>c" },
    { "remove_component", 2, 2, bi_remove_component, "es>-" },
    { "apply_force", 2, 3, bi_apply_force, "evv>-" }, { "apply_impulse", 2, 3, bi_apply_impulse, "evv>-" },
    { "raycast", 2, 3, bi_raycast, "vvq>a" }, { "entity_at", 1, 1, bi_entity_at, "v>e" },
    { "play_sound", 1, 2, bi_play_sound, "sn>-" }, { "send", 2, 16, bi_send, "esa*>a" },
    { "load_scene", 1, 1, bi_load_scene, "s>-" }, { "quit", 0, 0, bi_quit, ">-" },
    { "tile_at", 2, 2, bi_tile_at, "ev>s" }, { "set_tile", 3, 3, bi_set_tile, "evs>b" },
    { "tile_solid", 2, 2, bi_tile_solid, "ev>b" }, { "tile_center", 2, 2, bi_tile_center, "ev>v" },
    { "substr", 2, 3, bi_substr, "snn>s" },
    { "save_data", 2, 2, bi_save_data, "sa>b" }, { "load_data", 1, 2, bi_load_data, "sa>a" },
    { "has_data", 1, 1, bi_has_data, "s>b" }, { "delete_data", 1, 1, bi_delete_data, "s>b" },
    { "data_names", 0, 0, bi_data_names, ">W" },
    { "last_key", 0, 0, bi_last_key, ">s" }, { "set_volume", 2, 2, bi_set_volume, "sn>-" },
    { "volume", 1, 1, bi_volume, "s>n" },
};

static int sc_find_builtin(const char *name) {
    int i;
    for (i = 0; i < (int)BR_ARRAY_COUNT(k_builtin_table); ++i) if (strcmp(k_builtin_table[i].name, name) == 0) return i;
    return -1;
}
static const sc_builtin *sc_builtin_at(int index) { return &k_builtin_table[index]; }

int br_script_builtin_count(void) { return (int)BR_ARRAY_COUNT(k_builtin_table); }
const char *br_script_builtin_name(int index) {
    return index >= 0 && index < (int)BR_ARRAY_COUNT(k_builtin_table) ? k_builtin_table[index].name : NULL;
}

/* ======================================================================== */
/* 6. runtime                                                               */
/* ======================================================================== */

static const char k_runtime_key = 0;

static const br_field k_script_fields[] = {
    BR_FIELD(br_script, file, BR_FIELD_STRING, 0, 0),
    BR_FIELD(br_script, vars, BR_FIELD_STRING, 0, 0),
};
static const br_script k_script_default = { "", "", NULL };
static const br_component_desc k_script_desc = {
    "Script", sizeof(br_script), k_script_fields, (int)BR_ARRAY_COUNT(k_script_fields), &k_script_default
};
const br_component_desc *br_script_desc(void) { return &k_script_desc; }

void br_script_set_print(void (*fn)(const char *text, void *user), void *user) { g_print = fn; g_print_user = user; }

bool br_script_register_behaviour(const char *name, const br_behaviour *b) {
    int i;
    for (i = 0; i < g_behaviour_count; ++i)
        if (strcmp(g_behaviours[i].name, name) == 0) { g_behaviours[i].b = b; return true; }
    if (g_behaviour_count == SC_MAX_BEHAVIOURS) return false;
    snprintf(g_behaviours[g_behaviour_count].name, sizeof(g_behaviours[0].name), "%s", name);
    g_behaviours[g_behaviour_count++].b = b;
    return true;
}

static const br_behaviour *sc_find_behaviour(const char *name) {
    int i;
    for (i = 0; i < g_behaviour_count; ++i) if (strcmp(g_behaviours[i].name, name) == 0) return g_behaviours[i].b;
    return NULL;
}

static void sc_runtime_destroy(void *data) {
    sc_runtime *rt = (sc_runtime *)data;
    int i;
    for (i = 0; i < rt->program_count; ++i) sc_program_free(rt->programs[i]);
    br_free(rt->programs);
    br_free(rt->instances);              /* the instances themselves went with their entities */
    sc_heap_free(&rt->heap);
    br_free(rt);
}

static sc_runtime *sc_runtime_of(br_world *w) {
    sc_runtime *rt = (sc_runtime *)br_world_attached(w, &k_runtime_key);
    if (rt) return rt;
    if (!(rt = (sc_runtime *)br_alloc(sizeof(sc_runtime)))) return NULL;
    memset(rt, 0, sizeof(*rt));
    rt->w = w;
    rt->script_id = br_component_find(w, "Script");
    br_builtin_ids_of(w, &rt->ids);
    rt->body_id = br_component_find(w, "RigidBody");
    rt->collider_id = br_component_find(w, "Collider");
    br_rng_seed(&rt->rng, 0x5C21u);
    if (!br_world_attach(w, &k_runtime_key, rt, sc_runtime_destroy)) { br_free(rt); return NULL; }
    return rt;
}

/* The program for a file, read again when it changed on disk. */
static sc_program *sc_load(sc_runtime *rt, const char *file) {
    char full[BR_PATH_MAX * 2];
    br_dir_entry info;
    int i;
    int64_t modified = 0;
    char *source;
    size_t size;
    sc_program *prog;
    snprintf(full, sizeof(full), "%s%s", file[0] == '/' ? "" : br_assets_root(), file);
    if (br_fs_stat(full, &info)) modified = info.modified;
    for (i = 0; i < rt->program_count; ++i) {
        if (strcmp(rt->programs[i]->path, file) != 0) continue;
        if (rt->programs[i]->modified == modified) return rt->programs[i];
        break;                                         /* changed: replaced below once nothing runs it */
    }
    source = (char *)br_file_read_all(full, &size);
    if (!source) {
        prog = (sc_program *)br_alloc(sizeof(sc_program));
        if (!prog) return NULL;
        memset(prog, 0, sizeof(*prog));
        snprintf(prog->name, sizeof(prog->name), "%s", br_path_name(file));
        snprintf(prog->error, sizeof(prog->error), "can't read the script %s", file);
    } else {
        prog = sc_compile(source, file);
        br_free(source);
        if (!prog) return NULL;
    }
    snprintf(prog->path, sizeof(prog->path), "%s", file);
    prog->modified = modified;
    if (i < rt->program_count) {
        int k;
        bool used = false;                             /* the old version may still be running */
        for (k = 0; k < rt->instance_count; ++k) used |= rt->instances[k]->prog == rt->programs[i];
        if (!used) { sc_program_free(rt->programs[i]); rt->programs[i] = prog; return prog; }
        rt->programs[i]->path[0] = 0;                  /* keep it for those, but don't find it again */
    }
    if (rt->program_count == rt->program_cap) {
        int cap = rt->program_cap ? rt->program_cap * 2 : 16;
        sc_program **g = (sc_program **)br_realloc(rt->programs, sizeof(sc_program *) * (size_t)cap);
        if (!g) { sc_program_free(prog); return NULL; }
        rt->programs = g;
        rt->program_cap = cap;
    }
    rt->programs[rt->program_count++] = prog;
    return prog;
}

static void sc_instance_free(sc_runtime *rt, sc_instance *inst) {
    if (inst->slot >= 0 && inst->slot < rt->instance_count) {
        rt->instances[inst->slot] = rt->instances[--rt->instance_count];
        rt->instances[inst->slot]->slot = inst->slot;
    }
    br_free(inst->globals);
    br_free(inst->state);
    br_free(inst);
}

static sc_instance *sc_instance_create(sc_runtime *rt, br_entity e, const br_script *s) {
    sc_instance *inst = (sc_instance *)br_alloc(sizeof(sc_instance));
    if (!inst) return NULL;
    memset(inst, 0, sizeof(*inst));
    inst->rt = rt;
    inst->e = e;
    inst->slot = -1;
    if (rt->instance_count == rt->instance_cap) {
        int cap = rt->instance_cap ? rt->instance_cap * 2 : 64;
        sc_instance **g = (sc_instance **)br_realloc(rt->instances, sizeof(sc_instance *) * (size_t)cap);
        if (!g) { br_free(inst); return NULL; }
        rt->instances = g;
        rt->instance_cap = cap;
    }
    inst->slot = rt->instance_count;
    rt->instances[rt->instance_count++] = inst;
    if (!s->file[0]) { inst->failed = true; return inst; }    /* nothing to run yet */
    if ((inst->beh = sc_find_behaviour(s->file)) != NULL) {
        if (inst->beh->state_size && !(inst->state = br_alloc(inst->beh->state_size))) inst->failed = true;
        else if (inst->state) memset(inst->state, 0, inst->beh->state_size);
        return inst;
    }
    inst->prog = sc_load(rt, s->file);
    if (!inst->prog || !inst->prog->ok) {
        inst->failed = true;
        if (inst->prog) sc_report(rt, inst->prog->error);
        return inst;
    }
    if (inst->prog->global_count &&
        !(inst->globals = (sc_value *)br_alloc(sizeof(sc_value) * (size_t)inst->prog->global_count))) inst->failed = true;
    else if (inst->globals) memset(inst->globals, 0, sizeof(sc_value) * (size_t)inst->prog->global_count);
    return inst;
}

/* "speed = 120, name = \"Rex\"": literal starting values over the script's own. */
static void sc_apply_vars(sc_runtime *rt, sc_instance *inst, const char *vars) {
    sc_lexer L;
    sc_token t;
    char msg[300];
    L.p = vars;
    L.line = 1;
    L.depth = 0;
    for (;;) {
        sc_token name;
        sc_value v = sc_nil();
        int g;
        do { t = sc_next(&L); } while (t.t == T_COMMA || t.t == T_SEMI || t.t == T_NEWLINE);
        if (t.t == T_EOF) return;
        name = t;
        if (name.t != T_NAME || sc_next(&L).t != T_ASSIGN) break;
        if (!sc_read_value(rt, &L, sc_next(&L), &v, 0)) break;   /* a number, string, true/false, nil, vec2 or list */
        {
            char key[64];
            snprintf(key, sizeof(key), "%.*s", name.len, name.start);
            if ((g = sc_find_global(inst->prog, key)) < 0) {
                snprintf(msg, sizeof(msg), "%s: Script.vars sets '%s', but the script has no var by that name", inst->prog->name, key);
                sc_report(rt, msg);
            } else if (!sc_value_is(&v, inst->prog->globals[g].ty)) {
                char tn[32];
                snprintf(msg, sizeof(msg), "%s: Script.vars sets '%s', but it is %s %s", inst->prog->name, key,
                         inst->prog->globals[g].ty.t == TY_INT || inst->prog->globals[g].ty.t == TY_ENT ? "an" : "a",
                         sc_ty_name(inst->prog->globals[g].ty, tn, sizeof(tn)));
                sc_report(rt, msg);
            } else {
                inst->globals[g] = v;
            }
        }
    }
    snprintf(msg, sizeof(msg), "%s: can't read Script.vars \"%s\" (write it like: speed = 120, name = \"Rex\")", inst->prog->name, vars);
    sc_report(rt, msg);
}

/* Runs `fn` (or the variable initialisers, fn == -1) safely: an error stops this script only. */
static bool sc_run(sc_runtime *rt, sc_instance *inst, int fn, const sc_value *args, int argc, sc_value *result) {
    sc_vm vm;
    jmp_buf jb;
    volatile bool ok = true;
    memset(&vm, 0, sizeof(vm));
    vm.rt = rt;
    vm.inst = inst;
    vm.budget = SC_BUDGET;
    vm.fail = &jb;
    rt->busy++;
    if (setjmp(jb) == 0) {
        if (fn < 0) {
            int i;
            for (i = 0; i < inst->prog->global_count; ++i) {
                const sc_global *g = &inst->prog->globals[i];
                sc_value v = g->init ? sc_eval(&vm, g->init) : sc_default(&vm, g->ty, g->line);
                if (g->ty.t && g->init) sc_check_value(&vm, g->ty, &v, g->line, g->name);
                inst->globals[i] = v;
            }
        } else {
            sc_value r = sc_call_func(&vm, inst, fn, args, argc, inst->prog->funcs[fn].line);
            if (result) *result = r;
        }
    } else {
        ok = false;
    }
    rt->busy--;
    br_free(vm.stack);
    return ok;
}

static void sc_callback(sc_runtime *rt, sc_instance *inst, int cb, const sc_value *args, int argc) {
    if (inst->failed || !inst->prog || inst->prog->callbacks[cb] < 0) return;
    sc_run(rt, inst, inst->prog->callbacks[cb], args, argc, NULL);
}

/* Sets up the variables: the script's own starting values, then the
 * Script's vars. From then on other scripts can read, set and call it. */
static void sc_make_ready(sc_runtime *rt, sc_instance *inst, const br_script *s) {
    if (inst->ready) return;
    inst->ready = true;
    if (inst->failed || inst->beh) return;
    if (!sc_run(rt, inst, -1, NULL, 0, NULL)) return;
    if (s->vars[0]) sc_apply_vars(rt, inst, s->vars);
}

static void sc_start(sc_runtime *rt, sc_instance *inst, const br_script *s) {
    sc_make_ready(rt, inst, s);
    inst->started = true;
    if (inst->failed) return;
    if (inst->beh) {
        if (inst->beh->start) inst->beh->start(rt->w, inst->e, inst->state);
        return;
    }
    sc_callback(rt, inst, CB_START, NULL, 0);
}

static void sc_collect(sc_runtime *rt) {
    int i, k;
    if (rt->busy || rt->heap.since_gc < SC_GC_BYTES) return;
    for (i = 0; i < rt->instance_count; ++i) {
        const sc_instance *inst = rt->instances[i];
        if (inst->prog && inst->globals)
            for (k = 0; k < inst->prog->global_count; ++k) sc_mark_value(&inst->globals[k]);
    }
    sc_sweep(&rt->heap);
}

static void sc_script_added(br_world *w, br_entity e, void *data, void *user) {
    BR_UNUSED(w); BR_UNUSED(e); BR_UNUSED(user);
    ((br_script *)data)->instance = NULL;              /* a clone or a loaded copy starts fresh */
}

static void sc_script_removed(br_world *w, br_entity e, void *data, void *user) {
    br_script *s = (br_script *)data;
    sc_instance *inst = (sc_instance *)s->instance;
    sc_runtime *rt = (sc_runtime *)br_world_attached(w, &k_runtime_key);
    BR_UNUSED(user);
    if (!inst) return;
    if (inst->beh && inst->beh->on_destroy && inst->started && !inst->failed) inst->beh->on_destroy(w, e, inst->state);
    if (rt) sc_instance_free(rt, inst);
    s->instance = NULL;
}

void br_script_step(br_world *w, float dt) {
    sc_runtime *rt;
    uint32_t i, count;
    sc_value arg;
    if (!w->context || !(rt = sc_runtime_of(w)) || rt->script_id == BR_COMPONENT_INVALID) return;
    count = w->pools[rt->script_id].count;              /* scripts spawned now start next step */
    arg = sc_num(dt);
    for (i = 0; i < count && i < w->pools[rt->script_id].count; ++i) {
        br_component_pool *p = &w->pools[rt->script_id];
        br_entity e = p->entities[i];
        br_script *s = (br_script *)(p->data + (size_t)i * p->stride);
        sc_instance *inst = (sc_instance *)s->instance;
        if (!inst) {
            if (!(inst = sc_instance_create(rt, e, s))) continue;
            s->instance = inst;
        }
        if (!inst->started) sc_start(rt, inst, s);
        if (inst->failed) continue;
        if (inst->beh) { if (inst->beh->update) inst->beh->update(w, e, inst->state, dt); }
        else sc_callback(rt, inst, CB_UPDATE, &arg, 1);
    }
    sc_collect(rt);
}

static void sc_deliver(sc_runtime *rt, br_entity self, br_entity other, const br_contact_event *ev, bool flip) {
    sc_instance *inst = sc_instance_of(rt, self);
    sc_value args[3];
    if (!inst || !inst->started || inst->failed) return;
    if (inst->beh) {
        if ((ev->kind == BR_CONTACT_BEGIN || ev->kind == BR_CONTACT_END) && inst->beh->on_collision)
            inst->beh->on_collision(rt->w, self, inst->state, other, ev->kind == BR_CONTACT_BEGIN);
        if ((ev->kind == BR_TRIGGER_ENTER || ev->kind == BR_TRIGGER_EXIT) && inst->beh->on_trigger)
            inst->beh->on_trigger(rt->w, self, inst->state, other, ev->kind == BR_TRIGGER_ENTER);
        return;
    }
    args[0] = sc_ent(other);
    args[1] = flip ? sc_vec(-ev->normal.x, -ev->normal.y) : sc_vec(ev->normal.x, ev->normal.y);
    args[2] = sc_num(ev->speed);
    switch (ev->kind) {
    case BR_CONTACT_BEGIN: sc_callback(rt, inst, CB_COLLISION, args, 3); break;
    case BR_CONTACT_END: sc_callback(rt, inst, CB_COLLISION_END, args, 1); break;
    case BR_TRIGGER_ENTER: sc_callback(rt, inst, CB_TRIGGER, args, 1); break;
    default: sc_callback(rt, inst, CB_TRIGGER_EXIT, args, 1); break;
    }
}

void br_script_deliver_contacts(br_world *w) {
    sc_runtime *rt;
    const br_contact_event *ev;
    br_contact_event copy[256];
    uint32_t n, i, k;
    if (!w->context || !(rt = (sc_runtime *)br_world_attached(w, &k_runtime_key))) return;
    ev = br_physics_events(w, &n);
    for (i = 0; i < n; i += 256) {                      /* a copy: scripts may cause more physics calls */
        uint32_t chunk = n - i < 256 ? n - i : 256;
        memcpy(copy, ev + i, sizeof(br_contact_event) * chunk);
        for (k = 0; k < chunk; ++k) {
            sc_deliver(rt, copy[k].a, copy[k].b, &copy[k], false);
            sc_deliver(rt, copy[k].b, copy[k].a, &copy[k], true);
        }
        ev = br_physics_events(w, &n);
    }
    sc_collect(rt);
}

static void sc_sys_scripts(br_world *w, float dt, void *user) { BR_UNUSED(user); br_script_step(w, dt); }
static void sc_sys_events(br_world *w, float dt, void *user) { BR_UNUSED(dt); BR_UNUSED(user); br_script_deliver_contacts(w); }

void br_script_register(br_world *w) {
    br_component c = br_component_register_desc(w, &k_script_desc);
    br_component_set_hooks(w, c, sc_script_added, sc_script_removed, NULL);
    br_system_add(w, "builtin_scripts", BR_PHASE_FIXED_UPDATE, sc_sys_scripts, NULL, -10);
    br_system_add(w, "builtin_script_events", BR_PHASE_FIXED_UPDATE, sc_sys_events, NULL, 30);
}

/* ---- the C API ------------------------------------------------------------------------ */

static sc_instance *sc_running(br_world *w, br_entity e) {
    sc_runtime *rt = (sc_runtime *)br_world_attached(w, &k_runtime_key);
    sc_instance *inst = rt ? sc_instance_of(rt, e) : NULL;
    return inst && inst->prog && inst->ready && !inst->failed ? inst : NULL;
}

bool br_script_call(br_world *w, br_entity e, const char *function, const double *args, int arg_count) {
    sc_instance *inst = sc_running(w, e);
    sc_value v[16];
    int fn, i;
    if (!inst || (fn = sc_find_func(inst->prog, function)) < 0 || arg_count > 16) return false;
    for (i = 0; i < arg_count; ++i) v[i] = sc_num(args[i]);
    return sc_run(inst->rt, inst, fn, v, arg_count, NULL);
}

bool br_script_get_number(br_world *w, br_entity e, const char *var, double *out) {
    sc_instance *inst = sc_running(w, e);
    int g = inst ? sc_find_global(inst->prog, var) : -1;
    if (g < 0) return false;
    if (inst->globals[g].t == V_NUM) *out = inst->globals[g].as.n;
    else if (inst->globals[g].t == V_BOOL) *out = inst->globals[g].as.b;
    else return false;
    return true;
}

bool br_script_set_number(br_world *w, br_entity e, const char *var, double value) {
    sc_instance *inst = sc_running(w, e);
    int g = inst ? sc_find_global(inst->prog, var) : -1;
    sc_value v = sc_num(value);
    if (g < 0 || !sc_value_is(&v, inst->prog->globals[g].ty)) return false;   /* (a typed var keeps its type) */
    inst->globals[g] = v;
    return true;
}

void br_script_set_strict(bool strict) { g_strict_default = strict; }
bool br_script_strict(void) { return g_strict_default; }

bool br_script_get_string(br_world *w, br_entity e, const char *var, char *out, size_t size) {
    sc_instance *inst = sc_running(w, e);
    int g = inst ? sc_find_global(inst->prog, var) : -1;
    char buf[1024];
    sc_vm vm;
    if (g < 0 || size == 0) return false;
    if (inst->globals[g].t == V_STR) { snprintf(out, size, "%s", inst->globals[g].as.s->chars); return true; }
    memset(&vm, 0, sizeof(vm));
    vm.rt = inst->rt;
    buf[0] = 0;
    sc_append(&vm, buf, sizeof(buf), &inst->globals[g], 0);    /* never fails: no allocation */
    snprintf(out, size, "%s", buf);
    return true;
}

bool br_script_check(const char *source, const char *name, char *err, size_t err_size) {
    sc_program *p = sc_compile(source, name ? name : "script");
    bool ok = p && p->ok;
    if (err && err_size) snprintf(err, err_size, "%s", p ? p->error : "out of memory");
    sc_program_free(p);
    return ok;
}

const char *br_script_last_error(const br_world *w) {
    const sc_runtime *rt = (const sc_runtime *)br_world_attached(w, &k_runtime_key);
    return rt ? rt->last_error : "";
}

bool br_script_scene_request(br_world *w, char *path, size_t size) {
    sc_runtime *rt = (sc_runtime *)br_world_attached(w, &k_runtime_key);
    if (!rt || !rt->scene_request[0]) return false;
    snprintf(path, size, "%s", rt->scene_request);
    rt->scene_request[0] = 0;
    return true;
}

bool br_script_quit_request(br_world *w) {
    sc_runtime *rt = (sc_runtime *)br_world_attached(w, &k_runtime_key);
    bool q = rt && rt->quit_request;
    if (rt) rt->quit_request = false;
    return q;
}

uint32_t br_script_error_count(const br_world *w) {
    const sc_runtime *rt = (const sc_runtime *)br_world_attached(w, &k_runtime_key);
    return rt ? rt->error_count : 0;
}

void br_script_reload(br_world *w) {
    sc_runtime *rt = (sc_runtime *)br_world_attached(w, &k_runtime_key);
    int i;
    if (!rt) return;
    for (i = 0; i < rt->program_count; ++i) rt->programs[i]->modified = -1;   /* stale: read again when next needed */
}
