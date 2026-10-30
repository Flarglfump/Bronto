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
#include "bronto/components3d.h"
#include "bronto/physics3d.h"
#include "bronto/particles.h"
#include "bronto/model.h"
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
#include <stddef.h>
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

typedef enum { V_NIL = 0, V_BOOL, V_NUM, V_STR, V_VEC, V_ENT, V_LIST, V_COMP, V_OBJ, V_VEC3 } sc_type;
enum { OBJ_STR = 1, OBJ_LIST, OBJ_OBJECT };

typedef struct sc_obj { struct sc_obj *next; uint8_t kind, marked, pinned; } sc_obj;
typedef struct sc_str { sc_obj h; uint32_t len; char chars[1]; } sc_str;
struct sc_value;
typedef struct sc_list { sc_obj h; struct sc_value *items; uint32_t count, cap; } sc_list;
struct sc_class;
/* a struct's or a class's object: its fields, inherited ones first */
typedef struct sc_object { sc_obj h; struct sc_class *cls; uint32_t count; struct sc_value *fields; } sc_object;

typedef struct sc_value {
    uint8_t t;
    union {
        bool b;
        double n;
        sc_str *s;
        struct { double x, y, z; } v;          /* vec2 (z unused) and vec3 */
        br_entity e;
        sc_list *l;
        sc_object *o;
        struct { br_entity e; uint32_t c; } comp;
    } as;
} sc_value;

typedef struct sc_heap { sc_obj *objects; size_t bytes, since_gc; } sc_heap;

static sc_value sc_nil(void) { sc_value v; memset(&v, 0, sizeof(v)); return v; }
static sc_value sc_bool(bool b) { sc_value v = sc_nil(); v.t = V_BOOL; v.as.b = b; return v; }
static sc_value sc_num(double n) { sc_value v = sc_nil(); v.t = V_NUM; v.as.n = n; return v; }
static sc_value sc_vec(double x, double y) { sc_value v = sc_nil(); v.t = V_VEC; v.as.v.x = x; v.as.v.y = y; return v; }
static sc_value sc_vec3(double x, double y, double z) { sc_value v = sc_nil(); v.t = V_VEC3; v.as.v.x = x; v.as.v.y = y; v.as.v.z = z; return v; }
static sc_value sc_v3val(br_vec3 p) { return sc_vec3(p.x, p.y, p.z); }
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

static sc_object *sc_object_alloc(sc_heap *h, struct sc_class *cls, uint32_t count) {
    sc_object *o = (sc_object *)br_alloc(sizeof(sc_object) + sizeof(struct sc_value) * (count ? count : 1));
    if (!o) return NULL;
    memset(o, 0, sizeof(sc_object) + sizeof(struct sc_value) * (count ? count : 1));
    o->h.kind = OBJ_OBJECT;
    o->cls = cls;
    o->count = count;
    o->fields = (struct sc_value *)(o + 1);
    o->h.next = h->objects;
    h->objects = &o->h;
    h->bytes += sizeof(sc_object) + sizeof(struct sc_value) * count;
    h->since_gc += sizeof(sc_object) + sizeof(struct sc_value) * count;
    return o;
}

static void sc_obj_free(sc_obj *o) {
    if (o->kind == OBJ_LIST) br_free(((sc_list *)o)->items);
    br_free(o);
}

static size_t sc_obj_size(const sc_obj *o) {
    if (o->kind == OBJ_STR) return sizeof(sc_str) + ((const sc_str *)o)->len;
    if (o->kind == OBJ_OBJECT) return sizeof(sc_object) + sizeof(struct sc_value) * ((const sc_object *)o)->count;
    return sizeof(sc_list);
}

static void sc_mark_value(const sc_value *v);
static void sc_mark_obj(sc_obj *o) {
    uint32_t i;
    if (!o || o->marked || o->pinned) return;
    o->marked = 1;
    if (o->kind == OBJ_LIST) {
        sc_list *l = (sc_list *)o;
        for (i = 0; i < l->count; ++i) sc_mark_value(&l->items[i]);
    } else if (o->kind == OBJ_OBJECT) {
        sc_object *ob = (sc_object *)o;
        for (i = 0; i < ob->count; ++i) sc_mark_value(&ob->fields[i]);
    }
}
static void sc_mark_value(const sc_value *v) {
    if (v->t == V_STR) sc_mark_obj(&v->as.s->h);
    else if (v->t == V_LIST) sc_mark_obj(&v->as.l->h);
    else if (v->t == V_OBJ) sc_mark_obj(&v->as.o->h);
}

static void sc_sweep(sc_heap *h) {
    sc_obj **p = &h->objects;
    while (*p) {
        sc_obj *o = *p;
        if (!o->marked) {
            *p = o->next;
            h->bytes -= sc_obj_size(o);
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
    T_CLASS, T_STRUCT, T_PUBLIC, T_PRIVATE, T_PROTECTED, T_STATIC, T_CONST, T_OVERRIDE, T_THIS, T_SUPER, T_UNSIGNED,
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
    { "false", T_FALSE }, { "nil", T_NIL }, { "class", T_CLASS }, { "struct", T_STRUCT }, { "public", T_PUBLIC },
    { "private", T_PRIVATE }, { "protected", T_PROTECTED }, { "static", T_STATIC }, { "const", T_CONST },
    { "override", T_OVERRIDE }, { "this", T_THIS }, { "super", T_SUPER }, { "unsigned", T_UNSIGNED },
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
    N_BLOCK, N_VAR, N_ASSIGN, N_EXPR, N_IF, N_WHILE, N_FOR, N_RETURN, N_BREAK, N_CONTINUE,
    N_THIS, N_SUPER, N_CLASSREF        /* this; super.f(...); a class's name before . */
} sc_kind;

/* what a name refers to, found by the resolver */
enum { REF_UNRESOLVED = 0, REF_LOCAL, REF_GLOBAL, REF_FUNC, REF_BUILTIN, REF_CLASS };

/* Static types. TY_NONE: not known until it runs (no annotation). */
typedef enum {
    TY_NONE = 0, TY_ANY, TY_NIL, TY_BOOL, TY_INT, TY_NUM, TY_STR, TY_VEC, TY_LIST, TY_ENT, TY_COMP, TY_VOID, TY_OBJ, TY_VEC3
} sc_tyk;
#define TY_LAST TY_VEC3
static bool sc_vecty(int t) { return t == TY_VEC || t == TY_VEC3; }
enum {
    TYF_UNSIGNED = 1,              /* an unsigned int / number: never below 0 */
    TYF_ELEM_UNSIGNED = 2,         /* a list of those */
    TYF_NEGATIVE = 4               /* (the checker: a negative number written in the source) */
};
struct sc_class;
/* elem: a list's items (TY_NONE: any). cls: the struct or class, for TY_OBJ or a list of them */
typedef struct sc_ty { uint8_t t, elem, flags; struct sc_class *cls; } sc_ty;

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
    struct sc_class *cls;          /* N_CLASSREF, a REF_CLASS call: the class */
    struct sc_member *mem;         /* N_SUPER: the parent's function */
} sc_node;

typedef struct sc_dbg_local { const char *name; int slot, from, to; } sc_dbg_local;   /* (the debugger's) */

typedef struct sc_func {
    const char *name;
    const sc_dbg_local *dbg;       /* its locals' names: which slot, from which line to which */
    int dbg_count;
    int params, locals, line;
    sc_node *body;
    sc_ty *param_ty;               /* per parameter (arena) */
    const char **param_names;
    sc_ty ret;                     /* TY_NONE: not declared, TY_VOID: returns nothing */
    struct sc_class *cls;          /* a method's class (NULL: a plain function) */
    bool has_this;                 /* a method that isn't static: `this` is local slot 0 */
    bool is_init;                  /* a class's init(): may set its const fields */
} sc_func;
typedef struct sc_global { const char *name; sc_node *init; int line; sc_ty ty; bool infer, is_const; } sc_global;

/* ---- structs and classes ----
 * A class's members are its own fields and functions; inherited ones are
 * found through `parent`. Instance fields have slots in the object (the
 * parent's first); static fields have slots in the class's `statics`,
 * which live as long as the program (one set per world). */
enum { ACC_PUBLIC = 0, ACC_PROTECTED, ACC_PRIVATE };
typedef struct sc_member {
    const char *name;
    int line;
    uint8_t access;
    bool is_static, is_const, is_override, infer;
    sc_func *fn;                   /* a function; NULL for a field */
    sc_ty ty;                      /* a field's type */
    sc_node *init;                 /* a field's starting value */
    int slot;
    struct sc_class *owner;
} sc_member;

typedef struct sc_class {
    const char *name;
    int line;
    bool is_struct, defined, laid_out, visiting;
    struct sc_class *parent;
    const char *parent_name;
    int parent_line;
    struct sc_program *prog;
    sc_member *members;            /* (arena) */
    int member_count, member_cap;
    int field_count;               /* instance fields, inherited ones included */
    int static_count;
    struct sc_value *statics;      /* (arena) */
} sc_class;

typedef struct sc_chunk { struct sc_chunk *next; size_t used, size; } sc_chunk;

enum { CB_START, CB_UPDATE, CB_COLLISION, CB_COLLISION_END, CB_TRIGGER, CB_TRIGGER_EXIT, CB_DRAW, CB_PAUSE, CB_RESUME, CB_COUNT };
static const char *const k_callbacks[CB_COUNT] = {
    "start", "update", "on_collision", "on_collision_end", "on_trigger", "on_trigger_exit", "draw", "on_pause", "on_resume"
};

typedef struct sc_program {
    char path[BR_PATH_MAX];
    sc_class **classes;            /* structs and classes (in the arena), in the order they're written */
    int class_count, class_cap;
    bool statics_ready;            /* static fields have their starting values */
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
    bool three_d;                  /* @3d: entities' position, rotation, scale, velocity... are vec3s */
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
    sc_dbg_local dbg[512];         /* the debugger's names of the locals (sc_func.dbg) */
    int dbg_count;
    /* the type checker: the function being checked and its slots' types */
    const sc_func *fn;
    sc_ty slot_ty[256];
    bool local_const[256];         /* by slot: declared with const */
    sc_class *cls;                 /* inside a struct or class (its functions and fields) */
    bool has_this;                 /* inside a function that has `this` */
    bool in_func;                  /* inside a function's body (not a field's starting value) */
    int decls;                     /* top-level declarations so far */
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
static int sc_find_builtin(const char *name);              /* the table is in part 5 */

#if defined(__GNUC__) || defined(__clang__)
#  define SC_NOINLINE __attribute__((noinline))       /* (kept out of sc_compile's setjmp frame) */
#else
#  define SC_NOINLINE
#endif

/* ---- types --------------------------------------------------------------------- */

static sc_ty sc_ty_make(int t, int elem) { sc_ty ty; ty.t = (uint8_t)t; ty.elem = (uint8_t)elem; ty.flags = 0; ty.cls = NULL; return ty; }
static sc_ty sc_obj_ty(sc_class *c) { sc_ty ty = sc_ty_make(TY_OBJ, TY_NONE); ty.cls = c; return ty; }

/* A list's item type, and the list type for items of a type. */
static sc_ty sc_elem_ty(sc_ty list) {
    sc_ty e = sc_ty_make(list.elem, TY_NONE);
    if (list.flags & TYF_ELEM_UNSIGNED) e.flags |= TYF_UNSIGNED;
    if (list.elem == TY_OBJ) e.cls = list.cls;
    return e;
}
static sc_ty sc_list_of(sc_ty item) {
    sc_ty l = sc_ty_make(TY_LIST, item.t == TY_ANY ? TY_NONE : item.t);
    if (item.flags & TYF_UNSIGNED) l.flags |= TYF_ELEM_UNSIGNED;
    if (item.t == TY_OBJ) l.cls = item.cls;
    return l;
}
/* What a := takes from a value's type: lists any items, no source-only marks. */
static sc_ty sc_ty_plain(sc_ty ty) { ty.flags &= (uint8_t)~TYF_NEGATIVE; return ty; }

/* Is `c` the class `base` or one of its descendants? */
static bool sc_derives(const sc_class *c, const sc_class *base) {
    for (; c; c = c->parent) if (c == base) return true;
    return false;
}

static const char *sc_tyk_name(int t) {
    static const char *const names[] = { "untyped", "any", "nil", "bool", "int", "number", "string", "vec2", "list",
                                         "entity", "component", "void", "object", "vec3" };
    return t >= 0 && t <= TY_LAST ? names[t] : "?";
}

static const char *sc_ty_name(sc_ty ty, char *buf, size_t size) {
    if (ty.t == TY_OBJ && ty.cls) snprintf(buf, size, "%s", ty.cls->name);
    else if (ty.t == TY_LIST && ty.elem == TY_OBJ && ty.cls) snprintf(buf, size, "list[%s]", ty.cls->name);
    else if (ty.t == TY_LIST && ty.elem != TY_NONE)
        snprintf(buf, size, "list[%s%s]", ty.flags & TYF_ELEM_UNSIGNED ? "unsigned " : "", sc_tyk_name(ty.elem));
    else snprintf(buf, size, "%s%s", ty.flags & TYF_UNSIGNED ? "unsigned " : "", sc_tyk_name(ty.t));
    return buf;
}

static int sc_type_word(const char *s, int len) {
    static const struct { const char *word; int t; } words[] = {
        { "number", TY_NUM }, { "float", TY_NUM }, { "int", TY_INT }, { "string", TY_STR }, { "bool", TY_BOOL },
        { "vec2", TY_VEC }, { "list", TY_LIST }, { "entity", TY_ENT }, { "any", TY_ANY }, { "void", TY_VOID },
        { "uint", TY_INT }, { "vec3", TY_VEC3 },
    };
    size_t i;
    for (i = 0; i < BR_ARRAY_COUNT(words); ++i)
        if ((int)strlen(words[i].word) == len && strncmp(words[i].word, s, (size_t)len) == 0) return words[i].t;
    return -1;
}

static sc_class *sc_find_class_n(const sc_program *p, const char *name, int len) {
    int i;
    for (i = 0; i < p->class_count; ++i)
        if ((int)strlen(p->classes[i]->name) == len && strncmp(p->classes[i]->name, name, (size_t)len) == 0) return p->classes[i];
    return NULL;
}
static sc_class *sc_find_class(const sc_program *p, const char *name) { return sc_find_class_n(p, name, (int)strlen(name)); }

/* One type without list[...]: [unsigned] a type word, or a struct's or class's name. */
static sc_ty sc_parse_one_type(sc_parser *P, bool allow_void, const char *what) {
    sc_token t = P->cur;
    bool uns = false;
    int k = TY_NONE;
    sc_ty ty;
    sc_class *c;
    if (t.t == T_UNSIGNED) {                                 /* unsigned int, unsigned number */
        uns = true;
        sc_advance(P);
        t = P->cur;
        if (t.t != T_NAME || ((k = sc_type_word(t.start, t.len)) != TY_INT && k != TY_NUM))
            sc_parse_error(P, t.line, "unsigned is only for number types: unsigned int, unsigned number (or float)");
    }
    if (t.t == T_NAME && (c = sc_find_class_n(P->prog, t.start, t.len)) != NULL) {
        sc_advance(P);
        return sc_obj_ty(c);
    }
    if (t.t != T_NAME || (k = sc_type_word(t.start, t.len)) < 0) {
        if (t.t == T_NAME || t.t == T_NIL)
            sc_parse_error(P, t.line, "unknown type '%.*s' (the types: number, int, uint, string, bool, vec2, vec3, list, entity, any, or a struct or class)", t.len, t.start);
        sc_parse_error(P, t.line, "expected %s (number, int, string, bool, vec2, vec3, list, entity, any, or a struct or class)", what);
    }
    if (k == TY_VOID && !allow_void) sc_parse_error(P, t.line, "void is only for what a function returns (-> void)");
    if (t.len == 4 && strncmp(t.start, "uint", 4) == 0) {
        if (uns) sc_parse_error(P, t.line, "uint is already unsigned: write uint or unsigned int");
        uns = true;
    }
    sc_advance(P);
    ty = sc_ty_make(k, TY_NONE);
    if (uns) ty.flags |= TYF_UNSIGNED;
    return ty;
}

/* After ':' or '->': a type name, or list[T]. */
static sc_ty sc_parse_type(sc_parser *P, bool allow_void) {
    int line = P->cur.line;
    sc_ty ty = sc_parse_one_type(P, allow_void, "a type");
    if (ty.t == TY_LIST && P->cur.t == T_LBRACKET) {           /* list[number] */
        sc_ty e;
        sc_advance(P);
        if (P->cur.t != T_NAME && P->cur.t != T_UNSIGNED) sc_parse_error(P, P->cur.line, "expected the type of the list's items, like list[number]");
        e = sc_parse_one_type(P, false, "the type of the list's items, like list[number]");
        if (e.t == TY_LIST && P->cur.t == T_LBRACKET) sc_parse_error(P, line, "a list's items can be lists, but not typed lists (use list[list])");
        sc_expect(P, T_RBRACKET);
        ty = sc_list_of(e);
    }
    return ty;
}

static int sc_find_local(const sc_parser *P, const char *name, int len) {
    int i;
    for (i = P->local_count - 1; i >= 0; --i)
        if ((int)strlen(P->locals[i]) == len && strncmp(P->locals[i], name, (size_t)len) == 0) return i;
    return -1;
}

/* A scope ends: its locals' names stop at this line (for the debugger). */
static void sc_close_locals(sc_parser *P, int saved) {
    int i;
    for (i = 0; i < P->dbg_count; ++i)
        if (P->dbg[i].slot >= saved && P->dbg[i].to == (1 << 30)) P->dbg[i].to = P->prev.line;
}

static int sc_add_local(sc_parser *P, const char *name, int line) {
    if (P->local_count == 256) sc_parse_error(P, line, "too many local variables");
    P->locals[P->local_count] = name;
    if (P->local_count + 1 > P->max_locals) P->max_locals = P->local_count + 1;
    if (P->dbg_count < 512 && (name[0] != ' ' || strcmp(name, " this") == 0)) {
        sc_dbg_local *d = &P->dbg[P->dbg_count++];
        d->name = name[0] == ' ' ? name + 1 : name;
        d->slot = P->local_count;
        d->from = line;
        d->to = 1 << 30;
    }
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
    case T_THIS:
        sc_advance(P);
        if (!P->has_this) {
            if (P->cls && !P->in_func)
                sc_parse_error(P, t.line, "a field's starting value can't use this (set it in init())");
            sc_parse_error(P, t.line, P->cls ? "a static function has no this" : "this is only inside a struct's or class's functions");
        }
        n = sc_node_new(P, N_THIS, t.line);
        n->index = 0;
        return n;
    case T_SUPER: {                                    /* super.speak(): the parent's version */
        sc_token name;
        sc_advance(P);
        if (!P->has_this) sc_parse_error(P, t.line, "super is only inside a class's functions (not static ones)");
        sc_expect(P, T_DOT);
        name = P->cur;
        sc_expect(P, T_NAME);
        if (P->cur.t != T_LPAREN) sc_parse_error(P, name.line, "super is for calling the parent's functions, like super.%.*s()", name.len, name.start);
        sc_advance(P);
        n = sc_node_new(P, N_SUPER, t.line);
        n->name = sc_intern(P, name.start, name.len);
        return sc_call_args(P, n);
    }
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
        if (t.len == 4 && strncmp(t.start, "self", 4) == 0) {
            if (P->cls) sc_parse_error(P, t.line, "self (the script's entity) isn't seen inside a struct or class: pass the entity in");
            return sc_node_new(P, N_SELF, t.line);
        }
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
    case T_CONST:
    case T_VAR: {
        sc_token name;
        bool is_const = t.t == T_CONST;
        sc_advance(P);
        if (is_const && P->cur.t == T_VAR) sc_advance(P);      /* const var x = ... reads the same */
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
            sc_parse_error(P, name.line, "@strict: give '%.*s' a type (%s %.*s: number = 0, or %s %.*s := 0)",
                           name.len, name.start, is_const ? "const" : "var", name.len, name.start, is_const ? "const" : "var", name.len, name.start);
        if (is_const && !n->a) sc_parse_error(P, name.line, "a const needs its value: const %.*s = ...", name.len, name.start);
        n->index = sc_add_local(P, sc_intern(P, name.start, name.len), name.line);
        n->name = P->locals[n->index];
        P->local_const[n->index] = is_const;
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
        P->local_const[n->index] = false;
        n->c = sc_node_new(P, N_NIL, t.line);            /* a hidden slot for the loop position */
        n->c->index = sc_add_local(P, " for", t.line);
        P->local_const[n->c->index] = false;
        n->b = sc_block(P);
        sc_close_locals(P, saved);
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
    case T_FUNC: sc_parse_error(P, t.line, "functions can only be defined at the top of a file (or in a struct or class)"); break;
    case T_CLASS: case T_STRUCT: sc_parse_error(P, t.line, "structs and classes can only be defined at the top of a file"); break;
    case T_PUBLIC: case T_PRIVATE: case T_PROTECTED: case T_STATIC: case T_OVERRIDE:
        sc_parse_error(P, t.line, "'%.*s' is for the members of a struct or class", t.len, t.start);
        break;
    default: break;
    }
    {
        sc_node *e = sc_expression(P);
        sc_tok op = P->cur.t;
        if (op == T_ASSIGN || op == T_PLUS_EQ || op == T_MINUS_EQ || op == T_STAR_EQ || op == T_SLASH_EQ) {
            if (e->kind == N_THIS) sc_parse_error(P, P->cur.line, "can't assign to this (set its fields: this.x = ...)");
            if (e->kind != N_NAME && e->kind != N_MEMBER && e->kind != N_INDEX)
                sc_parse_error(P, P->cur.line, "can't assign to this (only to variables, properties and list items)");
            if (e->kind == N_NAME && e->ref == REF_LOCAL && P->local_const[e->index])
                sc_parse_error(P, e->line, "%s is const, so it can't be changed", e->name);
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
    sc_close_locals(P, saved);
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

/* From '(' to the end of the body. A method with `this` has it in slot 0. */
static void sc_func_rest(sc_parser *P, sc_func *f, bool has_this) {
    int line = f->line;
    P->local_count = P->max_locals = 0;
    P->dbg_count = 0;
    P->has_this = has_this;
    P->in_func = true;
    f->has_this = has_this;
    if (has_this) { sc_add_local(P, " this", line); P->local_const[0] = true; }
    sc_expect(P, T_LPAREN);
    {
        sc_ty types[64];
        const char *names[64];
        if (P->cur.t != T_RPAREN) {
            do {
                sc_token pn = P->cur;
                int slot;
                sc_expect(P, T_NAME);
                if (f->params == 64) sc_parse_error(P, pn.line, "too many parameters");
                names[f->params] = sc_intern(P, pn.start, pn.len);
                slot = sc_add_local(P, names[f->params], pn.line);
                P->local_const[slot] = false;
                types[f->params] = sc_ty_make(TY_NONE, TY_NONE);
                if (sc_accept(P, T_COLON)) types[f->params] = sc_parse_type(P, false);
                else if (P->prog->strict)
                    sc_parse_error(P, pn.line, "@strict: give the parameter '%.*s' a type (%.*s: number)", pn.len, pn.start, pn.len, pn.start);
                f->params++;
            } while (sc_accept(P, T_COMMA));
        }
        sc_expect(P, T_RPAREN);
        if (f->params) {
            f->param_ty = (sc_ty *)sc_arena_alloc(P->prog, sizeof(sc_ty) * (size_t)f->params);
            f->param_names = (const char **)sc_arena_alloc(P->prog, sizeof(char *) * (size_t)f->params);
            if (!f->param_ty || !f->param_names) sc_parse_error(P, line, "out of memory");
            memcpy(f->param_ty, types, sizeof(sc_ty) * (size_t)f->params);
            memcpy((void *)f->param_names, names, sizeof(char *) * (size_t)f->params);
        }
    }
    if (sc_accept(P, T_ARROW)) f->ret = sc_parse_type(P, true);
    else if (P->prog->strict)
        sc_parse_error(P, P->cur.line, "@strict: say what %s() returns (-> void if nothing)", f->name);
    f->body = sc_block(P);
    f->locals = P->max_locals;
    if (P->dbg_count) {                                    /* the locals' names, for the debugger */
        sc_dbg_local *d = (sc_dbg_local *)sc_arena_alloc(P->prog, sizeof(sc_dbg_local) * (size_t)P->dbg_count);
        if (d) { memcpy(d, P->dbg, sizeof(sc_dbg_local) * (size_t)P->dbg_count); f->dbg = d; f->dbg_count = P->dbg_count; }
    }
    P->has_this = false;
    P->in_func = false;
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
    sc_func_rest(P, &f, false);
    if (P->prog->func_count == P->prog->func_cap) {
        int cap = P->prog->func_cap ? P->prog->func_cap * 2 : 16;
        sc_func *g = (sc_func *)br_realloc(P->prog->funcs, sizeof(sc_func) * (size_t)cap);
        if (!g) sc_parse_error(P, line, "out of memory");
        P->prog->funcs = g;
        P->prog->func_cap = cap;
    }
    P->prog->funcs[P->prog->func_count++] = f;
}

/* var / const at the top: the script's own variables (one set per entity). */
static void sc_global_var(sc_parser *P) {
    sc_token name;
    sc_global g;
    int line = P->cur.line;
    bool is_const = P->cur.t == T_CONST;
    const char *word = is_const ? "const" : "var";
    sc_advance(P);
    if (is_const && P->cur.t == T_VAR) sc_advance(P);
    name = P->cur;
    sc_expect(P, T_NAME);
    memset(&g, 0, sizeof(g));
    g.name = sc_intern(P, name.start, name.len);
    g.line = line;
    g.is_const = is_const;
    if (sc_find_global(P->prog, g.name) >= 0) sc_parse_error(P, line, "there already is a variable '%s'", g.name);
    P->local_count = P->max_locals = 0;
    if (sc_accept(P, T_COLON)) g.ty = sc_parse_type(P, false);
    if (g.ty.t == TY_NONE && sc_accept(P, T_DECLARE)) { g.infer = true; g.init = sc_expression(P); }
    else if (sc_accept(P, T_ASSIGN)) g.init = sc_expression(P);
    else if (P->cur.t == T_DECLARE) sc_parse_error(P, P->cur.line, "':=' takes the value's type, so it can't follow a type: write %s %s: type = value", word, g.name);
    if (P->prog->strict && g.ty.t == TY_NONE && !g.infer)
        sc_parse_error(P, line, "@strict: give '%s' a type (%s %s: number = 0, or %s %s := 0)", g.name, word, g.name, word, g.name);
    if (is_const && !g.init) sc_parse_error(P, line, "a const needs its value: const %s = ...", g.name);
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

/* A class's own member by name (not inherited ones). */
static sc_member *sc_own_member(const sc_class *c, const char *name) {
    int i;
    for (i = 0; i < c->member_count; ++i) if (strcmp(c->members[i].name, name) == 0) return &c->members[i];
    return NULL;
}
/* A member by name, its own or inherited: the nearest one (so an override). */
static sc_member *sc_class_member(const sc_class *c, const char *name) {
    for (; c; c = c->parent) {
        sc_member *m = sc_own_member(c, name);
        if (m) return m;
    }
    return NULL;
}

static const char *sc_access_word(int a) { return a == ACC_PRIVATE ? "private" : a == ACC_PROTECTED ? "protected" : "public"; }

/* Can code in class `ctx` (NULL: outside every class) use member m? */
static bool sc_can_access(const sc_class *ctx, const sc_member *m) {
    if (m->access == ACC_PUBLIC) return true;
    if (m->access == ACC_PRIVATE) return ctx == m->owner;
    return ctx && sc_derives(ctx, m->owner);                  /* protected: the class and its children */
}

static sc_member *sc_new_member(sc_parser *P, sc_class *c, const char *name, int line) {
    if (sc_own_member(c, name)) sc_parse_error(P, line, "%s already has a member called '%s'", c->name, name);
    if (c->member_count == c->member_cap) {
        int cap = c->member_cap ? c->member_cap * 2 : 16;
        sc_member *g = (sc_member *)sc_arena_alloc(P->prog, sizeof(sc_member) * (size_t)cap);
        if (!g) sc_parse_error(P, line, "out of memory");
        if (c->member_count) memcpy(g, c->members, sizeof(sc_member) * (size_t)c->member_count);
        c->members = g;                                   /* (the old block stays in the arena) */
        c->member_cap = cap;
    }
    {
        sc_member *m = &c->members[c->member_count++];
        memset(m, 0, sizeof(*m));
        m->name = name;
        m->line = line;
        m->owner = c;
        m->slot = -1;
        return m;
    }
}

/* struct Name { ... } / class Name : Parent { ... } (the name was registered by sc_find_classes) */
static void sc_class_decl(sc_parser *P) {
    bool is_struct = P->cur.t == T_STRUCT;
    const char *kind = is_struct ? "struct" : "class";
    sc_token name;
    sc_class *c;
    int line = P->cur.line;
    sc_advance(P);
    name = P->cur;
    sc_expect(P, T_NAME);
    c = sc_find_class_n(P->prog, name.start, name.len);
    if (!c || c->defined) sc_parse_error(P, line, "there already is a struct or class '%.*s'", name.len, name.start);
    c->defined = true;
    c->line = line;
    if (sc_accept(P, T_COLON)) {                                 /* class Dog : Animal */
        sc_token pn = P->cur;
        if (is_struct) sc_parse_error(P, pn.line, "structs can't inherit (make %s a class to give it a parent)", c->name);
        sc_expect(P, T_NAME);
        c->parent_name = sc_intern(P, pn.start, pn.len);
        c->parent_line = pn.line;
    }
    P->cls = c;
    sc_skip_newlines(P);
    sc_expect(P, T_LBRACE);
    sc_skip_newlines(P);
    while (P->cur.t != T_RBRACE) {
        int access = -1, mline = P->cur.line;
        bool is_static = false, is_override = false;
        sc_token mn;
        sc_member *m;
        if (P->cur.t == T_EOF) sc_parse_error(P, line, "this %s's '{' is never closed", kind);
        for (;;) {                                               /* modifiers, in any order */
            sc_token t = P->cur;
            if (t.t == T_PUBLIC || t.t == T_PRIVATE || t.t == T_PROTECTED) {
                if (access >= 0) sc_parse_error(P, t.line, "a member is public, private or protected: only one of them");
                access = t.t == T_PUBLIC ? ACC_PUBLIC : t.t == T_PRIVATE ? ACC_PRIVATE : ACC_PROTECTED;
                if (access == ACC_PROTECTED && is_struct)
                    sc_parse_error(P, t.line, "structs have no children, so nothing in them is protected (use private)");
            } else if (t.t == T_STATIC) {
                if (is_static) sc_parse_error(P, t.line, "static twice");
                is_static = true;
            } else if (t.t == T_OVERRIDE) {
                if (is_override) sc_parse_error(P, t.line, "override twice");
                if (is_struct) sc_parse_error(P, t.line, "structs have no parent, so there is nothing to override");
                is_override = true;
            } else {
                break;
            }
            sc_advance(P);
        }
        if (P->cur.t == T_VAR || P->cur.t == T_CONST) {          /* a field */
            bool is_const = P->cur.t == T_CONST;
            const char *word = is_const ? "const" : "var";
            sc_advance(P);
            if (is_const && P->cur.t == T_VAR) sc_advance(P);
            mn = P->cur;
            sc_expect(P, T_NAME);
            if (is_override) sc_parse_error(P, mline, "override is for functions, not fields");
            m = sc_new_member(P, c, sc_intern(P, mn.start, mn.len), mline);
            m->access = (uint8_t)(access < 0 ? ACC_PUBLIC : access);
            m->is_static = is_static;
            m->is_const = is_const;
            P->local_count = P->max_locals = 0;
            if (sc_accept(P, T_COLON)) m->ty = sc_parse_type(P, false);
            if (m->ty.t == TY_NONE && sc_accept(P, T_DECLARE)) { m->infer = true; m->init = sc_expression(P); }
            else if (sc_accept(P, T_ASSIGN)) m->init = sc_expression(P);
            else if (P->cur.t == T_DECLARE) sc_parse_error(P, P->cur.line, "':=' takes the value's type, so it can't follow a type: write %s %s: type = value", word, m->name);
            if (P->prog->strict && m->ty.t == TY_NONE && !m->infer)
                sc_parse_error(P, mline, "@strict: give '%s' a type (%s %s: number = 0, or %s %s := 0)", m->name, word, m->name, word, m->name);
            if (is_const && is_static && !m->init) sc_parse_error(P, mline, "a static const needs its value: static const %s = ...", m->name);
            sc_end_statement(P);
        } else if (P->cur.t == T_FUNC) {                         /* a function (a method) */
            sc_func *f;
            sc_advance(P);
            mn = P->cur;
            sc_expect(P, T_NAME);
            m = sc_new_member(P, c, sc_intern(P, mn.start, mn.len), mline);
            m->access = (uint8_t)(access < 0 ? ACC_PUBLIC : access);
            m->is_static = is_static;
            m->is_override = is_override;
            if (!(f = (sc_func *)sc_arena_alloc(P->prog, sizeof(sc_func)))) sc_parse_error(P, mline, "out of memory");
            f->name = m->name;
            f->line = mline;
            f->cls = c;
            f->is_init = strcmp(m->name, "init") == 0;
            if (f->is_init && is_static) sc_parse_error(P, mline, "init() makes an object, so it can't be static");
            if (f->is_init && is_override) sc_parse_error(P, mline, "every %s has its own init(): no override needed", kind);
            if (is_static && is_override) sc_parse_error(P, mline, "static functions can't be overridden");
            m->fn = f;
            sc_func_rest(P, f, !is_static);
            if (f->is_init && f->ret.t != TY_NONE && f->ret.t != TY_VOID) sc_parse_error(P, mline, "init() returns nothing (-> void)");
            sc_skip_newlines(P);
        } else {
            if (P->cur.t == T_CLASS || P->cur.t == T_STRUCT) sc_parse_error(P, P->cur.line, "structs and classes can't be inside each other");
            sc_parse_error(P, P->cur.line, "expected var, const or func in %s %s, found '%.*s'", kind, c->name, P->cur.len, P->cur.start);
        }
        sc_skip_newlines(P);
    }
    sc_advance(P);
    P->cls = NULL;
    sc_end_statement(P);
}

/* Before parsing: every `struct Name` / `class Name`, so types can name a class written further down. */
static void sc_find_classes(sc_parser *P, const char *source) {
    sc_lexer L;
    sc_token t, prev;
    L.p = source;
    L.line = 1;
    L.depth = 0;
    prev.t = T_EOF;
    for (;;) {
        t = sc_next(&L);
        if (t.t == T_EOF || t.t == T_ERROR) return;               /* (the parser reports the error) */
        if (t.t == T_NAME && (prev.t == T_CLASS || prev.t == T_STRUCT)) {
            sc_class *c;
            if (sc_find_class_n(P->prog, t.start, t.len)) sc_parse_error(P, t.line, "there already is a struct or class '%.*s'", t.len, t.start);
            if (sc_type_word(t.start, t.len) >= 0) sc_parse_error(P, t.line, "'%.*s' is a type already", t.len, t.start);
            if (!(c = (sc_class *)sc_arena_alloc(P->prog, sizeof(sc_class)))) sc_parse_error(P, t.line, "out of memory");
            c->name = sc_intern(P, t.start, t.len);
            c->is_struct = prev.t == T_STRUCT;
            c->line = t.line;
            c->prog = P->prog;
            if (P->prog->class_count == P->prog->class_cap) {
                int cap = P->prog->class_cap ? P->prog->class_cap * 2 : 8;
                sc_class **g = (sc_class **)br_realloc(P->prog->classes, sizeof(sc_class *) * (size_t)cap);
                if (!g) sc_parse_error(P, t.line, "out of memory");
                P->prog->classes = g;
                P->prog->class_cap = cap;
            }
            P->prog->classes[P->prog->class_count++] = c;
        }
        prev = t;
    }
}

static bool sc_same_ty(sc_ty a, sc_ty b) {
    return a.t == b.t && a.elem == b.elem && (a.flags & ~TYF_NEGATIVE) == (b.flags & ~TYF_NEGATIVE) && a.cls == b.cls;
}

/* Field slots, parent first. */
static void sc_layout(sc_parser *P, sc_class *c) {
    int i;
    if (c->laid_out) return;
    if (c->visiting) sc_parse_error(P, c->line, "%s inherits from itself", c->name);
    c->visiting = true;
    if (c->parent) sc_layout(P, c->parent);
    c->field_count = c->parent ? c->parent->field_count : 0;
    for (i = 0; i < c->member_count; ++i) {
        sc_member *m = &c->members[i];
        if (m->fn) continue;
        if (m->is_static) m->slot = c->static_count++;
        else m->slot = c->field_count++;
    }
    if (c->static_count) {
        c->statics = (struct sc_value *)sc_arena_alloc(P->prog, sizeof(sc_value) * (size_t)c->static_count);
        if (!c->statics) sc_parse_error(P, c->line, "out of memory");
    }
    c->visiting = false;
    c->laid_out = true;
}

/* A struct holds its struct fields inside it, so it can't hold itself. */
static void sc_check_contains(sc_parser *P, sc_class *root, sc_class *c, int depth) {
    int i;
    if (depth > 64) return;
    for (i = 0; i < c->member_count; ++i) {
        const sc_member *m = &c->members[i];
        if (m->fn || m->is_static || m->ty.t != TY_OBJ || !m->ty.cls->is_struct) continue;
        if (m->ty.cls == root)
            sc_parse_error(P, m->line, "struct %s can't hold itself (through '%s'): use a class, a list, or a nil-able class field", root->name, m->name);
        sc_check_contains(P, root, m->ty.cls, depth + 1);
    }
}

/* After parsing: parents, field slots, and what overriding allows. */
static SC_NOINLINE void sc_link_classes(sc_parser *P) {
    sc_program *prog = P->prog;
    int i, k;
    for (i = 0; i < prog->class_count; ++i) {
        sc_class *c = prog->classes[i];
        if (!c->defined) sc_parse_error(P, c->line, "'%s' is never defined", c->name);
        if (sc_find_func(prog, c->name) >= 0) sc_parse_error(P, c->line, "there is a function called %s too", c->name);
        if (sc_find_global(prog, c->name) >= 0) sc_parse_error(P, c->line, "there is a variable called %s too", c->name);
        if (sc_find_builtin(c->name) >= 0) sc_parse_error(P, c->line, "%s() is a built-in function already", c->name);
        if (c->parent_name) {
            sc_class *p = sc_find_class(prog, c->parent_name);
            if (!p) sc_parse_error(P, c->parent_line, "unknown class '%s'", c->parent_name);
            if (p->is_struct) sc_parse_error(P, c->parent_line, "%s is a struct, and structs can't be inherited from", p->name);
            c->parent = p;
        }
    }
    for (i = 0; i < prog->class_count; ++i) sc_layout(P, prog->classes[i]);
    for (i = 0; i < prog->class_count; ++i) {
        sc_class *c = prog->classes[i];
        if (c->is_struct) sc_check_contains(P, c, c, 0);
        for (k = 0; k < c->member_count; ++k) {
            sc_member *m = &c->members[k];
            sc_member *base = c->parent ? sc_class_member(c->parent, m->name) : NULL;
            if (!base) {
                if (m->is_override) sc_parse_error(P, m->line, "%s() overrides nothing: %s's parents have no %s()", m->name, c->name, m->name);
                continue;
            }
            if (m->fn && m->fn->is_init && base->fn && base->fn->is_init) continue;      /* each class has its own init() */
            if (!m->fn || !base->fn)
                sc_parse_error(P, m->line, "%s already has '%s' from %s (a child can't declare it again)", c->name, m->name, base->owner->name);
            if (base->is_static || m->is_static)
                sc_parse_error(P, m->line, "%s() is static in %s, and static functions can't be overridden", m->name, base->is_static ? base->owner->name : c->name);
            if (!m->is_override)
                sc_parse_error(P, m->line, "%s() is already in %s: write override func %s() to replace it", m->name, base->owner->name, m->name);
            if (m->access > base->access)
                sc_parse_error(P, m->line, "%s() is %s in %s, so its override can't be %s", m->name, sc_access_word(base->access),
                               base->owner->name, sc_access_word(m->access));
            {
                const sc_func *f = m->fn, *b = base->fn;
                int a;
                bool same = f->params == b->params && sc_same_ty(f->ret, b->ret);
                for (a = 0; same && a < f->params; ++a) same = sc_same_ty(f->param_ty[a], b->param_ty[a]);
                if (!same) sc_parse_error(P, m->line, "an override takes the same parameters and returns the same type as %s.%s() (line %d)",
                                          base->owner->name, m->name, base->line);
            }
        }
    }
}

/* ---- the resolver: what each name refers to ------------------------------------ */

typedef struct sc_vm sc_vm;
typedef sc_value (*sc_native)(sc_vm *vm, sc_value *args, int argc, int line);
/* sig: the parameters' types, '>', the result's (see sc_sig_type), for the type checker */
typedef struct sc_builtin { const char *name; int min_args, max_args; sc_native fn; const char *sig; } sc_builtin;
static const sc_builtin *sc_builtin_at(int index);

static void sc_resolve(sc_parser *P, sc_node *n) {
    int i;
    if (!n) return;
    if (n->kind == N_NAME && n->ref == REF_UNRESOLVED) {
        if ((n->cls = sc_find_class(P->prog, n->name)) != NULL) { n->kind = N_CLASSREF; n->ref = REF_CLASS; }
        else if ((n->index = sc_find_global(P->prog, n->name)) >= 0) {
            if (P->cls) sc_parse_error(P, n->line, "a struct or class can't see the script's variables ('%s'): pass it in, or use a static field", n->name);
            n->ref = REF_GLOBAL;
        }
        else if (sc_find_func(P->prog, n->name) >= 0 || sc_find_builtin(n->name) >= 0)
            sc_parse_error(P, n->line, "'%s' is a function: call it with ()", n->name);
        else sc_parse_error(P, n->line, "unknown name '%s' (declare it with var)", n->name);
    } else if (n->kind == N_CALL) {
        if ((n->cls = sc_find_class(P->prog, n->name)) != NULL) {
            n->ref = REF_CLASS;                                    /* Point(1, 2): a new object */
        } else if ((n->index = sc_find_func(P->prog, n->name)) >= 0) {
            if (P->cls) sc_parse_error(P, n->line, "a struct or class can't call the script's functions ('%s'): make it a static function", n->name);
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

static const char *sc_article(int t) { return t == TY_INT || t == TY_ENT || t == TY_ANY || t == TY_OBJ ? "an" : "a"; }
static bool sc_vowel(char c) { return c && strchr("AEIOUaeiou", c) != NULL; }
static const char *sc_ty_article(sc_ty ty) {
    if (ty.t == TY_OBJ && ty.cls) return sc_vowel(ty.cls->name[0]) ? "an" : "a";
    if (ty.flags & TYF_UNSIGNED) return "an";                 /* an unsigned int */
    return sc_article(ty.t);
}

/* Can a value of type `from` be stored where `to` is declared? */
static bool sc_fits(sc_ty to, sc_ty from) {
    if (!sc_known(to) || !sc_known(from)) return from.t != TY_VOID;
    if ((to.flags & TYF_UNSIGNED) && (from.flags & TYF_NEGATIVE)) return false;
    if (to.t == TY_OBJ) {                                    /* the class or one of its children; classes may be nil */
        if (from.t == TY_NIL) return !to.cls->is_struct;
        return from.t == TY_OBJ && sc_derives(from.cls, to.cls);
    }
    if (to.t == from.t) {
        if (to.t != TY_LIST || to.elem == TY_NONE || from.elem == TY_NONE) return true;
        if (to.elem == TY_OBJ || from.elem == TY_OBJ) return to.elem == from.elem && sc_derives(from.cls, to.cls);
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
    if ((to.flags & TYF_UNSIGNED) && (from.flags & TYF_NEGATIVE)) sc_parse_error(P, line, "%s is unsigned, so it can't hold a negative number", what);
    if (to.t == TY_OBJ && from.t == TY_NIL) sc_parse_error(P, line, "%s is %s %s, a struct, so it can't be nil", what, sc_ty_article(to), to.cls->name);
    if (to.t == TY_INT && from.t == TY_NUM)
        sc_parse_error(P, line, "%s is an int, and this number may have a fraction (floor() or round() make it whole)", what);
    sc_parse_error(P, line, "%s is %s %s, so it can't hold %s %s", what, sc_ty_article(to),
                   sc_ty_name(to, a, sizeof(a)), sc_ty_article(from), sc_ty_name(from, b, sizeof(b)));
}

/* The type a built-in signature letter stands for. */
static sc_ty sc_sig_type(char c) {
    switch (c) {
    case 'n': return sc_ty_make(TY_NUM, 0);  case 'i': return sc_ty_make(TY_INT, 0);
    case 's': return sc_ty_make(TY_STR, 0);  case 'b': return sc_ty_make(TY_BOOL, 0);
    case 'v': return sc_ty_make(TY_VEC, 0);  case 'l': return sc_ty_make(TY_LIST, 0);
    case '3': return sc_ty_make(TY_VEC3, 0);
    case 'e': return sc_ty_make(TY_ENT, 0);  case 'c': return sc_ty_make(TY_COMP, 0);
    case 'L': return sc_ty_make(TY_LIST, TY_ENT);
    case 'R': return sc_ty_make(TY_LIST, TY_INT);
    case 'W': return sc_ty_make(TY_LIST, TY_STR);
    case '-': return sc_ty_make(TY_VOID, 0);
    default:  return sc_ty_make(TY_NONE, 0);   /* a (any), x / t / p / q / V: checked below */
    }
}

static sc_ty sc_expr_type(sc_parser *P, sc_node *n);



static void sc_check_arg(sc_parser *P, const sc_node *call, int i, char code, sc_ty got) {
    char want[48], tmp[32], have[32];
    sc_ty need;
    if (!sc_known(got) || code == 'a') return;
    switch (code) {
    case 'x': if (sc_numeric(got.t) || sc_vecty(got.t)) return; snprintf(want, sizeof(want), "a number or a vector"); break;
    case 'V': if (sc_vecty(got.t)) return; snprintf(want, sizeof(want), "a vec2 or a vec3"); break;
    case 't': if (got.t == TY_LIST || got.t == TY_STR) return; snprintf(want, sizeof(want), "a list or a string"); break;
    case 'p': if (sc_vecty(got.t) || got.t == TY_NIL) return; snprintf(want, sizeof(want), "a vector (or nil)"); break;
    case 'q': if (got.t == TY_ENT || got.t == TY_NIL) return; snprintf(want, sizeof(want), "an entity (or nil)"); break;
    default:
        need = sc_sig_type(code);
        if (sc_fits(need, got) && !(code == 'e' && got.t == TY_NIL)) return;
        snprintf(want, sizeof(want), "%s %s", sc_article(need.t), sc_ty_name(need, tmp, sizeof(tmp)));
        break;
    }
    sc_parse_error(P, call->items[i]->line, "%s() wants %s for argument %d, not %s %s", call->name, want, i + 1,
                   sc_ty_article(got), sc_ty_name(got, have, sizeof(have)));
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
        if (!sc_fits(sc_elem_ty(args[0]), args[1]))
            sc_parse_error(P, n->line, "push(): the list holds %s, not %s", sc_ty_name(sc_elem_ty(args[0]), a, sizeof(a)), sc_ty_name(args[1], bb, sizeof(bb)));
    }
    if (!ret) return sc_ty_make(TY_NONE, 0);
    switch (ret[1]) {
    case 'S': {                                  /* like the arguments: abs, min, max, clamp, lerp, push */
        if (n->count == 0) return sc_ty_make(TY_NONE, 0);
        if (strcmp(n->name, "push") == 0) return args[0];
        if (sc_vecty(args[0].t)) return args[0];
        for (i = 0, r = sc_ty_make(TY_INT, 0); i < n->count && i < 16; ++i) {
            if (strcmp(n->name, "lerp") == 0 && i == 2) break;
            if (!sc_known(args[i])) return sc_ty_make(TY_NONE, 0);
            if (args[i].t != TY_INT) r.t = TY_NUM;
        }
        if (strcmp(n->name, "lerp") == 0) r.t = TY_NUM;
        return r;
    }
    case 'E':                                    /* an item of the list: pop, remove */
        return n->count && args[0].t == TY_LIST ? sc_elem_ty(args[0]) : sc_ty_make(TY_NONE, 0);
    default:
        return sc_sig_type(ret[1]);
    }
}

/* Entity properties: their types (in 2D, and in @3d scripts), and what setting them takes. */
static const struct { const char *name; uint8_t t, t3; bool settable; } k_props[] = {
    { "name", TY_STR, TY_STR, true }, { "id", TY_INT, TY_INT, false }, { "alive", TY_BOOL, TY_BOOL, false },
    { "position", TY_VEC, TY_VEC3, true }, { "x", TY_NUM, TY_NUM, true }, { "y", TY_NUM, TY_NUM, true },
    { "z", TY_NUM, TY_NUM, true }, { "rotation", TY_NUM, TY_VEC3, true },
    { "scale", TY_VEC, TY_VEC3, true }, { "global_position", TY_VEC, TY_VEC3, true }, { "global_rotation", TY_NUM, TY_VEC3, true },
    { "parent", TY_ENT, TY_ENT, true }, { "velocity", TY_VEC, TY_VEC3, true }, { "angular_velocity", TY_NUM, TY_VEC3, true },
    { "forward", TY_VEC3, TY_VEC3, false }, { "right", TY_VEC3, TY_VEC3, false }, { "up", TY_VEC3, TY_VEC3, false },
};

/* A struct's or class's member, as code in P->cls sees it: an error if it can't. */
static sc_member *sc_need_member(sc_parser *P, sc_class *c, const char *name, int line, bool via_class, bool call) {
    sc_member *m = sc_class_member(c, name);
    if (!m) {
        if (call) sc_parse_error(P, line, "%s has no function %s()", c->name, name);
        sc_parse_error(P, line, "%s has no member '%s'", c->name, name);
    }
    if (!sc_can_access(P->cls, m))
        sc_parse_error(P, line, "'%s' is %s in %s%s", name, sc_access_word(m->access), m->owner->name,
                       m->access == ACC_PRIVATE ? " (only its own functions can use it)" : " (only it and its children can use it)");
    if (call && !m->fn) sc_parse_error(P, line, "'%s' is a field of %s, not a function", name, c->name);
    if (!call && m->fn) sc_parse_error(P, line, "%s() is a function: call it with ()", name);
    if (via_class && !m->is_static)
        sc_parse_error(P, line, "'%s' belongs to each %s, not to the class: use an object (or make it static)", name, c->name);
    return m;
}

static sc_ty sc_member_type(sc_parser *P, sc_node *n, bool setting) {
    sc_ty obj;
    char a[32];
    size_t i;
    if (n->a->kind == N_CLASSREF || (n->a->kind == N_NAME && n->a->ref == REF_CLASS)) {   /* Counter.total: a static field */
        sc_member *m = sc_need_member(P, n->a->cls, n->name, n->line, true, false);
        if (setting && m->is_const) sc_parse_error(P, n->line, "%s.%s is const, so it can't be changed", m->owner->name, n->name);
        n->mem = m;
        return m->ty;
    }
    obj = sc_expr_type(P, n->a);
    if (obj.t == TY_OBJ) {
        sc_member *m = sc_need_member(P, obj.cls, n->name, n->line, false, false);
        if (setting && m->is_const) {
            if (m->is_static) sc_parse_error(P, n->line, "%s.%s is const, so it can't be changed", m->owner->name, n->name);
            if (!(P->fn && P->fn->is_init && P->cls == m->owner && n->a->kind == N_THIS))
                sc_parse_error(P, n->line, "'%s' is const: only %s's init() can set it (this.%s = ...)", n->name, m->owner->name, n->name);
        }
        n->mem = m;
        return m->ty;
    }
    if (obj.t == TY_VEC) {
        if (strcmp(n->name, "x") == 0 || strcmp(n->name, "y") == 0) return sc_ty_make(TY_NUM, 0);
        if (strcmp(n->name, "length") == 0 && !setting) return sc_ty_make(TY_NUM, 0);
        sc_parse_error(P, n->line, "a vec2 has x, y and length, not '%s'", n->name);
    }
    if (obj.t == TY_VEC3) {
        if (strcmp(n->name, "x") == 0 || strcmp(n->name, "y") == 0 || strcmp(n->name, "z") == 0) return sc_ty_make(TY_NUM, 0);
        if (strcmp(n->name, "length") == 0 && !setting) return sc_ty_make(TY_NUM, 0);
        sc_parse_error(P, n->line, "a vec3 has x, y, z and length, not '%s'", n->name);
    }
    if (obj.t == TY_ENT) {
        if (isupper((unsigned char)n->name[0])) return sc_ty_make(TY_COMP, 0);
        for (i = 0; i < BR_ARRAY_COUNT(k_props); ++i)
            if (strcmp(k_props[i].name, n->name) == 0) {
                if (setting && !k_props[i].settable) sc_parse_error(P, n->line, "%s can't be changed", n->name);
                return sc_ty_make(P->prog->three_d ? k_props[i].t3 : k_props[i].t, 0);
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
        sc_ty r = sc_ty_make(op == T_SLASH ? TY_NUM : a.t == TY_INT && b.t == TY_INT ? TY_INT : TY_NUM, 0);
        if (op != T_MINUS && (a.flags & b.flags & TYF_UNSIGNED)) r.flags |= TYF_UNSIGNED;   /* stays 0 or more */
        return r;
    }
    if (sc_vecty(a.t) && a.t == b.t && (op == T_PLUS || op == T_MINUS || op == T_STAR)) return a;
    if (sc_vecty(a.t) && sc_numeric(b.t) && (op == T_STAR || op == T_SLASH)) return a;
    if (sc_numeric(a.t) && sc_vecty(b.t) && op == T_STAR) return b;
    if (a.t == TY_LIST && b.t == TY_LIST && op == T_PLUS) return sc_same_ty(a, b) ? a : sc_ty_make(TY_LIST, a.elem == b.elem && a.elem != TY_OBJ ? a.elem : TY_NONE);
    sc_parse_error(P, line, "can't use %s on %s %s and %s %s", sym, sc_article(a.t), sc_ty_name(a, an, sizeof(an)), sc_article(b.t), sc_ty_name(b, bn, sizeof(bn)));
    return sc_ty_make(TY_NONE, 0);
}

/* A call's arguments against a function's parameters. */
static void sc_check_args(sc_parser *P, sc_node *n, const sc_func *f, const char *label) {
    int i;
    if (n->count > f->params)
        sc_parse_error(P, n->line, "%s() takes %d argument%s, not %d", label, f->params, f->params == 1 ? "" : "s", n->count);
    for (i = 0; i < n->count; ++i) {
        sc_ty got = sc_expr_type(P, n->items[i]);
        if (!sc_fits(f->param_ty[i], got)) {
            char what[128];
            snprintf(what, sizeof(what), "%s()'s parameter '%s'", label, f->param_names[i]);
            sc_need_fit(P, n->items[i]->line, f->param_ty[i], got, what);
        }
    }
}

/* Point(1, 2): init()'s parameters, or (a struct without init) its fields in order. */
static sc_ty sc_construct_type(sc_parser *P, sc_node *n) {
    sc_class *c = n->cls;
    sc_member *init = sc_class_member(c, "init");
    int i, k;
    if (init && init->fn && init->fn->is_init) {
        char label[96];
        if (!sc_can_access(P->cls, init))
            sc_parse_error(P, n->line, "%s's init() is %s, so only %s can make one", c->name, sc_access_word(init->access),
                           init->access == ACC_PRIVATE ? "its own functions" : "it and its children");
        snprintf(label, sizeof(label), "%s", c->name);
        sc_check_args(P, n, init->fn, label);
        return sc_obj_ty(c);
    }
    if (!c->is_struct) {
        if (n->count) sc_parse_error(P, n->line, "%s has no init(), so %s() takes no arguments", c->name, c->name);
        return sc_obj_ty(c);
    }
    if (n->count > c->field_count)
        sc_parse_error(P, n->line, "%s() takes up to %d argument%s (its fields), not %d", c->name, c->field_count, c->field_count == 1 ? "" : "s", n->count);
    for (i = 0, k = 0; i < c->member_count && k < n->count; ++i) {
        sc_member *m = &c->members[i];
        sc_ty got;
        char what[96];
        if (m->fn || m->is_static) continue;
        if (!sc_can_access(P->cls, m))
            sc_parse_error(P, n->line, "%s's field '%s' is private, so %s() can't set it (give %s an init())", c->name, m->name, c->name, c->name);
        got = sc_expr_type(P, n->items[k]);
        snprintf(what, sizeof(what), "%s's field '%s'", c->name, m->name);
        sc_need_fit(P, n->items[k]->line, m->ty, got, what);
        k++;
    }
    return sc_obj_ty(c);
}

static sc_ty sc_expr_type(sc_parser *P, sc_node *n) {
    sc_ty a, b;
    char an[32];
    int i;
    switch (n->kind) {
    case N_THIS: return sc_obj_ty(P->cls);
    case N_CLASSREF:
        sc_parse_error(P, n->line, "%s is a %s: make one with %s(...), or use its static members (%s.name)", n->cls->name,
                       n->cls->is_struct ? "struct" : "class", n->cls->name, n->cls->name);
        return sc_ty_make(TY_NONE, 0);
    case N_SUPER: {
        sc_member *m;
        char label[96];
        if (!P->cls->parent) sc_parse_error(P, n->line, "%s has no parent class, so there is no super", P->cls->name);
        m = sc_need_member(P, P->cls->parent, n->name, n->line, false, true);
        if (m->is_static) sc_parse_error(P, n->line, "%s() is static: call it as %s.%s()", n->name, m->owner->name, n->name);
        n->mem = m;
        snprintf(label, sizeof(label), "%s.%s", m->owner->name, n->name);
        sc_check_args(P, n, m->fn, label);
        return m->fn->ret;
    }
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
        sc_class *cls = NULL;
        for (i = 0; i < n->count; ++i) {
            sc_ty it = sc_expr_type(P, n->items[i]);
            if (!sc_known(it) || it.t == TY_NIL || it.t == TY_LIST) elem = TY_NONE;
            else if (elem < 0) { elem = it.t; cls = it.cls; }
            else if (elem == TY_OBJ || it.t == TY_OBJ) { if (elem != it.t || cls != it.cls) elem = TY_NONE; }
            else if (elem != it.t) elem = sc_numeric(elem) && sc_numeric(it.t) ? TY_NUM : TY_NONE;
        }
        a = sc_ty_make(TY_LIST, elem < 0 ? TY_NONE : elem);
        if (a.elem == TY_OBJ) a.cls = cls;
        return a;
    }
    case N_NEG:
        a = sc_expr_type(P, n->a);
        a.flags &= (uint8_t)~TYF_UNSIGNED;
        if (n->a->kind == N_NUM) a.flags |= TYF_NEGATIVE;            /* -3: for unsigned's checks */
        if (!sc_known(a) || sc_numeric(a.t) || sc_vecty(a.t)) return a;
        sc_parse_error(P, n->line, "can't negate %s %s", sc_article(a.t), sc_ty_name(a, an, sizeof(an)));
        return a;
    case N_NOT: sc_expr_type(P, n->a); return sc_ty_make(TY_BOOL, 0);
    case N_AND: case N_OR:                       /* one of the two */
        a = sc_expr_type(P, n->a);
        b = sc_expr_type(P, n->b);
        return sc_same_ty(a, b) ? sc_ty_plain(a) : sc_ty_make(TY_NONE, 0);
    case N_BINARY:
        a = sc_expr_type(P, n->a);
        b = sc_expr_type(P, n->b);
        return sc_binary_type(P, n->op, a, b, n->line);
    case N_CALL:
        if (n->ref == REF_BUILTIN) return sc_builtin_type(P, n);
        if (n->ref == REF_CLASS) return sc_construct_type(P, n);
        {
            const sc_func *f = &P->prog->funcs[n->index];
            sc_check_args(P, n, f, f->name);
            return f->ret;
        }
    case N_METHOD:
        if (n->a->kind == N_CLASSREF) {                          /* Counter.reset(): a static function */
            char label[96];
            sc_member *m = sc_need_member(P, n->a->cls, n->name, n->line, false, true);
            if (!m->is_static) sc_parse_error(P, n->line, "%s() belongs to each %s, not to the class: call it on an object (or make it static)", n->name, n->a->cls->name);
            n->mem = m;
            snprintf(label, sizeof(label), "%s.%s", m->owner->name, n->name);
            sc_check_args(P, n, m->fn, label);
            return m->fn->ret;
        }
        a = sc_expr_type(P, n->a);
        if (a.t == TY_OBJ) {
            char label[96];
            sc_member *m = sc_need_member(P, a.cls, n->name, n->line, false, true);
            snprintf(label, sizeof(label), "%s.%s", m->owner->name, n->name);
            sc_check_args(P, n, m->fn, label);
            return m->fn->ret;
        }
        if (sc_known(a) && a.t != TY_ENT)
            sc_parse_error(P, n->line, "%s %s has no function %s()", sc_article(a.t), sc_ty_name(a, an, sizeof(an)), n->name);
        for (i = 0; i < n->count; ++i) sc_expr_type(P, n->items[i]);
        return sc_ty_make(TY_NONE, 0);
    case N_MEMBER: return sc_member_type(P, n, false);
    case N_INDEX:
        a = sc_expr_type(P, n->a);
        b = sc_expr_type(P, n->b);
        if (sc_known(b) && !sc_numeric(b.t)) sc_parse_error(P, n->b->line, "an index is a number, not %s %s", sc_article(b.t), sc_ty_name(b, an, sizeof(an)));
        if (a.t == TY_LIST) return sc_elem_ty(a);
        if (a.t == TY_STR) return sc_ty_make(TY_STR, 0);
        if (sc_known(a)) sc_parse_error(P, n->line, "can't index %s %s with []", sc_article(a.t), sc_ty_name(a, an, sizeof(an)));
        return sc_ty_make(TY_NONE, 0);
    default: return sc_ty_make(TY_NONE, 0);
    }
}

/* The declared type of what an assignment stores into. */
static sc_ty sc_target_type(sc_parser *P, sc_node *t) {
    if (t->kind == N_NAME) {
        if (t->ref == REF_GLOBAL && P->prog->globals[t->index].is_const) sc_parse_error(P, t->line, "%s is const, so it can't be changed", t->name);
        return sc_expr_type(P, t);
    }
    if (t->kind == N_MEMBER) {
        sc_ty ty = sc_member_type(P, t, true);
        if (ty.t == TY_COMP) sc_parse_error(P, t->line, "set a component's fields, like self.%s.color", t->name);
        return ty;
    }
    if (t->kind == N_INDEX) {
        sc_ty list = sc_expr_type(P, t->a);
        sc_expr_type(P, t->b);
        if (list.t == TY_STR) sc_parse_error(P, t->line, "strings can't be changed (build a new one with +)");
        return list.t == TY_LIST ? sc_elem_ty(list) : sc_ty_make(TY_NONE, 0);
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
            n->ty = a.t == TY_LIST ? sc_ty_make(TY_LIST, TY_NONE) : sc_ty_plain(a);
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
        if (n->a->kind == N_MEMBER && strcmp(n->a->name, "scale") == 0 && sc_numeric(b.t) && !n->a->mem) return;   /* scale = 2 */
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
        else if (a.t == TY_LIST) item = sc_elem_ty(a);
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

/* What the engine passes to each callback (in @3d scripts, on_collision's normal is a vec3). */
static const char *const k_callback_sig[CB_COUNT] = { "", "n", "evn", "e", "e", "e", "", "", "" };
static const char *const k_callback_sig3[CB_COUNT] = { "", "n", "e3n", "e", "e", "e", "", "", "" };

/* A function's body: its parameters' (and this's) types, then its statements. */
static void sc_check_func(sc_parser *P, sc_func *f) {
    int k, off = f->has_this ? 1 : 0;
    memset(P->slot_ty, 0, sizeof(P->slot_ty));
    if (f->has_this) P->slot_ty[0] = sc_obj_ty(f->cls);
    for (k = 0; k < f->params; ++k) P->slot_ty[k + off] = f->param_ty[k];
    P->fn = f;
    P->cls = f->cls;
    sc_check_stmt(P, f->body);
    if (sc_known(f->ret) && f->ret.t != TY_VOID && f->ret.t != TY_ENT && !(f->ret.t == TY_OBJ && !f->ret.cls->is_struct) &&
        !sc_always_returns(f->body))
        sc_parse_error(P, f->line, "not every way through %s() ends with a return (it must return %s %s)", f->name,
                       sc_article(f->ret.t), sc_tyk_name(f->ret.t));
    P->fn = NULL;
    P->cls = NULL;
}

static SC_NOINLINE void sc_typecheck(sc_parser *P) {
    sc_program *prog = P->prog;
    int i, k;
    P->fn = NULL;
    for (i = 0; i < prog->class_count; ++i) {                /* fields' starting values first: the rest may use them */
        sc_class *c = prog->classes[i];
        P->cls = c;
        for (k = 0; k < c->member_count; ++k) {
            sc_member *m = &c->members[k];
            sc_ty t;
            if (m->fn || !m->init) continue;
            memset(P->slot_ty, 0, sizeof(P->slot_ty));
            t = sc_expr_type(P, m->init);
            if (m->infer) {
                if (t.t == TY_VOID) sc_parse_error(P, m->line, "that call returns nothing (-> void), so there is no type to take");
                if (!sc_known(t) || t.t == TY_NIL) sc_parse_error(P, m->line, "can't tell the type of %s's value before it runs: write var %s: type = ...", m->name, m->name);
                m->ty = t.t == TY_LIST ? sc_ty_make(TY_LIST, TY_NONE) : sc_ty_plain(t);
            } else if (m->ty.t) {
                sc_need_fit(P, m->line, m->ty, t, m->name);
            }
        }
        P->cls = NULL;
    }
    for (i = 0; i < prog->global_count; ++i) {               /* in order: a := may use the ones above it */
        sc_global *g = &prog->globals[i];
        sc_ty t;
        memset(P->slot_ty, 0, sizeof(P->slot_ty));
        if (!g->init) continue;
        t = sc_expr_type(P, g->init);
        if (g->infer) {
            if (t.t == TY_VOID) sc_parse_error(P, g->line, "that call returns nothing (-> void), so there is no type to take");
            if (!sc_known(t) || t.t == TY_NIL) sc_parse_error(P, g->line, "can't tell the type of %s's value before it runs: write var %s: type = ...", g->name, g->name);
            g->ty = t.t == TY_LIST ? sc_ty_make(TY_LIST, TY_NONE) : sc_ty_plain(t);
        } else if (g->ty.t) {
            sc_need_fit(P, g->line, g->ty, t, g->name);
        }
    }
    for (i = 0; i < prog->func_count; ++i) {
        sc_func *f = &prog->funcs[i];
        int cb;
        for (cb = 0; cb < CB_COUNT; ++cb) {                   /* the engine calls these: their types are fixed */
            if (strcmp(f->name, k_callbacks[cb]) != 0) continue;
            for (k = 0; k < f->params; ++k) {
                char want[32], have[32];
                const char *sig = prog->three_d ? k_callback_sig3[cb] : k_callback_sig[cb];
                sc_ty need = k < (int)strlen(sig) ? sc_sig_type(sig[k]) : sc_ty_make(TY_NIL, 0);
                if (sc_known(f->param_ty[k]) && !sc_fits(f->param_ty[k], need))
                    sc_parse_error(P, f->line, "the engine passes %s %s as %s()'s parameter '%s', not %s %s", sc_article(need.t),
                                   sc_ty_name(need, want, sizeof(want)), f->name, f->param_names[k], sc_article(f->param_ty[k].t),
                                   sc_ty_name(f->param_ty[k], have, sizeof(have)));
            }
            if (sc_known(f->ret) && f->ret.t != TY_VOID) sc_parse_error(P, f->line, "the engine ignores what %s() returns: use -> void", f->name);
        }
        sc_check_func(P, f);
    }
    for (i = 0; i < prog->class_count; ++i)
        for (k = 0; k < prog->classes[i]->member_count; ++k)
            if (prog->classes[i]->members[k].fn) sc_check_func(P, prog->classes[i]->members[k].fn);
}

static void sc_program_free(sc_program *p) {
    int i;
    if (!p) return;
    while (p->arena) { sc_chunk *c = p->arena; p->arena = c->next; br_free(c); }
    for (i = 0; i < p->pinned_count; ++i) br_free(p->pinned[i]);
    br_free(p->pinned);
    br_free(p->funcs);
    br_free(p->globals);
    br_free(p->classes);
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
    sc_find_classes(&P, source);
    sc_advance(&P);
    sc_skip_newlines(&P);
    prog->strict = g_strict_default;
    while (P.cur.t != T_EOF) {
        if (P.cur.t == T_AT) {                                   /* @strict, @3d */
            int line = P.cur.line;
            sc_advance(&P);
            if (P.cur.t == T_NUM && P.cur.len == 1 && P.cur.num == 3 && (P.cur.start[1] == 'd' || P.cur.start[1] == 'D') &&
                !isalnum((unsigned char)P.cur.start[2]) && P.cur.start[2] != '_') {
                if (P.decls) sc_parse_error(&P, line, "@3d goes at the top of the file, before any var, func or class");
                sc_advance(&P);                                  /* "3", then "d" */
                sc_advance(&P);
                prog->three_d = true;
                if (P.cur.t != T_AT) sc_end_statement(&P);   /* (@strict @3d: both on one line) */
                continue;
            }
            if (P.cur.t != T_NAME || P.cur.len != 6 || strncmp(P.cur.start, "strict", 6) != 0)
                sc_parse_error(&P, line, "unknown annotation '@%.*s' (there are @strict and @3d)", P.cur.len, P.cur.start);
            if (P.decls) sc_parse_error(&P, line, "@strict goes at the top of the file, before any var, func or class");
            sc_advance(&P);
            prog->strict = true;
            if (P.cur.t != T_AT) sc_end_statement(&P);   /* (@strict @3d: both on one line) */
            continue;
        }
        if (P.cur.t == T_NAME && P.cur.len == 9 && strncmp(P.cur.start, "component", 9) == 0) {   /* (read by gamecomp.c: skipped here) */
            int depth = 0;
            sc_advance(&P);
            while (P.cur.t != T_EOF && P.cur.t != T_LBRACE) sc_advance(&P);
            do {
                if (P.cur.t == T_LBRACE) ++depth;
                else if (P.cur.t == T_RBRACE) --depth;
                else if (P.cur.t == T_EOF) sc_parse_error(&P, P.cur.line, "a component is never closed with '}'");
                sc_advance(&P);
            } while (depth > 0);
            sc_skip_newlines(&P);
            continue;
        }
        P.decls++;
        if (P.cur.t == T_FUNC) sc_function(&P);
        else if (P.cur.t == T_VAR || P.cur.t == T_CONST) sc_global_var(&P);
        else if (P.cur.t == T_CLASS || P.cur.t == T_STRUCT) sc_class_decl(&P);
        else if (P.cur.t >= T_PUBLIC && P.cur.t <= T_OVERRIDE)
            sc_parse_error(&P, P.cur.line, "'%.*s' is for the members of a struct or class", P.cur.len, P.cur.start);
        else sc_parse_error(&P, P.cur.line, "only var, const, func, struct, class and component can be at the top of a script (put code inside start())");
        sc_skip_newlines(&P);
    }
    sc_link_classes(&P);
    for (i = 0; i < prog->global_count; ++i) {
        P.local_count = 0;
        sc_resolve(&P, prog->globals[i].init);
    }
    for (i = 0; i < prog->func_count; ++i) sc_resolve(&P, prog->funcs[i].body);
    for (i = 0; i < prog->class_count; ++i) {
        sc_class *c = prog->classes[i];
        int k;
        P.cls = c;
        for (k = 0; k < c->member_count; ++k) sc_resolve(&P, c->members[k].fn ? c->members[k].fn->body : c->members[k].init);
        P.cls = NULL;
    }
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

/* A tween: one property eased from where it is to a value over some seconds (tween()). */
typedef struct sc_tween {
    int id;
    br_entity e, owner;             /* what it changes; whose script made it (for tween_then()) */
    char comp[32], prop[32];        /* "Shape" "color", or "" "position" */
    int kind;                       /* 0 number, 1 vec2, 2 vec3, 3 colour */
    double from[4], to[4];
    float time, dur, delay;
    int ease;
    int repeat;                     /* more times to go (-1: for ever) */
    bool yoyo, backward, started;
    char then[32];                  /* the owner's function to call when it ends */
} sc_tween;

struct sc_runtime {
    br_world *w;
    sc_tween *tweens;
    int tween_count, tween_cap, next_tween;
    float dt;                       /* this step's (move_and_slide() moves by velocity * dt) */
    sc_heap heap;
    sc_program **programs;
    int program_count, program_cap;
    sc_instance **instances;
    int instance_count, instance_cap;
    br_component script_id;
    br_builtin_ids ids;
    br_component body_id, collider_id;
    br_ids3d ids3;
    br_component body3_id;
    char last_error[300];
    uint32_t error_count;
    char scene_request[BR_PATH_MAX];  /* load_scene(), for the game to act on */
    bool quit_request;                /* quit() */
    char push_request[BR_PATH_MAX];   /* push_scene() */
    bool push_transparent, push_update_below;
    bool pop_request;                 /* pop_scene() */
    br_rng rng;
    br_rng draw_rng;                  /* random() inside draw(), which may not run every frame */
    br_renderer *draw_r;              /* while a draw() runs: what draw_rect() and the rest draw on */
    uint64_t text_frame, wheel_frame, motion_frame; /* the engine frame whose typed text / wheel / mouse motion a script last took */
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
    const sc_class *cls;            /* the class whose function is running (NULL: none): what private and protected allow */
    bool in_init;                   /* running that class's init(): its const fields can be set */
    uint32_t serial;                /* which run this is (the debugger's stepping tells them apart) */
    int nframes;                    /* the functions running, innermost last (for the debugger) */
    struct { const sc_func *f; sc_instance *inst; int base, line; } frames[SC_MAX_DEPTH + 1];
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
    case V_LIST: return "a list";   case V_VEC3: return "a vec3";
    case V_OBJ: {                                    /* "a Point": a few at a time can be in one message */
        static char names[4][80];
        static int next;
        char *b = names[next++ & 3];
        snprintf(b, sizeof(names[0]), "%s %s", sc_vowel(v->as.o->cls->name[0]) ? "an" : "a", v->as.o->cls->name);
        return b;
    }
    default: return "a component";
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
    case V_VEC3: snprintf(p, left, "(%.10g, %.10g, %.10g)", v->as.v.x, v->as.v.y, v->as.v.z); break;
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
    case V_OBJ: {                                    /* Point(x: 1, y: 2): every field, inherited ones first */
        const sc_object *o = v->as.o;
        const sc_class *chain[64];
        int depth_n = 0, c, k, shown = 0;
        const sc_class *cl;
        if (depth > 3) { snprintf(p, left, "%s(...)", o->cls->name); break; }
        snprintf(p, left, "%s(", o->cls->name);
        for (cl = o->cls; cl && depth_n < 64; cl = cl->parent) chain[depth_n++] = cl;
        for (c = depth_n - 1; c >= 0; --c)
            for (k = 0; k < chain[c]->member_count; ++k) {
                const sc_member *m = &chain[c]->members[k];
                if (m->fn || m->is_static || m->slot < 0 || (uint32_t)m->slot >= o->count) continue;
                if (shown++) strncat(buf, ", ", size - strlen(buf) - 1);
                strncat(buf, m->name, size - strlen(buf) - 1);
                strncat(buf, ": ", size - strlen(buf) - 1);
                sc_append(vm, buf, size, &o->fields[m->slot], depth + 1);
            }
        strncat(buf, ")", size - strlen(buf) - 1);
        break;
    }
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
    case V_VEC3: return a->as.v.x == b->as.v.x && a->as.v.y == b->as.v.y && a->as.v.z == b->as.v.z;
    case V_ENT: return a->as.e == b->as.e;
    case V_LIST: return a->as.l == b->as.l;
    case V_COMP: return a->as.comp.e == b->as.comp.e && a->as.comp.c == b->as.comp.c;
    case V_OBJ: {                                    /* structs: the same fields; class objects: the same object */
        uint32_t i;
        if (a->as.o == b->as.o) return true;
        if (!a->as.o->cls->is_struct || a->as.o->cls != b->as.o->cls) return false;
        for (i = 0; i < a->as.o->count; ++i) if (!sc_equal(&a->as.o->fields[i], &b->as.o->fields[i])) return false;
        return true;
    }
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
    case TY_INT: return v->t == V_NUM && v->as.n == floor(v->as.n) && (!(ty.flags & TYF_UNSIGNED) || v->as.n >= 0);
    case TY_NUM: return v->t == V_NUM && (!(ty.flags & TYF_UNSIGNED) || v->as.n >= 0);
    case TY_OBJ:
        if (v->t == V_NIL) return !ty.cls->is_struct;
        return v->t == V_OBJ && sc_derives(v->as.o->cls, ty.cls);
    case TY_STR: return v->t == V_STR;
    case TY_VEC: return v->t == V_VEC;
    case TY_VEC3: return v->t == V_VEC3;
    case TY_ENT: return v->t == V_ENT || v->t == V_NIL;
    case TY_COMP: return v->t == V_COMP || v->t == V_NIL;
    case TY_LIST:
        if (v->t != V_LIST) return false;
        if (ty.elem == TY_NONE) return true;
        for (i = 0; i < v->as.l->count; ++i) if (!sc_value_is(&v->as.l->items[i], sc_elem_ty(ty))) return false;
        return true;
    default: return false;
    }
}

/* Where a typed variable starts without a value: GDScript-like defaults. */
static sc_value sc_default(sc_vm *vm, sc_ty ty, int line);

static void sc_check_value(sc_vm *vm, sc_ty ty, const sc_value *v, int line, const char *what) {
    char tn[32];
    if (sc_value_is(v, ty)) return;
    if (sc_numeric(ty.t) && (ty.flags & TYF_UNSIGNED) && v->t == V_NUM && v->as.n < 0)
        sc_fail(vm, line, "%s is unsigned, and %g is below 0", what, v->as.n);
    if (ty.t == TY_INT && v->t == V_NUM) sc_fail(vm, line, "%s is an int, and %g isn't a whole number", what, v->as.n);
    if (ty.t == TY_OBJ && v->t == V_NIL) sc_fail(vm, line, "%s is %s %s, a struct, so it can't be nil", what, sc_ty_article(ty), ty.cls->name);
    if (ty.t == TY_LIST && v->t == V_LIST) sc_fail(vm, line, "%s is a %s, and this list holds something else", what, sc_ty_name(ty, tn, sizeof(tn)));
    sc_fail(vm, line, "%s is %s %s, so it can't hold %s", what, sc_ty_article(ty), sc_ty_name(ty, tn, sizeof(tn)), sc_type_name(v));
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
static sc_value sc_invoke(sc_vm *vm, sc_instance *inst, const sc_func *f, const sc_value *self_obj, const sc_value *args, int argc, int line);

/* Structs are values: storing one (in a variable, a field, a list, a
 * parameter) stores a copy, so every place owns its own. Class objects
 * are shared. */
static sc_value sc_own(sc_vm *vm, sc_value v, int line) {
    sc_object *o, *c;
    uint32_t i;
    if (v.t != V_OBJ || !v.as.o->cls->is_struct) return v;
    o = v.as.o;
    if (!(c = sc_object_alloc(&vm->rt->heap, o->cls, o->count))) sc_fail(vm, line, "out of memory");
    for (i = 0; i < o->count; ++i) c->fields[i] = sc_own(vm, o->fields[i], line);
    v.as.o = c;
    return v;
}

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
    if (a.t == V_VEC3 && b.t == V_VEC3 && (op == T_PLUS || op == T_MINUS || op == T_STAR)) {
        if (op == T_PLUS) return sc_vec3(a.as.v.x + b.as.v.x, a.as.v.y + b.as.v.y, a.as.v.z + b.as.v.z);
        if (op == T_MINUS) return sc_vec3(a.as.v.x - b.as.v.x, a.as.v.y - b.as.v.y, a.as.v.z - b.as.v.z);
        return sc_vec3(a.as.v.x * b.as.v.x, a.as.v.y * b.as.v.y, a.as.v.z * b.as.v.z);
    }
    if (a.t == V_VEC3 && b.t == V_NUM && (op == T_STAR || op == T_SLASH)) {
        if (op == T_SLASH && b.as.n == 0) sc_fail(vm, line, "division by zero");
        return op == T_STAR ? sc_vec3(a.as.v.x * b.as.n, a.as.v.y * b.as.n, a.as.v.z * b.as.n)
                            : sc_vec3(a.as.v.x / b.as.n, a.as.v.y / b.as.n, a.as.v.z / b.as.n);
    }
    if (a.t == V_NUM && b.t == V_VEC3 && op == T_STAR) return sc_vec3(a.as.n * b.as.v.x, a.as.n * b.as.v.y, a.as.n * b.as.v.z);
    if (a.t == V_LIST && b.t == V_LIST && op == T_PLUS) {
        sc_list *l = sc_list_alloc(&vm->rt->heap);
        sc_value r = sc_nil();
        uint32_t n = a.as.l->count + b.as.l->count;
        if (!l || (n && !(l->items = (sc_value *)br_alloc(sizeof(sc_value) * n)))) sc_fail(vm, line, "out of memory");
        if (a.as.l->count) memcpy(l->items, a.as.l->items, sizeof(sc_value) * a.as.l->count);
        if (b.as.l->count) memcpy(l->items + a.as.l->count, b.as.l->items, sizeof(sc_value) * b.as.l->count);
        l->count = l->cap = n;
        { uint32_t i; for (i = 0; i < n; ++i) l->items[i] = sc_own(vm, l->items[i], line); }   /* structs: copies */
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

static sc_value sc_new_object(sc_vm *vm, sc_class *c, int line);

static sc_value sc_default(sc_vm *vm, sc_ty ty, int line) {
    switch (ty.t) {
    case TY_OBJ: return ty.cls->is_struct ? sc_new_object(vm, ty.cls, line) : sc_nil();   /* a struct with its fields' starting values */
    case TY_INT: case TY_NUM: return sc_num(0);
    case TY_BOOL: return sc_bool(false);
    case TY_STR: return sc_string(vm, "", 0);
    case TY_VEC: return sc_vec(0, 0);
    case TY_VEC3: return sc_vec3(0, 0, 0);
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

/* An object with every field at its starting value, the parent's fields first (init() isn't called). */
static sc_value sc_new_object(sc_vm *vm, sc_class *c, int line) {
    const sc_class *chain[64];
    const sc_class *saved_cls = vm->cls;
    bool saved_init = vm->in_init;
    sc_class *cl;
    sc_object *o;
    sc_value v = sc_nil();
    int n = 0, i, k;
    if (vm->depth >= SC_MAX_DEPTH) sc_fail(vm, line, "too many nested calls (endless recursion?)");
    if (!(o = sc_object_alloc(&vm->rt->heap, c, (uint32_t)c->field_count))) sc_fail(vm, line, "out of memory");
    v.t = V_OBJ;
    v.as.o = o;
    for (cl = c; cl && n < 64; cl = cl->parent) chain[n++] = cl;
    vm->depth++;
    for (i = n - 1; i >= 0; --i) {
        vm->cls = chain[i];
        vm->in_init = false;
        for (k = 0; k < chain[i]->member_count; ++k) {
            const sc_member *m = &chain[i]->members[k];
            sc_value fv;
            if (m->fn || m->is_static) continue;
            fv = m->init ? sc_own(vm, sc_eval(vm, m->init), m->line) : sc_default(vm, m->ty, m->line);
            if (m->ty.t && m->init) sc_check_value(vm, m->ty, &fv, m->line, m->name);
            o->fields[m->slot] = fv;
        }
    }
    vm->depth--;
    vm->cls = saved_cls;
    vm->in_init = saved_init;
    return v;
}

/* Point(1, 2): a new object, then its init() (or, a struct without one, its fields in order). */
static sc_value sc_construct(sc_vm *vm, sc_class *c, const sc_value *args, int argc, int line) {
    sc_value v = sc_new_object(vm, c, line);
    sc_member *init = sc_class_member(c, "init");
    int i, k;
    if (init && init->fn && init->fn->is_init) {
        sc_invoke(vm, vm->inst, init->fn, &v, args, argc, line);
        return v;
    }
    if (argc && !c->is_struct) sc_fail(vm, line, "%s has no init(), so %s() takes no arguments", c->name, c->name);
    for (i = 0, k = 0; i < c->member_count && k < argc; ++i) {
        const sc_member *m = &c->members[i];
        char what[96];
        if (m->fn || m->is_static) continue;
        snprintf(what, sizeof(what), "%s's field '%s'", c->name, m->name);
        if (m->ty.t) sc_check_value(vm, m->ty, &args[k], line, what);
        v.as.o->fields[m->slot] = sc_own(vm, args[k], line);
        k++;
    }
    if (k < argc) sc_fail(vm, line, "%s() takes up to %d arguments (its fields), not %d", c->name, k, argc);
    return v;
}

/* A member of an object, as the running code may use it. `hint`: the field the type checker found. */
static sc_member *sc_obj_member(sc_vm *vm, const sc_object *o, const char *name, sc_member *hint, bool call, int line) {
    sc_member *m = !call && hint && !hint->fn && sc_derives(o->cls, hint->owner) ? hint : sc_class_member(o->cls, name);
    if (!m) {
        if (call) sc_fail(vm, line, "%s has no function %s()", o->cls->name, name);
        sc_fail(vm, line, "%s has no member '%s'", o->cls->name, name);
    }
    if (!sc_can_access(vm->cls, m)) sc_fail(vm, line, "'%s' is %s in %s", name, sc_access_word(m->access), m->owner->name);
    if (call && !m->fn) sc_fail(vm, line, "'%s' is a field of %s, not a function", name, o->cls->name);
    if (!call && m->fn) sc_fail(vm, line, "%s() is a function: call it with ()", name);
    return m;
}

static sc_value *sc_field_ref(sc_member *m, const sc_object *o) {
    return m->is_static ? &m->owner->statics[m->slot] : &o->fields[m->slot];
}

static void sc_store_field(sc_vm *vm, sc_member *m, const sc_object *o, sc_value val, int line) {
    if (m->is_const && !(vm->in_init && vm->cls == m->owner && !m->is_static))
        sc_fail(vm, line, "'%s' is const: only %s's init() can set it", m->name, m->owner->name);
    if (m->ty.t) sc_check_value(vm, m->ty, &val, line, m->name);
    *sc_field_ref(m, o) = val;
}

/* Stores `val` into the place `target` describes (a variable, a property, a list item). */
static void sc_assign(sc_vm *vm, sc_node *target, sc_value val) {
    val = sc_own(vm, val, target->line);
    switch (target->kind) {
    case N_NAME:
        if (target->ty.t) sc_check_value(vm, target->ty, &val, target->line, target->name);
        if (target->ref == REF_LOCAL) *sc_local(vm, target->index) = val;
        else vm->inst->globals[target->index] = val;
        return;
    case N_MEMBER: {
        sc_value obj;
        if (target->a->kind == N_CLASSREF) { sc_store_field(vm, target->mem, NULL, val, target->line); return; }   /* Counter.total = 0 */
        obj = sc_eval(vm, target->a);
        if (obj.t == V_OBJ) {
            sc_member *m = sc_obj_member(vm, obj.as.o, target->name, target->mem, false, target->line);
            sc_store_field(vm, m, obj.as.o, val, target->line);
            return;
        }
        if (obj.t == V_VEC || obj.t == V_VEC3) {                 /* a copy: change it, store it back */
            if (strcmp(target->name, "x") == 0) obj.as.v.x = sc_need_num(vm, &val, target->line, "x");
            else if (strcmp(target->name, "y") == 0) obj.as.v.y = sc_need_num(vm, &val, target->line, "y");
            else if (obj.t == V_VEC3 && strcmp(target->name, "z") == 0) obj.as.v.z = sc_need_num(vm, &val, target->line, "z");
            else sc_fail(vm, target->line, obj.t == V_VEC3 ? "a vec3 has only x, y and z" : "a vec2 has only x and y");
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
    if (n->ref == REF_CLASS) return sc_construct(vm, n->cls, args, n->count, n->line);
    return sc_builtin_at(n->index)->fn(vm, args, n->count, n->line);
}

static sc_instance *sc_instance_of(sc_runtime *rt, br_entity e) {
    const br_script *s = br_entity_alive(rt->w, e) ? (const br_script *)br_get(rt->w, e, rt->script_id) : NULL;
    return s ? (sc_instance *)s->instance : NULL;
}

static sc_value sc_eval_method(sc_vm *vm, sc_node *n) {
    sc_value obj, args[16];
    sc_instance *target;
    int i, fn;
    if (n->count > 16) sc_fail(vm, n->line, "too many arguments");
    if (n->a->kind == N_CLASSREF) {                              /* Counter.reset(): static */
        for (i = 0; i < n->count; ++i) args[i] = sc_eval(vm, n->items[i]);
        return sc_invoke(vm, vm->inst, n->mem->fn, NULL, args, n->count, n->line);
    }
    obj = sc_eval(vm, n->a);
    if (obj.t == V_OBJ) {                                        /* the object's own class's version: overrides win */
        sc_member *m = sc_obj_member(vm, obj.as.o, n->name, NULL, true, n->line);
        for (i = 0; i < n->count; ++i) args[i] = sc_eval(vm, n->items[i]);
        return sc_invoke(vm, vm->inst, m->fn, m->is_static ? NULL : &obj, args, n->count, n->line);
    }
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
    case N_THIS: return *sc_local(vm, 0);
    case N_CLASSREF: sc_fail(vm, n->line, "%s is a class, not a value", n->cls->name); break;
    case N_SUPER: {                                              /* super.speak(): the parent's version, on this */
        sc_value self_obj = *sc_local(vm, 0), args[16];
        int i;
        if (n->count > 16) sc_fail(vm, n->line, "too many arguments");
        for (i = 0; i < n->count; ++i) args[i] = sc_eval(vm, n->items[i]);
        return sc_invoke(vm, vm->inst, n->mem->fn, &self_obj, args, n->count, n->line);
    }
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
            for (i = 0; i < n->count; ++i) { l->items[i] = sc_own(vm, sc_eval(vm, n->items[i]), n->line); l->count++; }
        }
        return v;
    }
    case N_NEG: {
        sc_value a = sc_eval(vm, n->a);
        if (a.t == V_NUM) return sc_num(-a.as.n);
        if (a.t == V_VEC) return sc_vec(-a.as.v.x, -a.as.v.y);
        if (a.t == V_VEC3) return sc_vec3(-a.as.v.x, -a.as.v.y, -a.as.v.z);
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
    case N_MEMBER: {
        sc_value o;
        if (n->a->kind == N_CLASSREF) return n->mem->owner->statics[n->mem->slot];     /* Counter.total */
        o = sc_eval(vm, n->a);
        if (o.t == V_OBJ) return *sc_field_ref(sc_obj_member(vm, o.as.o, n->name, n->mem, false, n->line), o.as.o);
        return sc_get_member(vm, &o, n->name, n->line);
    }
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

static void sc_debug_statement(sc_vm *vm, int line);
static bool g_debug_on;                                     /* a debugger is attached (and has something to do) */
static uint32_t g_debug_serial;

static int sc_exec(sc_vm *vm, sc_node *n) {
    if (--vm->budget < 0) sc_fail(vm, n->line, "this took too long (an endless loop?)");
    if (n->kind != N_BLOCK) {
        if (vm->nframes > 0 && vm->nframes <= SC_MAX_DEPTH + 1) vm->frames[vm->nframes - 1].line = n->line;
        if (g_debug_on) sc_debug_statement(vm, n->line);
    }
    switch (n->kind) {
    case N_BLOCK: return sc_exec_block(vm, n);
    case N_EXPR: sc_eval(vm, n->a); return FLOW_NORMAL;
    case N_VAR: {
        sc_value v = n->a ? sc_own(vm, sc_eval(vm, n->a), n->line) : sc_default(vm, n->ty, n->line);   /* first: a call may move the stack */
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
            sc_value item = sc_own(vm, seq.as.l->items[i], n->line);
            if (n->ty.t) sc_check_value(vm, n->ty, &item, n->line, "the loop variable");
            *sc_local(vm, n->index) = item;
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

/* Runs a function: a plain one (self_obj NULL) or a method (on self_obj, its `this`). */
static sc_value sc_invoke(sc_vm *vm, sc_instance *inst, const sc_func *f, const sc_value *self_obj, const sc_value *args, int argc, int line) {
    sc_instance *saved_inst = vm->inst;
    const sc_class *saved_cls = vm->cls;
    bool saved_init = vm->in_init;
    int saved_base = vm->base, saved_top = vm->top, i, off = f->has_this ? 1 : 0;
    sc_value result, self_v = self_obj ? *self_obj : sc_nil();          /* (copied: the stack may move) */
    if (vm->depth >= SC_MAX_DEPTH) sc_fail(vm, line, "too many nested calls (endless recursion?)");
    if (off && self_v.t != V_OBJ) sc_fail(vm, line, "%s() needs an object to run on", f->name);
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
    for (i = 0; i < f->locals; ++i) vm->stack[vm->base + i] = sc_nil();
    if (off) vm->stack[vm->base] = self_v;
    for (i = 0; i < f->params && i < argc; ++i) vm->stack[vm->base + off + i] = sc_own(vm, args[i], line);
    vm->inst = inst;
    vm->cls = f->cls;
    vm->in_init = f->is_init;
    for (i = 0; i < f->params; ++i) {                          /* typed parameters */
        sc_ty ty = f->param_ty[i];
        if (!ty.t) continue;
        if (i >= argc) { sc_value d = sc_default(vm, ty, line); vm->stack[vm->base + off + i] = d; continue; }
        {
            char what[96];
            snprintf(what, sizeof(what), "%s()'s parameter '%s'", f->name, f->param_names[i]);
            sc_check_value(vm, ty, &args[i], line, what);
        }
    }
    vm->depth++;
    if (vm->nframes <= SC_MAX_DEPTH) {
        vm->frames[vm->nframes].f = f;
        vm->frames[vm->nframes].inst = inst;
        vm->frames[vm->nframes].base = vm->base;
        vm->frames[vm->nframes].line = f->line;
    }
    vm->nframes++;
    vm->ret = sc_nil();
    result = sc_exec(vm, f->body) == FLOW_RETURN ? sc_own(vm, vm->ret, line) : sc_nil();
    vm->nframes--;
    if (f->ret.t && f->ret.t != TY_VOID) {
        char what[96];
        snprintf(what, sizeof(what), "what %s() returns", f->name);
        sc_check_value(vm, f->ret, &result, f->line, what);
    }
    vm->depth--;
    vm->inst = saved_inst;
    vm->cls = saved_cls;
    vm->in_init = saved_init;
    vm->base = saved_base;
    vm->top = saved_top;
    return result;
}

static sc_value sc_call_func(sc_vm *vm, sc_instance *inst, int fn, const sc_value *args, int argc, int line) {
    return sc_invoke(vm, inst, &inst->prog->funcs[fn], NULL, args, argc, line);
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

/* ---- 3D entities (a Transform3D) ---- */
static br_transform3d *sc_t3(sc_runtime *rt, br_entity e) {
    return rt->ids3.transform3d == BR_COMPONENT_INVALID ? NULL : (br_transform3d *)br_get(rt->w, e, rt->ids3.transform3d);
}
static br_vec3 sc_need_vec3(sc_vm *vm, const sc_value *v, int line, const char *what) {
    if (v->t != V_VEC3) sc_fail(vm, line, "%s needs a vec3, not %s", what, sc_type_name(v));
    return br_v3((float)v->as.v.x, (float)v->as.v.y, (float)v->as.v.z);
}
static br_rigidbody3d *sc_body3(sc_vm *vm, br_entity e, int line, const char *what) {
    br_rigidbody3d *rb = vm->rt->body3_id == BR_COMPONENT_INVALID ? NULL : (br_rigidbody3d *)br_get(vm->rt->w, e, vm->rt->body3_id);
    if (!rb) sc_fail(vm, line, "%s needs a RigidBody3D", what);
    return rb;
}
/* A 3D entity's property (true if `name` is one). */
static bool sc_get3(sc_vm *vm, br_entity e, br_transform3d *t, const char *name, int line, sc_value *out) {
    br_world *w = vm->rt->w;
    if (strcmp(name, "position") == 0) { *out = sc_v3val(t->position); return true; }
    if (strcmp(name, "x") == 0) { *out = sc_num(t->position.x); return true; }
    if (strcmp(name, "y") == 0) { *out = sc_num(t->position.y); return true; }
    if (strcmp(name, "z") == 0) { *out = sc_num(t->position.z); return true; }
    if (strcmp(name, "rotation") == 0) { *out = sc_v3val(t->rotation); return true; }
    if (strcmp(name, "scale") == 0) { *out = sc_v3val(t->scale); return true; }
    if (strcmp(name, "global_position") == 0 || strcmp(name, "global_rotation") == 0) {
        br_vec3 p;
        br_quat q;
        br_world_transform3d(w, e, &p, &q, NULL);
        *out = name[7] == 'p' ? sc_v3val(p) : sc_v3val(br_quat_to_euler(q));
        return true;
    }
    if (strcmp(name, "forward") == 0) { *out = sc_v3val(br_forward3d(w, e)); return true; }
    if (strcmp(name, "right") == 0) { *out = sc_v3val(br_right3d(w, e)); return true; }
    if (strcmp(name, "up") == 0) { *out = sc_v3val(br_up3d(w, e)); return true; }
    if (strcmp(name, "velocity") == 0) { *out = sc_v3val(sc_body3(vm, e, line, "velocity")->velocity); return true; }
    if (strcmp(name, "angular_velocity") == 0) { *out = sc_v3val(sc_body3(vm, e, line, "angular_velocity")->angular_velocity); return true; }
    return false;
}
static bool sc_set3(sc_vm *vm, br_entity e, br_transform3d *t, const char *name, const sc_value *val, int line) {
    br_world *w = vm->rt->w;
    if (strcmp(name, "position") == 0) { t->position = sc_need_vec3(vm, val, line, "position"); return true; }
    if (strcmp(name, "x") == 0) { t->position.x = (float)sc_need_num(vm, val, line, "x"); return true; }
    if (strcmp(name, "y") == 0) { t->position.y = (float)sc_need_num(vm, val, line, "y"); return true; }
    if (strcmp(name, "z") == 0) { t->position.z = (float)sc_need_num(vm, val, line, "z"); return true; }
    if (strcmp(name, "rotation") == 0) { t->rotation = sc_need_vec3(vm, val, line, "rotation"); return true; }
    if (strcmp(name, "scale") == 0) {
        if (val->t == V_NUM) t->scale = br_v3((float)val->as.n, (float)val->as.n, (float)val->as.n);
        else t->scale = sc_need_vec3(vm, val, line, "scale");
        return true;
    }
    if (strcmp(name, "global_position") == 0) { br_set_world_position3d(w, e, sc_need_vec3(vm, val, line, "global_position")); return true; }
    if (strcmp(name, "global_rotation") == 0) {
        br_set_world_rotation3d(w, e, br_quat_from_euler(sc_need_vec3(vm, val, line, "global_rotation")));
        return true;
    }
    if (strcmp(name, "velocity") == 0) { sc_body3(vm, e, line, "velocity")->velocity = sc_need_vec3(vm, val, line, "velocity"); return true; }
    if (strcmp(name, "angular_velocity") == 0) {
        sc_body3(vm, e, line, "angular_velocity")->angular_velocity = sc_need_vec3(vm, val, line, "angular_velocity");
        return true;
    }
    if (strcmp(name, "forward") == 0 || strcmp(name, "right") == 0 || strcmp(name, "up") == 0)
        sc_fail(vm, line, "%s can't be changed (turn it with rotation or look_at())", name);
    return false;
}

static sc_value sc_field_get(sc_vm *vm, const br_field *f, const void *data) {
    const unsigned char *p = (const unsigned char *)data + f->offset;
    switch (f->type) {
    case BR_FIELD_INT: return sc_num(*(const int *)p);
    case BR_FIELD_FLOAT: return sc_num(*(const float *)p);
    case BR_FIELD_BOOL: return sc_bool(*(const bool *)p);
    case BR_FIELD_VEC2: return sc_vec(((const br_vec2 *)p)->x, ((const br_vec2 *)p)->y);
    case BR_FIELD_VEC3: return sc_vec3(((const br_vec3 *)p)->x, ((const br_vec3 *)p)->y, ((const br_vec3 *)p)->z);
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
    case BR_FIELD_VEC3: *(br_vec3 *)p = sc_need_vec3(vm, v, line, f->name); return;
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
    if (obj->t == V_VEC3) {
        if (strcmp(name, "x") == 0) return sc_num(obj->as.v.x);
        if (strcmp(name, "y") == 0) return sc_num(obj->as.v.y);
        if (strcmp(name, "z") == 0) return sc_num(obj->as.v.z);
        if (strcmp(name, "length") == 0) return sc_num(sqrt(obj->as.v.x * obj->as.v.x + obj->as.v.y * obj->as.v.y + obj->as.v.z * obj->as.v.z));
        sc_fail(vm, line, "a vec3 has x, y, z and length, not '%s'", name);
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
        {                                                      /* a 3D entity: vec3s */
            br_transform3d *t3 = sc_t3(rt, e);
            sc_value r3;
            if (t3 && sc_get3(vm, e, t3, name, line, &r3)) return r3;
            if (!t3 && (strcmp(name, "z") == 0 || strcmp(name, "forward") == 0 || strcmp(name, "right") == 0 || strcmp(name, "up") == 0))
                sc_fail(vm, line, ".%s is for 3D entities (with a Transform3D)", name);
        }
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
        {
            br_transform3d *t3 = sc_t3(rt, e);
            if (t3 && sc_set3(vm, e, t3, name, val, line)) return;
            if (!t3 && strcmp(name, "z") == 0) sc_fail(vm, line, ".z is for 3D entities (with a Transform3D)");
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
                if (other->prog->globals[g].is_const) sc_fail(vm, line, "%s is const, so it can't be changed", name);
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
    if (a[0].t == V_VEC3 && a[1].t == V_VEC3)
        return sc_vec3(a[0].as.v.x + (a[1].as.v.x - a[0].as.v.x) * t, a[0].as.v.y + (a[1].as.v.y - a[0].as.v.y) * t,
                       a[0].as.v.z + (a[1].as.v.z - a[0].as.v.z) * t);
    return sc_num(NUM(0, "lerp()") + (NUM(1, "lerp()") - a[0].as.n) * t);
}
static sc_value bi_random(ARGS) {
    double u = (double)br_rng_float(vm->rt->draw_r ? &vm->rt->draw_rng : &vm->rt->rng);
    if (n == 0) return sc_num(u);
    if (n == 1) return sc_num(u * NUM(0, "random()"));
    return sc_num(NUM(0, "random()") + u * (NUM(1, "random()") - a[0].as.n));
}
static sc_value bi_vec2(ARGS) { BR_UNUSED(n); return sc_vec(NUM(0, "vec2()"), NUM(1, "vec2()")); }
static sc_value bi_vec3(ARGS) { BR_UNUSED(n); return sc_vec3(NUM(0, "vec3()"), NUM(1, "vec3()"), NUM(2, "vec3()")); }
/* A vec2 or a vec3 (z = 0 for a vec2), for the functions that take either. */
static void sc_need_anyvec(sc_vm *vm, const sc_value *v, int line, const char *what) {
    if (v->t != V_VEC && v->t != V_VEC3) sc_fail(vm, line, "%s needs a vec2 or a vec3, not %s", what, sc_type_name(v));
}
static void sc_same_vecs(sc_vm *vm, const sc_value *a, const sc_value *b, int line, const char *what) {
    sc_need_anyvec(vm, a, line, what);
    sc_need_anyvec(vm, b, line, what);
    if (a->t != b->t) sc_fail(vm, line, "%s needs two vec2s or two vec3s, not %s and %s", what, sc_type_name(a), sc_type_name(b));
}
static double sc_vz(const sc_value *v) { return v->t == V_VEC3 ? v->as.v.z : 0; }
static sc_value bi_length(ARGS) {
    BR_UNUSED(n);
    sc_need_anyvec(vm, &a[0], line, "length()");
    return sc_num(sqrt(a[0].as.v.x * a[0].as.v.x + a[0].as.v.y * a[0].as.v.y + sc_vz(&a[0]) * sc_vz(&a[0])));
}
static sc_value bi_normalize(ARGS) {
    double x, y, z, l;
    BR_UNUSED(n);
    sc_need_anyvec(vm, &a[0], line, "normalize()");
    x = a[0].as.v.x; y = a[0].as.v.y; z = sc_vz(&a[0]); l = sqrt(x * x + y * y + z * z);
    if (a[0].t == V_VEC3) return l > 0 ? sc_vec3(x / l, y / l, z / l) : sc_vec3(0, 0, 0);
    return l > 0 ? sc_vec(x / l, y / l) : sc_vec(0, 0);
}
static sc_value bi_dot(ARGS) {
    BR_UNUSED(n);
    sc_same_vecs(vm, &a[0], &a[1], line, "dot()");
    return sc_num(a[0].as.v.x * a[1].as.v.x + a[0].as.v.y * a[1].as.v.y + sc_vz(&a[0]) * sc_vz(&a[1]));
}
static sc_value bi_cross(ARGS) {
    br_vec3 x = sc_need_vec3(vm, &a[0], line, "cross()"), y = sc_need_vec3(vm, &a[1], line, "cross()");
    BR_UNUSED(n);
    return sc_vec3((double)x.y * y.z - (double)x.z * y.y, (double)x.z * y.x - (double)x.x * y.z, (double)x.x * y.y - (double)x.y * y.x);
}
static sc_value bi_distance(ARGS) {
    double dx, dy, dz;
    BR_UNUSED(n);
    sc_same_vecs(vm, &a[0], &a[1], line, "distance()");
    dx = a[1].as.v.x - a[0].as.v.x; dy = a[1].as.v.y - a[0].as.v.y; dz = sc_vz(&a[1]) - sc_vz(&a[0]);
    return sc_num(sqrt(dx * dx + dy * dy + dz * dz));
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
    v = sc_own(vm, v, line);                                     /* (a struct: the list's own copy) */
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
/* last_input(): what the player touched last - "keyboard", "mouse", "gamepad" or "touch" ("" before anything) -
 * kept across scenes: for showing a key's name or a button's. */
/* The on-screen touch controls (touch.h): are they showing; hide or show them; change them. */
static sc_value bi_touch_controls_shown(ARGS) { br_engine *e = sc_engine(vm); UNUSED_ARGS; return sc_bool(e && br_touch_ui_visible(&e->touch)); }
static sc_value bi_show_touch_controls(ARGS) {
    br_engine *e = sc_engine(vm);
    BR_UNUSED(n); BR_UNUSED(line);
    if (e) br_touch_ui_hide(&e->touch, !sc_truthy(&a[0]));
    return sc_nil();
}
static sc_value bi_set_touch_controls(ARGS) {
    br_engine *e = sc_engine(vm);
    char why[200];
    BR_UNUSED(n);
    if (e && !br_touch_ui_configure(&e->touch, sc_need_str(vm, &a[0], line, "set_touch_controls()"), why, sizeof(why)))
        sc_fail(vm, line, "set_touch_controls(): %s", why);
    return sc_nil();
}

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
/* ---- tags (the Tags component: components.h) ---- */
static const char *sc_need_tag(sc_vm *vm, const sc_value *v, int line, const char *what) {
    const char *tag = sc_need_str(vm, v, line, what);
    if (!br_tag_valid(tag)) sc_fail(vm, line, "\"%s\" isn't a tag: up to %d letters, digits and _-.:/", tag, BR_TAG_MAX - 1);
    return tag;
}
/* find_tagged(tag, ...): every entity with all of these tags. first_tagged(tag, ...): the first, or nil. */
static uint32_t sc_tagged(sc_vm *vm, sc_value *a, int n, int line, const char *what, br_entity *out, uint32_t max) {
    const char *tags[16];
    int i;
    for (i = 0; i < n && i < 16; ++i) tags[i] = sc_need_tag(vm, &a[i], line, what);
    return br_find_tagged(vm->rt->w, tags, n < 16 ? n : 16, out, max);
}
static sc_value bi_find_tagged(ARGS) {
    sc_value list = sc_new_list(vm, line);
    uint32_t count = sc_tagged(vm, a, n, line, "find_tagged()", NULL, 0), i;
    br_entity *found;
    if (count == 0) return list;
    found = (br_entity *)br_alloc(sizeof(br_entity) * count);
    count = sc_tagged(vm, a, n, line, "find_tagged()", found, count);
    for (i = 0; i < count; ++i) sc_list_push(vm, list.as.l, sc_ent(found[i]), line);
    br_free(found);
    return list;
}
static sc_value bi_first_tagged(ARGS) {
    br_entity e = BR_ENTITY_NULL;
    return sc_tagged(vm, a, n, line, "first_tagged()", &e, 1) ? sc_ent(e) : sc_nil();
}
static sc_value bi_has_tag(ARGS) {
    br_entity e = sc_need_entity(vm, &a[0], line, "has_tag()");
    BR_UNUSED(n);
    return sc_bool(br_tag_has(vm->rt->w, e, sc_need_tag(vm, &a[1], line, "has_tag()")));
}
static sc_value bi_add_tag(ARGS) {
    br_entity e = sc_need_entity(vm, &a[0], line, "add_tag()");
    const char *tag = sc_need_tag(vm, &a[1], line, "add_tag()");
    BR_UNUSED(n);
    if (!br_tag_add(vm->rt->w, e, tag)) sc_fail(vm, line, "no room for the tag \"%s\" (an entity's tags fit in %d letters)", tag, BR_TAGS_MAX - 1);
    return sc_nil();
}
static sc_value bi_remove_tag(ARGS) {
    br_entity e = sc_need_entity(vm, &a[0], line, "remove_tag()");
    BR_UNUSED(n);
    return sc_bool(br_tag_remove(vm->rt->w, e, sc_need_tag(vm, &a[1], line, "remove_tag()")));
}
static sc_value bi_tags(ARGS) {
    br_entity e = sc_need_entity(vm, &a[0], line, "tags()");
    sc_value list = sc_new_list(vm, line);
    char tag[BR_TAG_MAX];
    int i;
    BR_UNUSED(n);
    for (i = 0; br_tag_at(vm->rt->w, e, i, tag, sizeof(tag)); ++i) sc_list_push(vm, list.as.l, sc_string(vm, tag, strlen(tag)), line);
    return list;
}
/* find_with("Health"): every entity with that component. */
static sc_value bi_find_with(ARGS) {
    const char *name = sc_need_str(vm, &a[0], line, "find_with()");
    br_component c = br_component_find(vm->rt->w, name);
    sc_value list = sc_new_list(vm, line);
    uint32_t i;
    BR_UNUSED(n);
    if (c == BR_COMPONENT_INVALID) sc_fail(vm, line, "find_with(): there is no component called %s", name);
    for (i = 0; i < vm->rt->w->pools[c].count; ++i) sc_list_push(vm, list.as.l, sc_ent(vm->rt->w->pools[c].entities[i]), line);
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
    if (n > 1 && a[1].t == V_VEC3) {                      /* 3D: the same with Transform3Ds */
        br_vec3 at = sc_need_vec3(vm, &a[1], line, "spawn() position");
        br_transform3d *t0 = (br_transform3d *)br_get(rt->w, made[0], rt->ids3.transform3d);
        br_vec3 shift = t0 ? br_v3_sub(at, t0->position) : br_v3(0, 0, 0);
        for (i = 0; i < count; ++i) {
            br_transform3d *t = (br_transform3d *)br_get(rt->w, made[i], rt->ids3.transform3d);
            br_entity pe = br_parent_of(rt->w, made[i]);
            bool root = pe == BR_ENTITY_NULL || (n > 2 && a[2].t == V_ENT && pe == a[2].as.e);
            if (t && root) t->position = br_v3_add(t->position, shift);
        }
    } else if (n > 1 && a[1].t != V_NIL) {                /* move it so the first entity lands at pos */
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
    br_vec2 f, p;
    if (a[1].t == V_VEC3) {                                  /* 3D: a RigidBody3D */
        br_vec3 f3 = sc_need_vec3(vm, &a[1], line, "apply_force()"), p3;
        if (n > 2) { p3 = sc_need_vec3(vm, &a[2], line, "apply_force() point"); br_physics3d_apply_force(vm->rt->w, e, f3, &p3); }
        else br_physics3d_apply_force(vm->rt->w, e, f3, NULL);
        return sc_nil();
    }
    f = sc_need_vec(vm, &a[1], line, "apply_force()");
    if (n > 2) { p = sc_need_vec(vm, &a[2], line, "apply_force() point"); br_physics_apply_force(vm->rt->w, e, f, &p); }
    else br_physics_apply_force(vm->rt->w, e, f, NULL);
    return sc_nil();
}
static sc_value bi_apply_impulse(ARGS) {
    br_entity e = sc_need_entity(vm, &a[0], line, "apply_impulse()");
    br_vec2 f, p;
    if (a[1].t == V_VEC3) {
        br_vec3 f3 = sc_need_vec3(vm, &a[1], line, "apply_impulse()"), p3;
        if (n > 2) { p3 = sc_need_vec3(vm, &a[2], line, "apply_impulse() point"); br_physics3d_apply_impulse(vm->rt->w, e, f3, &p3); }
        else br_physics3d_apply_impulse(vm->rt->w, e, f3, NULL);
        return sc_nil();
    }
    f = sc_need_vec(vm, &a[1], line, "apply_impulse()");
    if (n > 2) { p = sc_need_vec(vm, &a[2], line, "apply_impulse() point"); br_physics_apply_impulse(vm->rt->w, e, f, &p); }
    else br_physics_apply_impulse(vm->rt->w, e, f, NULL);
    return sc_nil();
}
static sc_value bi_raycast(ARGS) {
    br_vec2 from, to;
    br_entity ignore = n > 2 && a[2].t == V_ENT ? a[2].as.e : BR_ENTITY_NULL;
    int mask = n > 3 && a[3].t != V_NIL ? (int)NUM(3, "raycast() mask") : -1;
    br_raycast_hit hit;
    sc_value list;
    if (a[0].t == V_VEC3) {                                  /* 3D: Collider3Ds */
        br_vec3 f3 = sc_need_vec3(vm, &a[0], line, "raycast()"), t3 = sc_need_vec3(vm, &a[1], line, "raycast()");
        br_raycast3d_hit h3;
        if (!br_physics3d_raycast(vm->rt->w, f3, t3, mask, ignore, &h3)) return sc_nil();
        list = sc_new_list(vm, line);
        sc_list_push(vm, list.as.l, sc_ent(h3.entity), line);
        sc_list_push(vm, list.as.l, sc_v3val(h3.point), line);
        sc_list_push(vm, list.as.l, sc_v3val(h3.normal), line);
        return list;
    }
    from = sc_need_vec(vm, &a[0], line, "raycast()");
    to = sc_need_vec(vm, &a[1], line, "raycast()");
    if (!br_physics_raycast(vm->rt->w, from, to, mask, ignore, &hit)) return sc_nil();
    list = sc_new_list(vm, line);                         /* [entity, point, normal] */
    sc_list_push(vm, list.as.l, sc_ent(hit.entity), line);
    sc_list_push(vm, list.as.l, sc_vec(hit.point.x, hit.point.y), line);
    sc_list_push(vm, list.as.l, sc_vec(hit.normal.x, hit.normal.y), line);
    return list;
}
static sc_value bi_entity_at(ARGS) {
    br_entity e;
    int mask = n > 1 && a[1].t != V_NIL ? (int)NUM(1, "entity_at() mask") : -1;
    if (a[0].t == V_VEC3) {
        uint32_t k = br_physics3d_overlap_sphere(vm->rt->w, sc_need_vec3(vm, &a[0], line, "entity_at()"), 1e-4f, mask, &e, 1);
        return k ? sc_ent(e) : sc_nil();
    }
    e = br_physics_point(vm->rt->w, sc_need_vec(vm, &a[0], line, "entity_at()"), mask);
    return e == BR_ENTITY_NULL ? sc_nil() : sc_ent(e);
}
/* ---- 3D ---- */
static sc_value bi_overlap_sphere(ARGS) {
    br_vec3 c = sc_need_vec3(vm, &a[0], line, "overlap_sphere()");
    double r = NUM(1, "overlap_sphere()");
    sc_value list = sc_new_list(vm, line);
    br_entity found[256];
    uint32_t k, i;
    BR_UNUSED(n);
    k = br_physics3d_overlap_sphere(vm->rt->w, c, (float)r, -1, found, 256);
    for (i = 0; i < k && i < 256; ++i) sc_list_push(vm, list.as.l, sc_ent(found[i]), line);
    return list;
}
static sc_value bi_apply_torque(ARGS) {
    br_entity e = sc_need_entity(vm, &a[0], line, "apply_torque()");
    BR_UNUSED(n);
    br_physics3d_apply_torque(vm->rt->w, e, sc_need_vec3(vm, &a[1], line, "apply_torque()"));
    return sc_nil();
}
static sc_value bi_look_at(ARGS) {
    br_entity e = sc_need_entity(vm, &a[0], line, "look_at()");
    BR_UNUSED(n);
    if (!sc_t3(vm->rt, e)) sc_fail(vm, line, "look_at() turns 3D entities (with a Transform3D)");
    br_look_at3d(vm->rt->w, e, sc_need_vec3(vm, &a[1], line, "look_at()"));
    return sc_nil();
}
/* move_and_slide(e, velocity): a 3D character moved by velocity * dt, sliding along
 * what it meets; returns the velocity with the parts that went into things taken out. */
static sc_value bi_move_and_slide(ARGS) {
    br_entity e = sc_need_entity(vm, &a[0], line, "move_and_slide()");
    br_vec3 v = sc_need_vec3(vm, &a[1], line, "move_and_slide()");
    float dt = vm->rt->dt > 0 ? vm->rt->dt : 1.0f / 60.0f;
    BR_UNUSED(n);
    if (!sc_t3(vm->rt, e)) sc_fail(vm, line, "move_and_slide() moves 3D entities (with a Transform3D)");
    br_physics3d_move(vm->rt->w, e, br_v3_scale(v, dt), &v, NULL);
    return sc_v3val(v);
}
static sc_value sc_move_flag(sc_vm *vm, sc_value *a, int line, const char *what, int which) {
    br_physics3d_move_info info;
    br_entity e = sc_need_entity(vm, &a[0], line, what);
    br_physics3d_last_move(vm->rt->w, e, &info);
    return sc_bool(which == 0 ? info.on_floor : which == 1 ? info.on_wall : info.on_ceiling);
}
static sc_value bi_is_on_floor(ARGS) { BR_UNUSED(n); return sc_move_flag(vm, a, line, "is_on_floor()", 0); }
static sc_value bi_is_on_wall(ARGS) { BR_UNUSED(n); return sc_move_flag(vm, a, line, "is_on_wall()", 1); }
static sc_value bi_is_on_ceiling(ARGS) { BR_UNUSED(n); return sc_move_flag(vm, a, line, "is_on_ceiling()", 2); }
/* The scene camera, and the screen it draws on (the engine's framebuffer). */
static bool sc_camera3(sc_vm *vm, br_r3d *r3) {
    br_engine *e = sc_engine(vm);
    br_camera3d cam;
    int w = e && e->renderer.target ? e->renderer.target->width : 320, h = e && e->renderer.target ? e->renderer.target->height : 180;
    if (!br_scene_camera3d(vm->rt->w, &cam, NULL)) return false;
    memset(r3, 0, sizeof(*r3));
    r3->camera = cam;
    r3->viewport = br_recti_make(0, 0, w, h);
    r3->view = br_camera3d_view(&cam);
    r3->proj = br_camera3d_projection(&cam, (float)w / (float)h);
    r3->view_proj = br_m4_mul(r3->proj, r3->view);
    return true;
}
static sc_value bi_world_to_screen3(sc_vm *vm, const sc_value *a, int line) {   /* (world_to_screen() with a vec3) */
    br_r3d r3;
    br_vec3 sp;
    if (!sc_camera3(vm, &r3) || !br_r3d_project(&r3, sc_need_vec3(vm, &a[0], line, "world_to_screen()"), &sp)) return sc_vec(-100000, -100000);
    return sc_vec(sp.x, sp.y);
}
static sc_value bi_screen_ray(ARGS) {
    br_r3d r3;
    br_vec3 o, d;
    sc_value list;
    br_vec2 p = sc_need_vec(vm, &a[0], line, "screen_ray()");
    BR_UNUSED(n);
    if (!sc_camera3(vm, &r3)) sc_fail(vm, line, "screen_ray() needs a scene camera (an active Camera3D with a Transform3D)");
    br_r3d_ray(&r3, p, &o, &d);
    list = sc_new_list(vm, line);
    sc_list_push(vm, list.as.l, sc_v3val(o), line);
    sc_list_push(vm, list.as.l, sc_v3val(d), line);
    return list;
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

/* play_sound(path[, volume[, pitch[, pan]]]): on the sfx bus; returns the voice (0 if none was free). */
static sc_value bi_play_sound(ARGS) {
    br_engine *e = sc_engine(vm);
    const br_sound *s;
    br_play_params pp;
    if (!e) return sc_num(0);
    if (!(s = br_asset_sound(sc_need_str(vm, &a[0], line, "play_sound()")))) sc_fail(vm, line, "play_sound(): can't load %s", a[0].as.s->chars);
    pp = br_play_defaults();
    if (n > 1 && a[1].t != V_NIL) pp.volume = (float)NUM(1, "play_sound() volume");
    if (n > 2 && a[2].t != V_NIL) pp.pitch = (float)NUM(2, "play_sound() pitch");
    if (n > 3 && a[3].t != V_NIL) pp.pan = (float)NUM(3, "play_sound() pan");
    if (pp.pitch < 0.05f) pp.pitch = 0.05f;
    if (pp.pitch > 8) pp.pitch = 8;
    pp.pan = pp.pan < -1 ? -1 : pp.pan > 1 ? 1 : pp.pan;
    return sc_num((double)br_play(&e->mixer, s, &pp));
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
    char num[128];
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
    case V_VEC3:
        if (!isfinite(v->as.v.x) || !isfinite(v->as.v.y) || !isfinite(v->as.v.z)) return "a vec3 that isn't finite";
        snprintf(num, sizeof(num), "vec3(%.17g, %.17g, %.17g)", v->as.v.x, v->as.v.y, v->as.v.z);
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
    case V_OBJ: return "a struct or class object (save its fields instead)";
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
    double x, y, z;
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
    case T_NAME: {
        bool three = t.len == 4 && strncmp(t.start, "vec3", 4) == 0;
        if (t.len != 4 || (strncmp(t.start, "vec2", 4) != 0 && !three) || sc_next(L).t != T_LPAREN) return false;
        if (!sc_read_number(L, sc_next(L), &x) || sc_next(L).t != T_COMMA) return false;
        if (!sc_read_number(L, sc_next(L), &y)) return false;
        if (three && (sc_next(L).t != T_COMMA || !sc_read_number(L, sc_next(L), &z))) return false;
        if (sc_next(L).t != T_RPAREN) return false;
        *out = three ? sc_vec3(x, y, z) : sc_vec(x, y);
        return true;
    }
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

/* ---- drawing from draw() ----
 * draw_rect(), draw_text() and the rest draw straight onto the scene, in the
 * layer of the script's draw_layer, in the same space as the entities around
 * them (the world, or the screen for layers from the camera's ui_layer up).
 * They work only while draw() runs. */
static br_renderer *sc_draw_target(sc_vm *vm, int line, const char *what) {
    if (!vm->rt->draw_r) sc_fail(vm, line, "%s only works inside draw()", what);
    return vm->rt->draw_r;
}
static br_color sc_need_color(sc_vm *vm, const sc_value *v, int line, const char *what) {
    static const br_field f = { "colour", BR_FIELD_COLOR, 0, sizeof(br_color), 0, 0, NULL, 0 };
    br_color c = br_rgba(255, 255, 255, 255);
    if (v->t != V_STR && v->t != V_LIST) sc_fail(vm, line, "%s needs a colour (\"#rrggbb\", rgb() or [r, g, b]), not %s", what, sc_type_name(v));
    sc_field_set(vm, &f, &c, v, line);
    return c;
}
static sc_value bi_draw_rect(ARGS) {
    br_renderer *r = sc_draw_target(vm, line, "draw_rect()");
    br_vec2 p = sc_need_vec(vm, &a[0], line, "draw_rect() position"), size = sc_need_vec(vm, &a[1], line, "draw_rect() size");
    br_color c = sc_need_color(vm, &a[2], line, "draw_rect()");
    br_rect rect = br_rect_make(p.x, p.y, size.x, size.y);
    if (n > 3 && !sc_truthy(&a[3])) br_draw_rect(r, rect, c); else br_fill_rect(r, rect, c);
    return sc_nil();
}
static sc_value bi_draw_line(ARGS) {
    br_renderer *r = sc_draw_target(vm, line, "draw_line()");
    BR_UNUSED(n);
    br_draw_line(r, sc_need_vec(vm, &a[0], line, "draw_line()"), sc_need_vec(vm, &a[1], line, "draw_line()"), sc_need_color(vm, &a[2], line, "draw_line()"));
    return sc_nil();
}
static sc_value bi_draw_circle(ARGS) {
    br_renderer *r = sc_draw_target(vm, line, "draw_circle()");
    br_vec2 c = sc_need_vec(vm, &a[0], line, "draw_circle() centre");
    float radius = (float)NUM(1, "draw_circle() radius");
    br_color col = sc_need_color(vm, &a[2], line, "draw_circle()");
    if (n > 3 && !sc_truthy(&a[3])) br_draw_circle(r, c, radius, col); else br_fill_circle(r, c, radius, col);
    return sc_nil();
}
static sc_value bi_draw_text(ARGS) {
    br_renderer *r = sc_draw_target(vm, line, "draw_text()");
    sc_value text = sc_to_string(vm, &a[0]);
    br_vec2 p = sc_need_vec(vm, &a[1], line, "draw_text() position");
    br_color c = sc_need_color(vm, &a[2], line, "draw_text()");
    float scale = n > 3 && a[3].t != V_NIL ? (float)NUM(3, "draw_text() size") : 1;
    br_draw_text(r, text.as.s->chars, p.x, p.y, scale, c);
    return sc_nil();
}
/* draw_image(path, centre[, frame[, frame_size[, flip_x]]]) */
static sc_value bi_draw_image(ARGS) {
    br_renderer *r = sc_draw_target(vm, line, "draw_image()");
    const char *path = sc_need_str(vm, &a[0], line, "draw_image()");
    br_vec2 at = sc_need_vec(vm, &a[1], line, "draw_image() position"), fs = br_v2(0, 0);
    const br_image *img = br_asset_image(path);
    int frame = n > 2 && a[2].t != V_NIL ? (int)NUM(2, "draw_image() frame") : 0;
    bool flip = n > 4 && sc_truthy(&a[4]);
    br_recti src;
    if (!img) sc_fail(vm, line, "draw_image(): can't load %s", path);
    if (n > 3 && a[3].t != V_NIL) fs = sc_need_vec(vm, &a[3], line, "draw_image() frame_size");
    src = br_recti_make(0, 0, img->width, img->height);
    if (fs.x >= 1 && fs.y >= 1 && fs.x <= (float)img->width && fs.y <= (float)img->height) {
        int per_row = img->width / (int)fs.x, total = per_row * (img->height / (int)fs.y);
        if (frame < 0) frame = 0;
        if (frame >= total) frame = total - 1;
        src = br_recti_make((frame % per_row) * (int)fs.x, (frame / per_row) * (int)fs.y, (int)fs.x, (int)fs.y);
    }
    br_draw_sprite(r, img, &src, at, br_v2((float)src.w / 2, (float)src.h / 2), br_v2(flip ? -1.0f : 1.0f, 1), 0, BR_WHITE);
    return sc_nil();
}
/* text_size(text[, size]): how much room draw_text() (or a Label) needs. Works anywhere. */
static sc_value bi_text_size(ARGS) {
    sc_value text = sc_to_string(vm, &a[0]);
    br_vec2 m = br_measure_text(text.as.s->chars, n > 1 && a[1].t != V_NIL ? (float)NUM(1, "text_size() size") : 1);
    return sc_vec(m.x, m.y);
}

/* ---- particles (particles.h) ---- */
static sc_value bi_emit(ARGS) {
    br_entity e = sc_need_entity(vm, &a[0], line, "emit()");
    int count = n > 1 && a[1].t != V_NIL ? (int)NUM(1, "emit() count") : -1;
    if (!br_particles_emit(vm->rt->w, e, count < -1 ? 0 : count)) sc_fail(vm, line, "emit() needs an entity with Particles (and a Transform or Transform3D)");
    return sc_nil();
}
static sc_value bi_particle_count(ARGS) {
    br_entity e = sc_need_entity(vm, &a[0], line, "particle_count()");
    BR_UNUSED(n);
    return sc_num(br_particles_count(vm->rt->w, e));
}

/* ---- tweens: a property eased to a value over time ---- */
static const char *const k_ease_names[] = {
    "linear", "ease_in", "ease_out", "ease_in_out", "cubic_in", "cubic_out", "cubic_in_out",
    "sine_in", "sine_out", "sine_in_out", "back_in", "back_out", "elastic_out", "bounce_out"
};

static double sc_bounce(double t) {
    if (t < 1 / 2.75) return 7.5625 * t * t;
    if (t < 2 / 2.75) { t -= 1.5 / 2.75; return 7.5625 * t * t + 0.75; }
    if (t < 2.5 / 2.75) { t -= 2.25 / 2.75; return 7.5625 * t * t + 0.9375; }
    t -= 2.625 / 2.75;
    return 7.5625 * t * t + 0.984375;
}

static double sc_ease(int which, double t) {
    const double c1 = 1.70158, c3 = c1 + 1;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    switch (which) {
    case 1: return t * t;
    case 2: return 1 - (1 - t) * (1 - t);
    case 3: return t < 0.5 ? 2 * t * t : 1 - 2 * (1 - t) * (1 - t);
    case 4: return t * t * t;
    case 5: return 1 - (1 - t) * (1 - t) * (1 - t);
    case 6: return t < 0.5 ? 4 * t * t * t : 1 - 4 * (1 - t) * (1 - t) * (1 - t);
    case 7: return 1 - cos(t * BR_PI / 2);
    case 8: return sin(t * BR_PI / 2);
    case 9: return -(cos(BR_PI * t) - 1) / 2;
    case 10: return c3 * t * t * t - c1 * t * t;
    case 11: return 1 + c3 * (t - 1) * (t - 1) * (t - 1) + c1 * (t - 1) * (t - 1);
    case 12: return t <= 0 ? 0 : t >= 1 ? 1 : pow(2, -10 * t) * sin((t * 10 - 0.75) * (2 * BR_PI / 3)) + 1;
    case 13: return sc_bounce(t);
    default: return t;
    }
}

static int sc_need_ease(sc_vm *vm, const sc_value *v, int line, const char *what) {
    const char *name = sc_need_str(vm, v, line, what);
    int i;
    for (i = 0; i < (int)BR_ARRAY_COUNT(k_ease_names); ++i)
        if (strcmp(name, k_ease_names[i]) == 0) return i;
    sc_fail(vm, line, "%s: there is no ease \"%s\" (linear, ease_in, ease_out, ease_in_out, cubic_..., sine_..., back_in, back_out, elastic_out, bounce_out)", what, name);
    return 0;
}

static bool sc_parse_colour(const char *s, double out[4]) {
    unsigned v[4] = { 0, 0, 0, 255 };
    size_t n = strlen(s), i;
    if (s[0] != '#' || (n != 7 && n != 9)) return false;
    for (i = 1; i < n; ++i) if (!isxdigit((unsigned char)s[i])) return false;
    for (i = 0; i < (n - 1) / 2; ++i) {
        char hex[3];
        hex[0] = s[1 + i * 2]; hex[1] = s[2 + i * 2]; hex[2] = 0;
        v[i] = (unsigned)strtoul(hex, NULL, 16);
    }
    for (i = 0; i < 4; ++i) out[i] = v[i];
    return true;
}

/* The value now (kind -1: whatever it is, which sets *kind), as numbers. */
static void sc_tween_read(sc_vm *vm, const sc_tween *tw, int *kind, double out[4], int line) {
    sc_value obj = sc_ent(tw->e), v;
    if (tw->comp[0]) obj = sc_get_member(vm, &obj, tw->comp, line);
    if (obj.t == V_NIL) sc_fail(vm, line, "tween(): it has no %s", tw->comp);
    v = sc_get_member(vm, &obj, tw->prop, line);
    if (v.t == V_NUM) { *kind = 0; out[0] = v.as.n; }
    else if (v.t == V_VEC) { *kind = 1; out[0] = v.as.v.x; out[1] = v.as.v.y; }
    else if (v.t == V_VEC3) { *kind = 2; out[0] = v.as.v.x; out[1] = v.as.v.y; out[2] = v.as.v.z; }
    else if (v.t == V_STR && sc_parse_colour(v.as.s->chars, out)) *kind = 3;
    else sc_fail(vm, line, "tween(): %s is %s, which can't be eased (numbers, vec2s, vec3s and colours can)", tw->prop, sc_type_name(&v));
}

static void sc_tween_write(sc_vm *vm, const sc_tween *tw, const double v[4], int line) {
    sc_value obj = sc_ent(tw->e), val;
    if (tw->comp[0]) obj = sc_get_member(vm, &obj, tw->comp, line);
    if (obj.t == V_NIL) sc_fail(vm, line, "tween(): it has no %s", tw->comp);
    if (tw->kind == 0) val = sc_num(v[0]);
    else if (tw->kind == 1) val = sc_vec(v[0], v[1]);
    else if (tw->kind == 2) val = sc_vec3(v[0], v[1], v[2]);
    else {
        char text[16];
        snprintf(text, sizeof(text), "#%02x%02x%02x%02x", clamp_byte(v[0]), clamp_byte(v[1]), clamp_byte(v[2]), clamp_byte(v[3]));
        val = sc_string(vm, text, strlen(text));
    }
    sc_set_member(vm, &obj, tw->prop, &val, line);
}

static sc_tween *sc_tween_find(sc_runtime *rt, int id) {
    int i;
    for (i = 0; i < rt->tween_count; ++i)
        if (rt->tweens[i].id == id) return &rt->tweens[i];
    return NULL;
}

static sc_tween *sc_need_tween(sc_vm *vm, const sc_value *v, int line, const char *what) {
    sc_tween *tw = sc_tween_find(vm->rt, (int)sc_need_num(vm, v, line, what));
    return tw;                                                 /* (NULL: finished already; nothing to do) */
}

/* tween(e, "position", vec2(100, 50), 0.5[, "ease_out"[, delay]]) */
static sc_value bi_tween(ARGS) {
    sc_runtime *rt = vm->rt;
    sc_tween tw;
    const char *path;
    const char *dot;
    double now[4];
    int i, now_kind;
    memset(&tw, 0, sizeof(tw));
    tw.e = sc_need_entity(vm, &a[0], line, "tween()");
    if (!br_entity_alive(rt->w, tw.e)) sc_fail(vm, line, "tween(): that entity was destroyed");
    path = sc_need_str(vm, &a[1], line, "tween() property");
    if ((dot = strchr(path, '.')) != NULL) {                    /* "Shape.color": a component's field */
        if ((size_t)(dot - path) >= sizeof(tw.comp) || strlen(dot + 1) >= sizeof(tw.prop)) sc_fail(vm, line, "tween(): \"%s\" is too long", path);
        memcpy(tw.comp, path, (size_t)(dot - path));
        snprintf(tw.prop, sizeof(tw.prop), "%s", dot + 1);
        if (!isupper((unsigned char)tw.comp[0])) sc_fail(vm, line, "tween(): \"%s\": a component's name starts with a capital (\"Shape.color\")", path);
    } else {
        if (strlen(path) >= sizeof(tw.prop)) sc_fail(vm, line, "tween(): \"%s\" is too long", path);
        snprintf(tw.prop, sizeof(tw.prop), "%s", path);
    }
    sc_tween_read(vm, &tw, &now_kind, now, line);              /* (checks it's there, and can be eased) */
    tw.kind = now_kind;
    if (tw.kind == 0) tw.to[0] = sc_need_num(vm, &a[2], line, "tween() target");
    else if (tw.kind == 1) {
        br_vec2 v;
        if (a[2].t == V_NUM) v = br_v2((float)a[2].as.n, (float)a[2].as.n);       /* (scale: a number for both) */
        else v = sc_need_vec(vm, &a[2], line, "tween() target");
        tw.to[0] = v.x; tw.to[1] = v.y;
    } else if (tw.kind == 2) {
        br_vec3 v;
        if (a[2].t == V_NUM) v = br_v3((float)a[2].as.n, (float)a[2].as.n, (float)a[2].as.n);
        else v = sc_need_vec3(vm, &a[2], line, "tween() target");
        tw.to[0] = v.x; tw.to[1] = v.y; tw.to[2] = v.z;
    } else if (!sc_parse_colour(sc_need_str(vm, &a[2], line, "tween() target"), tw.to))
        sc_fail(vm, line, "tween(): \"%s\" isn't a colour (use \"#rrggbb\" or rgb())", a[2].as.s->chars);
    tw.dur = (float)NUM(3, "tween() seconds");
    if (tw.dur < 0) tw.dur = 0;
    tw.ease = n > 4 && a[4].t != V_NIL ? sc_need_ease(vm, &a[4], line, "tween() ease") : 0;
    tw.delay = n > 5 && a[5].t != V_NIL ? (float)NUM(5, "tween() delay") : 0;
    tw.owner = vm->inst ? vm->inst->e : BR_ENTITY_NULL;
    for (i = 0; i < rt->tween_count; ++i) {                     /* a new tween of the same thing replaces the old */
        const sc_tween *o = &rt->tweens[i];
        if (o->e == tw.e && strcmp(o->comp, tw.comp) == 0 && strcmp(o->prop, tw.prop) == 0 && o->delay <= 0 && tw.delay <= 0) {
            rt->tweens[i] = rt->tweens[--rt->tween_count];
            break;
        }
    }
    if (rt->tween_count == rt->tween_cap) {
        int cap = rt->tween_cap ? rt->tween_cap * 2 : 16;
        sc_tween *g = (sc_tween *)br_realloc(rt->tweens, (size_t)cap * sizeof(sc_tween));
        if (!g) sc_fail(vm, line, "out of memory");
        rt->tweens = g;
        rt->tween_cap = cap;
    }
    tw.id = ++rt->next_tween;
    rt->tweens[rt->tween_count++] = tw;
    return sc_num(tw.id);
}

static sc_value bi_tween_repeat(ARGS) {
    sc_tween *tw = sc_need_tween(vm, &a[0], line, "tween_repeat()");
    int times = (int)NUM(1, "tween_repeat() times");
    bool yoyo = n > 2 && sc_truthy(&a[2]);
    if (tw) { tw->repeat = times < 0 ? -1 : times; tw->yoyo = yoyo; }
    return sc_nil();
}

static sc_value bi_tween_then(ARGS) {
    sc_tween *tw = sc_need_tween(vm, &a[0], line, "tween_then()");
    const char *fn = sc_need_str(vm, &a[1], line, "tween_then() function");
    int f = -1;
    BR_UNUSED(n);
    if (!vm->inst || !vm->inst->prog || (f = sc_find_func(vm->inst->prog, fn)) < 0)
        sc_fail(vm, line, "tween_then(): this script has no function %s()", fn);
    if (vm->inst->prog->funcs[f].params != 0) sc_fail(vm, line, "tween_then(): %s() should take no arguments", fn);
    if (strlen(fn) >= sizeof(tw->then)) sc_fail(vm, line, "tween_then(): %s is too long a name", fn);
    if (tw) { snprintf(tw->then, sizeof(tw->then), "%s", fn); tw->owner = vm->inst->e; }
    return sc_nil();
}

/* stop_tween(id) or stop_tween(e): it stays where it got to */
static sc_value bi_stop_tween(ARGS) {
    sc_runtime *rt = vm->rt;
    int i;
    BR_UNUSED(n);
    if (a[0].t == V_ENT) {
        for (i = rt->tween_count; i-- > 0;)
            if (rt->tweens[i].e == a[0].as.e) rt->tweens[i] = rt->tweens[--rt->tween_count];
    } else {
        int id = (int)NUM(0, "stop_tween()");
        for (i = 0; i < rt->tween_count; ++i)
            if (rt->tweens[i].id == id) { rt->tweens[i] = rt->tweens[--rt->tween_count]; break; }
    }
    return sc_nil();
}

static sc_value bi_is_tweening(ARGS) {
    sc_runtime *rt = vm->rt;
    int i;
    if (a[0].t == V_NUM) return sc_bool(sc_tween_find(rt, (int)a[0].as.n) != NULL);
    {
        br_entity e = sc_need_entity(vm, &a[0], line, "is_tweening()");
        const char *path = n > 1 && a[1].t != V_NIL ? sc_need_str(vm, &a[1], line, "is_tweening() property") : NULL;
        for (i = 0; i < rt->tween_count; ++i) {
            const sc_tween *tw = &rt->tweens[i];
            char full[70];
            if (tw->e != e) continue;
            if (!path) return sc_bool(true);
            snprintf(full, sizeof(full), "%s%s%s", tw->comp, tw->comp[0] ? "." : "", tw->prop);
            if (strcmp(full, path) == 0) return sc_bool(true);
        }
    }
    return sc_bool(false);
}

static sc_value bi_ease(ARGS) {
    int which = sc_need_ease(vm, &a[0], line, "ease()");
    BR_UNUSED(n);
    return sc_num(sc_ease(which, NUM(1, "ease() t")));
}

static bool sc_run(sc_runtime *rt, sc_instance *inst, int fn, const sc_value *args, int argc, sc_value *result);

/* One tween, `step` seconds on. False: it can't go on (its entity lost the component, say). */
static bool sc_tween_advance(sc_runtime *rt, sc_tween *tw, float step, bool *finished) {
    sc_vm vm;
    jmp_buf jb;
    volatile bool ok = true;
    memset(&vm, 0, offsetof(sc_vm, frames));
    vm.rt = rt;
    vm.budget = SC_BUDGET;
    vm.fail = &jb;
    *finished = false;
    if (setjmp(jb) == 0) {
        double v[4], t;
        int k, kind;
        if (!tw->started) {                                      /* from wherever it is when it starts */
            sc_tween_read(&vm, tw, &kind, tw->from, 0);
            if (kind != tw->kind) sc_fail(&vm, 0, "tween(): %s changed type", tw->prop);
            tw->started = true;
        }
        tw->time += step;
        t = tw->dur > 0 ? tw->time / tw->dur : 1;
        if (t >= 1 - 1e-5) { t = 1; *finished = true; }       /* (sums of 1/60 fall just short) */
        t = sc_ease(tw->ease, tw->backward ? 1 - t : t);
        for (k = 0; k < 4; ++k) v[k] = tw->from[k] + (tw->to[k] - tw->from[k]) * t;
        sc_tween_write(&vm, tw, v, 0);
    } else {
        ok = false;
    }
    return ok;
}

/* Each fixed step, after the scripts' updates: every tween moves on by dt. */
static void sc_tweens_step(sc_runtime *rt, float dt) {
    typedef struct done_call { br_entity owner; char fn[32]; } done_call;
    done_call done[32];
    int ndone = 0, i = 0;
    if (!rt->tween_count) return;
    rt->busy++;
    while (i < rt->tween_count) {
        sc_tween *tw = &rt->tweens[i];
        bool finished;
        float step = dt;
        if (!br_entity_alive(rt->w, tw->e) ||                   /* gone, or lost the component: it just stops */
            (tw->comp[0] && !br_has(rt->w, tw->e, br_component_find(rt->w, tw->comp)))) {
            rt->tweens[i] = rt->tweens[--rt->tween_count];
            continue;
        }
        if (tw->delay > 0) {
            tw->delay -= step;
            if (tw->delay > 0) { ++i; continue; }
            step = -tw->delay;                                   /* the rest of the step, after the wait */
            tw->delay = 0;
        }
        if (!sc_tween_advance(rt, tw, step, &finished)) { rt->tweens[i] = rt->tweens[--rt->tween_count]; continue; }
        if (finished) {
            if (tw->repeat != 0) {                               /* again (back, with yoyo) */
                if (tw->repeat > 0) tw->repeat--;
                tw->time = 0;
                if (tw->yoyo) tw->backward = !tw->backward;
            } else {
                if (tw->then[0] && ndone < (int)BR_ARRAY_COUNT(done)) {
                    done[ndone].owner = tw->owner;
                    memcpy(done[ndone].fn, tw->then, sizeof(done[ndone].fn));
                    ++ndone;
                }
                rt->tweens[i] = rt->tweens[--rt->tween_count];
                continue;
            }
        }
        ++i;
    }
    rt->busy--;
    for (i = 0; i < ndone; ++i) {                                /* then their tween_then()s, which may start more */
        sc_instance *inst = br_entity_alive(rt->w, done[i].owner) ? sc_instance_of(rt, done[i].owner) : NULL;
        int f;
        if (!inst || inst->failed || !inst->started || !inst->prog || (f = sc_find_func(inst->prog, done[i].fn)) < 0) continue;
        sc_run(rt, inst, f, NULL, 0, NULL);
    }
}

/* ---- cameras (the Camera component: components.h) ---- */
static sc_value bi_camera(ARGS) {
    br_entity e = br_camera_current(vm->rt->w);
    UNUSED_ARGS;
    return e == BR_ENTITY_NULL ? sc_nil() : sc_ent(e);
}
static sc_value bi_shake_camera(ARGS) {
    BR_UNUSED(n);
    br_camera_shake(vm->rt->w, (float)NUM(0, "shake_camera() strength"), (float)NUM(1, "shake_camera() seconds"));
    return sc_nil();
}
static sc_value bi_snap_camera(ARGS) { UNUSED_ARGS; br_camera_snap(vm->rt->w); return sc_nil(); }
static void sc_view(sc_vm *vm, br_camera2d *cam) {
    br_engine *e = sc_engine(vm);
    int ui;
    br_camera_view(vm->rt->w, e ? e->framebuffer.width : 320, e ? e->framebuffer.height : 180, cam, &ui);
}
static sc_value bi_screen_to_world(ARGS) {
    br_camera2d cam;
    br_renderer r;
    br_vec2 p = sc_need_vec(vm, &a[0], line, "screen_to_world()");
    BR_UNUSED(n);
    sc_view(vm, &cam);
    memset(&r, 0, sizeof(r));
    br_begin_camera(&r, &cam);
    p = br_screen_to_world(&r, p);
    return sc_vec(p.x, p.y);
}
static sc_value bi_world_to_screen(ARGS) {
    br_camera2d cam;
    br_renderer r;
    br_vec2 p;
    BR_UNUSED(n);
    if (a[0].t == V_VEC3) return bi_world_to_screen3(vm, a, line);   /* 3D: through the scene's Camera3D */
    p = sc_need_vec(vm, &a[0], line, "world_to_screen()");
    sc_view(vm, &cam);
    memset(&r, 0, sizeof(r));
    br_begin_camera(&r, &cam);
    p = br_world_to_screen(&r, p);
    return sc_vec(p.x, p.y);
}

/* play_animation(e, frames[, fps[, loop]]): its Animation plays these frames (from the first, unless they're playing already). */
/* A 3D model's animation: its Animator plays the clip of that name. */
static br_animator *sc_animator_for(sc_vm *vm, br_entity e, bool make) {
    br_component ac = br_component_find(vm->rt->w, "Animator");
    br_animator *am;
    if (ac == BR_COMPONENT_INVALID) return NULL;
    if ((am = (br_animator *)br_get(vm->rt->w, e, ac)) != NULL || !make) return am;
    if (vm->rt->ids3.mesh == BR_COMPONENT_INVALID || !br_has(vm->rt->w, e, vm->rt->ids3.mesh) || br_has(vm->rt->w, e, vm->rt->ids.sprite)) return NULL;
    return (br_animator *)br_add(vm->rt->w, e, ac, NULL);
}

static sc_value bi_play_animation(ARGS) {
    br_entity e = sc_need_entity(vm, &a[0], line, "play_animation()");
    const char *frames = sc_need_str(vm, &a[1], line, "play_animation() frames");
    br_animation *an;
    br_animator *am = sc_animator_for(vm, e, true);
    if (am) {                                                 /* 3D: (e, clip[, speed[, loop]]) */
        if (strlen(frames) >= sizeof(am->clip)) sc_fail(vm, line, "play_animation(): \"%s\" is too long a clip name", frames);
        if (br_animator_clip_count(vm->rt->w, e) > 0 && br_model_find_clip(br_asset_model(((const br_mesh3d *)br_get(vm->rt->w, e, vm->rt->ids3.mesh))->file), frames) < 0) {
            int k, c = br_animator_clip_count(vm->rt->w, e);
            char names[200] = "";
            for (k = 0; k < c && strlen(names) < 150; ++k) {
                strcat(names, k ? ", " : "");
                strncat(names, br_animator_clip_name(vm->rt->w, e, k), 40);
            }
            sc_fail(vm, line, "play_animation(): the model has no animation \"%s\" (it has %s)", frames, names);
        }
        if (n > 2 && a[2].t != V_NIL) am->speed = (float)NUM(2, "play_animation() speed");
        if (n > 3 && a[3].t != V_NIL) am->loop = sc_truthy(&a[3]);
        if (strcmp(am->clip, frames) != 0) snprintf(am->clip, sizeof(am->clip), "%s", frames);
        else if (!am->playing) { am->time = am->speed < 0 ? br_animator_clip_length(vm->rt->w, e, frames) : 0; am->playing = true; }
        return sc_nil();
    }
    if (br_animation_parse(frames, NULL, 0) <= 0) sc_fail(vm, line, "play_animation(): \"%s\" isn't a list of frames (like \"0-3\" or \"4,5,6\")", frames);
    if (strlen(frames) >= BR_ANIM_FRAMES_MAX) sc_fail(vm, line, "play_animation(): the list of frames is too long");
    if (!(an = (br_animation *)br_get(vm->rt->w, e, vm->rt->ids.animation)))
        an = (br_animation *)br_add(vm->rt->w, e, vm->rt->ids.animation, NULL);
    if (!an) sc_fail(vm, line, "out of memory");
    if (n > 2 && a[2].t != V_NIL) an->fps = (float)NUM(2, "play_animation() fps");
    if (n > 3 && a[3].t != V_NIL) an->loop = sc_truthy(&a[3]);
    if (strcmp(an->frames, frames) != 0 || !an->playing) {
        snprintf(an->frames, sizeof(an->frames), "%s", frames);
        an->started[0] = 0;                                 /* from its first frame, at the next step */
        an->playing = true;
        {   /* and show that frame now */
            int first;
            br_sprite *sp = (br_sprite *)br_get(vm->rt->w, e, vm->rt->ids.sprite);
            if (sp && br_animation_parse(frames, &first, 1) > 0) sp->frame = first;
        }
    }
    return sc_nil();
}

/* ---- sounds: voices, music and tones ---- */
static br_mixer *sc_mixer(sc_vm *vm) { br_engine *e = sc_engine(vm); return e ? &e->mixer : NULL; }
static br_voice sc_voice(sc_vm *vm, const sc_value *v, int line, const char *what) {
    double d = sc_need_num(vm, v, line, what);
    return d > 0 ? (br_voice)d : 0;
}
/* stop_animation(e): holds where it is (a 3D model's Animator, or a 2D Animation). */
static sc_value bi_stop_animation(ARGS) {
    br_entity e = sc_need_entity(vm, &a[0], line, "stop_animation()");
    br_animator *am = sc_animator_for(vm, e, false);
    br_animation *an = (br_animation *)br_get(vm->rt->w, e, vm->rt->ids.animation);
    BR_UNUSED(n);
    if (am) am->playing = false;
    if (an) an->playing = false;
    return sc_nil();
}
/* animation_clips(e): the names of a 3D model's animations. */
static sc_value bi_animation_clips(ARGS) {
    br_entity e = sc_need_entity(vm, &a[0], line, "animation_clips()");
    sc_value list = sc_new_list(vm, line);
    int k, c = br_animator_clip_count(vm->rt->w, e);
    BR_UNUSED(n);
    for (k = 0; k < c; ++k) {
        const char *name = br_animator_clip_name(vm->rt->w, e, k);
        sc_list_push(vm, list.as.l, sc_string(vm, name, strlen(name)), line);
    }
    return list;
}
static sc_value bi_stop_sound(ARGS) { br_mixer *m = sc_mixer(vm); br_voice v = sc_voice(vm, &a[0], line, "stop_sound()"); BR_UNUSED(n); if (m && v) br_voice_stop(m, v); return sc_nil(); }
static sc_value bi_sound_playing(ARGS) { br_mixer *m = sc_mixer(vm); br_voice v = sc_voice(vm, &a[0], line, "sound_playing()"); BR_UNUSED(n); return sc_bool(m && v && br_voice_playing(m, v)); }
static sc_value bi_set_sound_volume(ARGS) { br_mixer *m = sc_mixer(vm); br_voice v = sc_voice(vm, &a[0], line, "set_sound_volume()"); float x = (float)NUM(1, "set_sound_volume()"); BR_UNUSED(n); if (m && v) br_voice_set_volume(m, v, x < 0 ? 0 : x); return sc_nil(); }
static sc_value bi_set_sound_pitch(ARGS) { br_mixer *m = sc_mixer(vm); br_voice v = sc_voice(vm, &a[0], line, "set_sound_pitch()"); float x = (float)NUM(1, "set_sound_pitch()"); BR_UNUSED(n); if (m && v) br_voice_set_pitch(m, v, x < 0.05f ? 0.05f : x > 8 ? 8 : x); return sc_nil(); }
static sc_value bi_set_sound_pan(ARGS) { br_mixer *m = sc_mixer(vm); br_voice v = sc_voice(vm, &a[0], line, "set_sound_pan()"); float x = (float)NUM(1, "set_sound_pan()"); BR_UNUSED(n); if (m && v) br_voice_set_pan(m, v, x < -1 ? -1 : x > 1 ? 1 : x); return sc_nil(); }
static sc_value bi_pause_sound(ARGS) { br_mixer *m = sc_mixer(vm); br_voice v = sc_voice(vm, &a[0], line, "pause_sound()"); BR_UNUSED(n); if (m && v) br_voice_set_paused(m, v, sc_truthy(&a[1])); return sc_nil(); }
/* stop_sounds([bus]): every sound playing (or every one on a bus), music included. */
static sc_value bi_stop_sounds(ARGS) {
    br_mixer *m = sc_mixer(vm);
    int bus = n > 0 && a[0].t != V_NIL ? sc_bus_named(vm, &a[0], line, "stop_sounds()") : -1, i;
    if (!m) return sc_nil();
    for (i = 0; i < BR_AUDIO_MAX_VOICES; ++i) {
        br_voice_state *vs = &m->voices[i];
        if (vs->active && (bus < 0 || vs->params.bus == bus)) vs->stopping = true;
    }
    return sc_nil();
}
static sc_value bi_sound_length(ARGS) {
    const br_sound *s = br_asset_sound(sc_need_str(vm, &a[0], line, "sound_length()"));
    BR_UNUSED(n);
    if (!s) sc_fail(vm, line, "sound_length(): can't load %s", a[0].as.s->chars);
    return sc_num(br_sound_duration(s));
}

/* The music: one track at a time on the music bus, kept playing across scenes, faded in and out. */
static struct {
    char path[BR_PATH_MAX];
    br_mixer *mixer;
    br_voice voice, old;              /* the track, and the one fading out under it */
    float volume;                     /* the track's own volume (the bus's is on top) */
    double fade_start, fade_len;      /* engine time */
    float old_volume;
    double old_start, old_len;
} g_music;

static void sc_music_tick(double now) {
    br_mixer *m = g_music.mixer;
    if (!m) return;
    if (g_music.voice) {
        float k = g_music.fade_len > 0 ? (float)((now - g_music.fade_start) / g_music.fade_len) : 1;
        if (!br_voice_playing(m, g_music.voice)) { g_music.voice = 0; g_music.path[0] = 0; }
        else br_voice_set_volume(m, g_music.voice, g_music.volume * (k < 0 ? 0 : k > 1 ? 1 : k));
    }
    if (g_music.old) {
        float k = g_music.old_len > 0 ? (float)((now - g_music.old_start) / g_music.old_len) : 1;
        if (k >= 1 || !br_voice_playing(m, g_music.old)) { br_voice_stop(m, g_music.old); g_music.old = 0; }
        else br_voice_set_volume(m, g_music.old, g_music.old_volume * (1 - k));
    }
}
static void sc_music_out(double now, double fade) {
    if (!g_music.voice || !g_music.mixer) { g_music.voice = 0; g_music.path[0] = 0; return; }
    if (g_music.old) br_voice_stop(g_music.mixer, g_music.old);
    if (fade <= 0) { br_voice_stop(g_music.mixer, g_music.voice); g_music.old = 0; }
    else {
        const br_voice_state *vs = &g_music.mixer->voices[(g_music.voice & 0xFFu) % BR_AUDIO_MAX_VOICES];
        g_music.old = g_music.voice;
        g_music.old_volume = vs->params.volume;
        g_music.old_start = now;
        g_music.old_len = fade;
    }
    g_music.voice = 0;
    g_music.path[0] = 0;
}
/* play_music(path[, volume[, fade]]): loops it on the music bus, fading in over `fade`
 * seconds while any other track fades out. The same track again only changes its volume. */
static sc_value bi_play_music(ARGS) {
    br_engine *e = sc_engine(vm);
    const char *path = sc_need_str(vm, &a[0], line, "play_music()");
    float volume = n > 1 && a[1].t != V_NIL ? (float)NUM(1, "play_music() volume") : 1;
    double fade = n > 2 && a[2].t != V_NIL ? NUM(2, "play_music() fade") : 0;
    const br_sound *s;
    br_play_params pp;
    if (!e) return sc_nil();
    if (!(s = br_asset_sound(path))) sc_fail(vm, line, "play_music(): can't load %s", path);
    if (g_music.mixer != &e->mixer) { memset(&g_music, 0, sizeof(g_music)); g_music.mixer = &e->mixer; }
    sc_music_tick(e->time);
    if (g_music.voice && strcmp(g_music.path, path) == 0) { g_music.volume = volume < 0 ? 0 : volume; return sc_nil(); }
    sc_music_out(e->time, fade);
    pp = br_play_defaults();
    pp.loop = true;
    pp.bus = BR_BUS_MUSIC;
    pp.volume = fade > 0 ? 0 : volume;
    g_music.voice = br_play(&e->mixer, s, &pp);
    snprintf(g_music.path, sizeof(g_music.path), "%s", g_music.voice ? path : "");
    g_music.volume = volume < 0 ? 0 : volume;
    g_music.fade_start = e->time;
    g_music.fade_len = fade;
    return sc_nil();
}
static sc_value bi_stop_music(ARGS) {
    br_engine *e = sc_engine(vm);
    if (e && g_music.mixer == &e->mixer) sc_music_out(e->time, n > 0 && a[0].t != V_NIL ? NUM(0, "stop_music() fade") : 0);
    BR_UNUSED(line);
    return sc_nil();
}
static sc_value bi_music(ARGS) { UNUSED_ARGS; return sc_string(vm, g_music.path, strlen(g_music.path)); }
const char *br_script_music(void) { return g_music.path; }

/* play_tone(wave, frequency, seconds[, volume]): a tone made on the spot ("sine", "square",
 * "triangle", "saw" or "noise") on the sfx bus, for blips with no sound file. Returns the voice. */
#define SC_TONES 48
static struct { br_sound sound; int wave; float freq, secs, vol; uint32_t used; br_mixer *mixer; } g_tones[SC_TONES];
static uint32_t g_tone_clock;
static sc_value bi_play_tone(ARGS) {
    static const char *const waves[] = { "sine", "square", "triangle", "saw", "noise" };
    br_mixer *m = sc_mixer(vm);
    const char *wave = sc_need_str(vm, &a[0], line, "play_tone() wave");
    float f = (float)NUM(1, "play_tone() frequency"), secs = (float)NUM(2, "play_tone() seconds");
    float vol = n > 3 && a[3].t != V_NIL ? (float)NUM(3, "play_tone() volume") : 0.5f;
    int w = -1, i, slot = -1;
    for (i = 0; i < 5; ++i) if (sc_strieq(wave, waves[i])) w = i;
    if (w < 0) sc_fail(vm, line, "play_tone(): there is no wave called \"%s\" (sine, square, triangle, saw, noise)", wave);
    if (f < 10 || f > 20000) sc_fail(vm, line, "play_tone(): %g Hz is outside 10 to 20000", (double)f);
    if (secs <= 0 || secs > 10) sc_fail(vm, line, "play_tone(): a tone lasts more than 0 and up to 10 seconds");
    vol = vol < 0 ? 0 : vol > 1 ? 1 : vol;
    if (!m) return sc_num(0);
    for (i = 0; i < SC_TONES; ++i)
        if (g_tones[i].sound.samples && g_tones[i].wave == w && g_tones[i].freq == f && g_tones[i].secs == secs && g_tones[i].vol == vol) { slot = i; break; }
    if (slot < 0) {                                   /* make it, in a free slot or the one used longest ago */
        uint32_t oldest = 0xFFFFFFFFu;
        for (i = 0; i < SC_TONES; ++i) {
            if (!g_tones[i].sound.samples) { slot = i; break; }
            if (g_tones[i].used < oldest) { oldest = g_tones[i].used; slot = i; }
        }
        if (g_tones[slot].sound.samples) {
            if (g_tones[slot].mixer) br_mixer_stop_sound(g_tones[slot].mixer, &g_tones[slot].sound);
            br_sound_destroy(&g_tones[slot].sound);
        }
        if (!br_sound_generate(&g_tones[slot].sound, (br_waveform)w, f, secs, vol, m->sample_rate)) { memset(&g_tones[slot], 0, sizeof(g_tones[0])); return sc_num(0); }
        g_tones[slot].wave = w; g_tones[slot].freq = f; g_tones[slot].secs = secs; g_tones[slot].vol = vol;
    }
    g_tones[slot].used = ++g_tone_clock;
    g_tones[slot].mixer = m;
    return sc_num((double)br_play(m, &g_tones[slot].sound, NULL));
}

/* ---- messages: broadcast(fn, args...) calls fn in every script of the scene that has it ---- */
static sc_value bi_broadcast(ARGS) {
    sc_runtime *rt = vm->rt;
    const char *fn_name = sc_need_str(vm, &a[0], line, "broadcast()");
    br_entity *targets;
    int count = 0, i, called = 0;
    br_engine *e = sc_engine(vm);
    if (!rt->instance_count) return sc_num(0);
    if (!(targets = (br_entity *)br_alloc(sizeof(br_entity) * (size_t)rt->instance_count))) sc_fail(vm, line, "out of memory");
    for (i = 0; i < rt->instance_count; ++i) targets[count++] = rt->instances[i]->e;    /* (calls may add or remove scripts) */
    for (i = 0; i < count; ++i) {
        sc_instance *t = sc_instance_of(rt, targets[i]);
        int fn;
        if (!t || !t->prog || t->failed || !t->ready || (fn = sc_find_func(t->prog, fn_name)) < 0) continue;
        sc_call_func(vm, t, fn, a + 1, n - 1, line);
        ++called;
    }
    br_free(targets);
    if (e) {                                          /* and on the engine's message bus, as "script.<fn>", for C code */
        char type[80];
        double nums[6];
        int k, m = 0;
        snprintf(type, sizeof(type), "script.%s", fn_name);
        memset(nums, 0, sizeof(nums));
        for (k = 1; k < n && m < 6; ++k) if (a[k].t == V_NUM) nums[m++] = a[k].as.n;
        if (br_events_find_type(&e->events, type)) br_events_post_data(&e->events, br_events_find_type(&e->events, type), nums, sizeof(nums));
    }
    return sc_num(called);
}

/* ---- scenes on a stack, and values that outlive them ---- */
static sc_value bi_push_scene(ARGS) {
    sc_runtime *rt = vm->rt;
    snprintf(rt->push_request, sizeof(rt->push_request), "%s", sc_need_str(vm, &a[0], line, "push_scene()"));
    rt->push_transparent = n > 1 && a[1].t != V_NIL ? sc_truthy(&a[1]) : true;
    rt->push_update_below = n > 2 && sc_truthy(&a[2]);
    return sc_nil();
}
static sc_value bi_pop_scene(ARGS) { UNUSED_ARGS; vm->rt->pop_request = true; return sc_nil(); }

#define SC_GLOBALS_MAX 256
static struct { char name[64]; char *text; } g_globals[SC_GLOBALS_MAX];
static int g_global_count;
static int sc_global_slot(const char *name) {
    int i;
    for (i = 0; i < g_global_count; ++i) if (strcmp(g_globals[i].name, name) == 0) return i;
    return -1;
}
static const char *sc_global_name(sc_vm *vm, const sc_value *v, int line, const char *what) {
    const char *name = sc_need_str(vm, v, line, what);
    if (!name[0] || strlen(name) > 63) sc_fail(vm, line, "%s: a global's name is 1 to 63 characters", what);
    return name;
}
/* set_global(name, value): kept while the game runs (load_scene and push_scene don't lose it);
 * the same values save_data() takes. */
static sc_value bi_set_global(ARGS) {
    const char *name = sc_global_name(vm, &a[0], line, "set_global()"), *why;
    sc_text t;
    int slot = sc_global_slot(name);
    BR_UNUSED(n);
    memset(&t, 0, sizeof(t));
    if ((why = sc_write_value(&t, &a[1], 0)) != NULL) { br_free(t.p); sc_fail(vm, line, "set_global(): can't keep %s", why); }
    if (t.failed) { br_free(t.p); sc_fail(vm, line, "out of memory"); }
    if (slot < 0) {
        if (g_global_count == SC_GLOBALS_MAX) { br_free(t.p); sc_fail(vm, line, "set_global(): no room for more than %d globals", SC_GLOBALS_MAX); }
        slot = g_global_count++;
        snprintf(g_globals[slot].name, sizeof(g_globals[slot].name), "%s", name);
    } else {
        br_free(g_globals[slot].text);
    }
    g_globals[slot].text = t.p;
    return sc_nil();
}
static sc_value bi_global(ARGS) {
    int slot = sc_global_slot(sc_global_name(vm, &a[0], line, "global()"));
    sc_lexer L;
    sc_token t;
    sc_value v;
    if (slot < 0) return n > 1 ? a[1] : sc_nil();
    L.p = g_globals[slot].text;
    L.line = 1;
    L.depth = 0;
    t = sc_next(&L);
    if (!sc_read_value(vm->rt, &L, t, &v, 0)) sc_fail(vm, line, "out of memory");
    return v;
}
static sc_value bi_has_global(ARGS) { BR_UNUSED(n); return sc_bool(sc_global_slot(sc_global_name(vm, &a[0], line, "has_global()")) >= 0); }
static sc_value bi_delete_global(ARGS) {
    int slot = sc_global_slot(sc_global_name(vm, &a[0], line, "delete_global()"));
    BR_UNUSED(n);
    if (slot < 0) return sc_bool(false);
    br_free(g_globals[slot].text);
    g_globals[slot] = g_globals[--g_global_count];
    return sc_bool(true);
}

void br_script_reset_session(void) {
    int i;
    for (i = 0; i < g_global_count; ++i) br_free(g_globals[i].text);
    g_global_count = 0;
    if (g_music.mixer) {
        if (g_music.voice) br_voice_stop(g_music.mixer, g_music.voice);
        if (g_music.old) br_voice_stop(g_music.mixer, g_music.old);
    }
    memset(&g_music, 0, sizeof(g_music));
    for (i = 0; i < SC_TONES; ++i)
        if (g_tones[i].sound.samples && g_tones[i].mixer) br_mixer_stop_sound(g_tones[i].mixer, &g_tones[i].sound);
}
void br_script_release(void) {
    int i;
    br_script_reset_session();
    for (i = 0; i < SC_TONES; ++i) if (g_tones[i].sound.samples) br_sound_destroy(&g_tones[i].sound);
    memset(g_tones, 0, sizeof(g_tones));
}

/* ---- the window ---- */
static sc_value bi_set_fullscreen(ARGS) { br_engine *e = sc_engine(vm); BR_UNUSED(n); BR_UNUSED(line); if (e) br_window_set_fullscreen(e, sc_truthy(&a[0])); return sc_nil(); }
static sc_value bi_fullscreen(ARGS) { br_engine *e = sc_engine(vm); UNUSED_ARGS; return sc_bool(e && br_window_is_fullscreen(e)); }
static sc_value bi_set_window_title(ARGS) { br_engine *e = sc_engine(vm); const char *t = sc_need_str(vm, &a[0], line, "set_window_title()"); BR_UNUSED(n); if (e) br_window_set_title(e, t); return sc_nil(); }
static sc_value bi_show_cursor(ARGS) { br_engine *e = sc_engine(vm); BR_UNUSED(n); BR_UNUSED(line); if (e) br_window_set_cursor_visible(e, sc_truthy(&a[0])); return sc_nil(); }
static sc_value bi_screen_size(ARGS) { br_engine *e = sc_engine(vm); UNUSED_ARGS; return e ? sc_vec(e->framebuffer.width, e->framebuffer.height) : sc_vec(320, 180); }

/* ---- more input: the wheel, typed text, touches, actions ---- */
static sc_value bi_mouse_released(ARGS) {
    br_engine *e = sc_engine(vm);
    int b = n ? (int)NUM(0, "mouse_released()") : 0;
    if (b < 0 || b >= BR_MOUSE_BUTTON_COUNT) sc_fail(vm, line, "mouse buttons are 0 (left), 1 (right) and 2 (middle)");
    return sc_bool(e && br_mouse_released(&e->input, (br_mouse_button)b));
}
/* mouse_wheel(): how far the wheel turned since the last frame (+y: towards the player), once a frame. */
/* How far the mouse moved since the last frame, in game pixels - also while it's
 * locked for mouse-look, when its position stays put. Taken once a frame. */
static sc_value bi_mouse_motion(ARGS) {
    br_engine *e = sc_engine(vm);
    UNUSED_ARGS;
    if (!e || vm->rt->motion_frame == e->frame + 1) return sc_vec(0, 0);
    vm->rt->motion_frame = e->frame + 1;
    return sc_vec(e->input.motion_x, e->input.motion_y);
}
static sc_value bi_lock_mouse(ARGS) {
    br_engine *e = sc_engine(vm);
    BR_UNUSED(n); BR_UNUSED(line);
    if (e) br_window_set_mouse_locked(e, sc_truthy(&a[0]));
    return sc_nil();
}
static sc_value bi_mouse_locked(ARGS) {
    br_engine *e = sc_engine(vm);
    UNUSED_ARGS;
    return sc_bool(e && e->window.mouse_locked);
}
static sc_value bi_mouse_wheel(ARGS) {
    br_engine *e = sc_engine(vm);
    UNUSED_ARGS;
    if (!e || vm->rt->wheel_frame == e->frame + 1) return sc_vec(0, 0);
    vm->rt->wheel_frame = e->frame + 1;
    return sc_vec(e->input.wheel_x, e->input.wheel_y);
}
/* text_typed(): the characters typed since the last frame (once a frame), for name entry and the like. */
static sc_value bi_text_typed(ARGS) {
    br_engine *e = sc_engine(vm);
    char buf[BR_TEXT_INPUT_MAX * 4 + 1];
    size_t len = 0;
    int i;
    UNUSED_ARGS;
    if (!e || vm->rt->text_frame == e->frame + 1) return sc_string(vm, "", 0);
    vm->rt->text_frame = e->frame + 1;
    for (i = 0; i < e->input.text_count; ++i) {             /* as UTF-8 */
        uint32_t c = e->input.text[i];
        if (c < 0x80) buf[len++] = (char)c;
        else if (c < 0x800) { buf[len++] = (char)(0xC0 | (c >> 6)); buf[len++] = (char)(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { buf[len++] = (char)(0xE0 | (c >> 12)); buf[len++] = (char)(0x80 | ((c >> 6) & 0x3F)); buf[len++] = (char)(0x80 | (c & 0x3F)); }
        else { buf[len++] = (char)(0xF0 | (c >> 18)); buf[len++] = (char)(0x80 | ((c >> 12) & 0x3F)); buf[len++] = (char)(0x80 | ((c >> 6) & 0x3F)); buf[len++] = (char)(0x80 | (c & 0x3F)); }
    }
    return sc_string(vm, buf, len);
}
/* touches(): where each finger on the screen is (game pixels), leaving out those on the on-screen controls. */
static sc_value bi_touches(ARGS) {
    br_engine *e = sc_engine(vm);
    sc_value list = sc_new_list(vm, line);
    int i;
    BR_UNUSED(a); BR_UNUSED(n);
    if (e) for (i = 0; i < e->input.touch_count; ++i)
        if (!e->input.touches[i].claimed) sc_list_push(vm, list.as.l, sc_vec(e->input.touches[i].x, e->input.touches[i].y), line);
    return list;
}
/* Actions: names bound to inputs, shared by every scene (input.h). An input is a key's name ("space"),
 * "mouse:0" (a mouse button), "pad:a" (a gamepad button, as button_down() names them), or "pad:left_x-" /
 * "pad:left_y+" (a stick or trigger pushed that way past the dead zone). */
static void sc_bind_one(sc_vm *vm, br_input *in, br_action act, const char *what, int line) {
    if (strncmp(what, "mouse:", 6) == 0) {
        int b = atoi(what + 6);
        if (b < 0 || b >= BR_MOUSE_BUTTON_COUNT || !isdigit((unsigned char)what[6])) sc_fail(vm, line, "bind(): \"%s\": mouse buttons are mouse:0, mouse:1 and mouse:2", what);
        br_input_bind(in, act, BR_BIND_MOUSE_BUTTON, b, -1);
    } else if (strncmp(what, "pad:", 4) == 0) {
        static const struct { const char *name; int axis; } axes[] = {
            { "left_x", BR_PAD_AXIS_LEFT_X }, { "left_y", BR_PAD_AXIS_LEFT_Y }, { "right_x", BR_PAD_AXIS_RIGHT_X },
            { "right_y", BR_PAD_AXIS_RIGHT_Y }, { "lt", BR_PAD_AXIS_LEFT_TRIGGER }, { "rt", BR_PAD_AXIS_RIGHT_TRIGGER },
        };
        char name[32];
        size_t len = strlen(what + 4), i;
        snprintf(name, sizeof(name), "%s", what + 4);
        if (len > 1 && (name[len - 1] == '+' || name[len - 1] == '-')) {
            bool neg = name[len - 1] == '-';
            name[len - 1] = 0;
            for (i = 0; i < BR_ARRAY_COUNT(axes); ++i)
                if (sc_strieq(name, axes[i].name)) { br_input_bind(in, act, neg ? BR_BIND_PAD_AXIS_NEG : BR_BIND_PAD_AXIS_POS, axes[i].axis, -1); return; }
            sc_fail(vm, line, "bind(): there is no axis called \"%s\" (left_x left_y right_x right_y lt rt, then + or -)", name);
        } else {
            int b = sc_pad_button_named(name);
            if (b < 0 || b >= BR_PAD_BUTTON_COUNT) {
                if (b == SC_PAD_LT || b == SC_PAD_RT) { br_input_bind(in, act, BR_BIND_PAD_AXIS_POS, b == SC_PAD_LT ? BR_PAD_AXIS_LEFT_TRIGGER : BR_PAD_AXIS_RIGHT_TRIGGER, -1); return; }
                sc_fail(vm, line, "bind(): there is no gamepad button called \"%s\"", name);
            }
            br_input_bind(in, act, BR_BIND_PAD_BUTTON, b, -1);
        }
    } else {
        br_key k = sc_key_named(what);
        bool either = sc_strieq(what, "shift") || sc_strieq(what, "ctrl") || sc_strieq(what, "alt");   /* either side, as key_down() takes them */
        if (k == BR_KEY_UNKNOWN) sc_fail(vm, line, "bind(): there is no key called \"%s\" (or write mouse:0, pad:a, pad:left_x-)", what);
        br_input_bind(in, act, BR_BIND_KEY, (int)k, either ? BR_KEY_EITHER_SIDE : -1);
    }
}
static br_action sc_action(sc_vm *vm, const sc_value *v, int line, const char *what, bool create) {
    br_engine *e = sc_engine(vm);
    const char *name = sc_need_str(vm, v, line, what);
    br_action act;
    if (!name[0] || strlen(name) >= BR_ACTION_NAME_MAX) sc_fail(vm, line, "%s: an action's name is 1 to %d characters", what, BR_ACTION_NAME_MAX - 1);
    if (!e) return -1;
    act = create ? br_input_action(&e->input, name) : br_input_find_action(&e->input, name);
    if (create && act < 0) sc_fail(vm, line, "%s: no room for more actions", what);
    return act;
}
/* bind(action, input, ...): adds inputs to an action (up to 6). */
static sc_value bi_bind(ARGS) {
    br_engine *e = sc_engine(vm);
    br_action act = sc_action(vm, &a[0], line, "bind()", true);
    int i;
    if (!e) return sc_nil();
    for (i = 1; i < n; ++i) {
        if (e->input.actions[act].binding_count >= BR_MAX_ACTION_BINDINGS) sc_fail(vm, line, "bind(): an action takes up to %d inputs", BR_MAX_ACTION_BINDINGS);
        sc_bind_one(vm, &e->input, act, sc_need_str(vm, &a[i], line, "bind() input"), line);
    }
    return sc_nil();
}
static sc_value bi_unbind(ARGS) { br_engine *e = sc_engine(vm); br_action act = sc_action(vm, &a[0], line, "unbind()", false); BR_UNUSED(n); if (e && act >= 0) br_input_clear_bindings(&e->input, act); return sc_nil(); }
/* which: 0 down, 1 pressed this step, 2 released this step - built on the keys' and buttons' own
 * states, so a press is seen in exactly one step (as key_pressed() is). */
static bool sc_action_state(sc_vm *vm, const sc_value *v, int line, int which, const char *what) {
    br_engine *e = sc_engine(vm);
    br_action act = sc_action(vm, v, line, what, false);
    const br_action_def *d;
    int i, p;
    bool any_down = false, any_edge = false, any_tap = false;
    if (!e || act < 0) return false;
    d = &e->input.actions[act];
    for (i = 0; i < d->binding_count; ++i) {
        const br_binding *b = &d->bindings[i];
        bool down = false, pressed = false, released = false;
        switch (b->kind) {
        case BR_BIND_KEY: {
            br_key other = b->pad == BR_KEY_EITHER_SIDE ? br_key_right_partner((br_key)b->code) : BR_KEY_UNKNOWN;
            down = br_key_down(&e->input, (br_key)b->code); pressed = br_key_pressed(&e->input, (br_key)b->code); released = br_key_released(&e->input, (br_key)b->code);
            if (other != BR_KEY_UNKNOWN) {                       /* "shift": the right one too */
                down |= br_key_down(&e->input, other); pressed |= br_key_pressed(&e->input, other); released |= br_key_released(&e->input, other);
            }
            break;
        }
        case BR_BIND_MOUSE_BUTTON: down = br_mouse_down(&e->input, (br_mouse_button)b->code); pressed = br_mouse_pressed(&e->input, (br_mouse_button)b->code); released = br_mouse_released(&e->input, (br_mouse_button)b->code); break;
        case BR_BIND_PAD_BUTTON:
            for (p = 0; p < BR_MAX_GAMEPADS; ++p) {
                if (b->pad >= 0 && b->pad != p) continue;
                down |= e->input.pads[p].buttons[b->code]; pressed |= e->input.pads[p].pressed[b->code]; released |= e->input.pads[p].released[b->code];
            }
            break;
        case BR_BIND_PAD_AXIS_POS: case BR_BIND_PAD_AXIS_NEG:
            for (p = 0; p < BR_MAX_GAMEPADS; ++p) {
                float x;
                if (b->pad >= 0 && b->pad != p) continue;
                x = br_pad_axis(&e->input, p, (br_gamepad_axis)b->code);
                down |= b->kind == BR_BIND_PAD_AXIS_POS ? x > 0.5f : x < -0.5f;
            }
            pressed = down && !d->prev_down;              /* (a stick has no edges of its own) */
            released = !down && d->prev_down;
            break;
        default: break;
        }
        any_down |= down;
        any_tap |= pressed;                              /* (down and up again within one frame: still a press) */
        any_edge |= which == 1 ? pressed : released;
    }
    if (which == 0) return any_down || any_tap;
    if (which == 1) return any_edge;
    return any_edge && !any_down;
}
static sc_value bi_action_down(ARGS) { BR_UNUSED(n); return sc_bool(sc_action_state(vm, &a[0], line, 0, "action_down()")); }
static sc_value bi_action_pressed(ARGS) { BR_UNUSED(n); return sc_bool(sc_action_state(vm, &a[0], line, 1, "action_pressed()")); }
static sc_value bi_action_released(ARGS) { BR_UNUSED(n); return sc_bool(sc_action_state(vm, &a[0], line, 2, "action_released()")); }
/* action_axis(negative, positive): -1, 0 or 1 from two actions (left and right, say). */
static sc_value bi_action_axis(ARGS) {
    BR_UNUSED(n);
    return sc_num((sc_action_state(vm, &a[1], line, 0, "action_axis()") ? 1 : 0) - (sc_action_state(vm, &a[0], line, 0, "action_axis()") ? 1 : 0));
}

/* ---- physics: gravity ---- */
static sc_value bi_gravity(ARGS) { br_vec2 g = br_physics_gravity(vm->rt->w); UNUSED_ARGS; return sc_vec(g.x, g.y); }
/* set_gravity(v): the PhysicsWorld's gravity (one is added to this entity if the scene has none). */
static sc_value bi_set_gravity(ARGS) {
    br_world *w = vm->rt->w;
    br_component pw = br_component_find(w, "PhysicsWorld");
    br_vec2 g = sc_need_vec(vm, &a[0], line, "set_gravity()");
    br_physics_world *d = NULL;
    BR_UNUSED(n);
    if (pw == BR_COMPONENT_INVALID) sc_fail(vm, line, "set_gravity(): there's no physics in this scene");
    if (w->pools[pw].count) d = (br_physics_world *)w->pools[pw].data;
    else if (vm->inst) d = (br_physics_world *)br_add(w, vm->inst->e, pw, NULL);
    if (d) d->gravity = g;
    return sc_nil();
}

/* ---- tilemaps by column and row ---- */
static sc_value bi_tile_cell(ARGS) {
    br_entity e = sc_need_entity(vm, &a[0], line, "tile_cell()");
    int col, row;
    BR_UNUSED(n);
    if (!br_has(vm->rt->w, e, vm->rt->ids.tilemap)) sc_fail(vm, line, "tile_cell(): the entity has no Tilemap");
    br_tilemap_get(vm->rt->w, e, sc_need_vec(vm, &a[1], line, "tile_cell() point"), &col, &row);
    return sc_vec(col, row);
}
static sc_value bi_tile_get(ARGS) {
    br_entity e = sc_need_entity(vm, &a[0], line, "tile_get()");
    char c = br_tilemap_cell(vm->rt->w, e, (int)floor(NUM(1, "tile_get() column")), (int)floor(NUM(2, "tile_get() row")));
    BR_UNUSED(n);
    return sc_string(vm, &c, c ? 1 : 0);
}
static sc_value bi_tile_put(ARGS) {
    br_entity e = sc_need_entity(vm, &a[0], line, "tile_put()");
    const char *t = sc_need_str(vm, &a[3], line, "tile_put()");
    BR_UNUSED(n);
    if (strlen(t) != 1) sc_fail(vm, line, "tile_put() wants one character, not \"%s\"", t);
    return sc_bool(br_tilemap_set_cell(vm->rt->w, e, (int)floor(NUM(1, "tile_put() column")), (int)floor(NUM(2, "tile_put() row")), t[0]));
}
static sc_value bi_tile_cell_center(ARGS) {
    br_entity e = sc_need_entity(vm, &a[0], line, "tile_cell_center()");
    br_vec2 c;
    BR_UNUSED(n);
    if (!br_tilemap_cell_center(vm->rt->w, e, (int)floor(NUM(1, "tile_cell_center() column")), (int)floor(NUM(2, "tile_cell_center() row")), &c))
        sc_fail(vm, line, "tile_cell_center(): the entity has no Tilemap");
    return sc_vec(c.x, c.y);
}
static sc_value bi_tile_box_solid(ARGS) {
    br_entity e = sc_need_entity(vm, &a[0], line, "tile_box_solid()");
    BR_UNUSED(n);
    return sc_bool(br_tilemap_solid_box(vm->rt->w, e, sc_need_vec(vm, &a[1], line, "tile_box_solid() centre"), sc_need_vec(vm, &a[2], line, "tile_box_solid() size")));
}

/* ---- odds and ends ---- */
static sc_value bi_random_seed(ARGS) { BR_UNUSED(n); br_rng_seed(&vm->rt->rng, (uint64_t)(int64_t)floor(NUM(0, "random_seed()"))); return sc_nil(); }
static sc_value bi_clipboard(ARGS) {
    br_engine *e = sc_engine(vm);
    char *text;
    sc_value v;
    UNUSED_ARGS;
    if (!e || !(text = br_clipboard_get(e))) return sc_string(vm, "", 0);
    v = sc_string(vm, text, strlen(text));
    br_free(text);
    return v;
}
static sc_value bi_set_clipboard(ARGS) { br_engine *e = sc_engine(vm); const char *t = sc_need_str(vm, &a[0], line, "set_clipboard()"); BR_UNUSED(n); return sc_bool(e && br_clipboard_set(e, t)); }
static sc_value bi_fps(ARGS) { br_engine *e = sc_engine(vm); UNUSED_ARGS; return sc_num(e ? e->fps : 0); }
static sc_value bi_frame_count(ARGS) { br_engine *e = sc_engine(vm); UNUSED_ARGS; return sc_num(e ? (double)e->frame : 0); }
static sc_value bi_log(sc_vm *vm, sc_value *a, int n, int warn) {
    char buf[1024];
    int i;
    buf[0] = 0;
    for (i = 0; i < n; ++i) {
        if (i) strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
        sc_append(vm, buf, sizeof(buf), &a[i], 0);
    }
    if (warn) BR_WARN("[%s] %s", vm->inst && vm->inst->prog ? vm->inst->prog->name : "script", buf);
    else BR_ERROR("[%s] %s", vm->inst && vm->inst->prog ? vm->inst->prog->name : "script", buf);
    return sc_nil();
}
static sc_value bi_log_warning(ARGS) { BR_UNUSED(line); return bi_log(vm, a, n, 1); }
static sc_value bi_log_error(ARGS) { BR_UNUSED(line); return bi_log(vm, a, n, 0); }
static sc_value bi_open_url(ARGS) { br_engine *e = sc_engine(vm); const char *u = sc_need_str(vm, &a[0], line, "open_url()"); BR_UNUSED(n); return sc_bool(e && br_open_path(e, u)); }
static sc_value bi_image_size(ARGS) {
    const br_image *img = br_asset_image(sc_need_str(vm, &a[0], line, "image_size()"));
    BR_UNUSED(n);
    if (!img) sc_fail(vm, line, "image_size(): can't load %s", a[0].as.s->chars);
    return sc_vec(img->width, img->height);
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
    { "vec2", 2, 2, bi_vec2, "nn>v" }, { "length", 1, 1, bi_length, "V>n" }, { "normalize", 1, 1, bi_normalize, "V>S" },
    { "dot", 2, 2, bi_dot, "VV>n" }, { "distance", 2, 2, bi_distance, "VV>n" }, { "angle", 1, 1, bi_angle, "v>n" },
    { "vec3", 3, 3, bi_vec3, "nnn>3" }, { "cross", 2, 2, bi_cross, "33>3" },
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
    { "touch_controls_shown", 0, 0, bi_touch_controls_shown, ">b" }, { "show_touch_controls", 1, 1, bi_show_touch_controls, "b>-" },
    { "set_touch_controls", 1, 1, bi_set_touch_controls, "s>-" },
    { "find", 1, 1, bi_find, "s>e" }, { "find_all", 1, 1, bi_find_all, "s>L" }, { "find_with", 1, 1, bi_find_with, "s>L" }, { "destroy", 1, 1, bi_destroy, "e>-" },
    { "find_tagged", 1, 16, bi_find_tagged, "ss*>L" }, { "first_tagged", 1, 16, bi_first_tagged, "ss*>e" },
    { "has_tag", 2, 2, bi_has_tag, "es>b" }, { "add_tag", 2, 2, bi_add_tag, "es>-" },
    { "remove_tag", 2, 2, bi_remove_tag, "es>b" }, { "tags", 1, 1, bi_tags, "e>W" },
    { "spawn", 1, 3, bi_spawn, "spq>e" }, { "children", 1, 1, bi_children, "e>L" }, { "child", 2, 2, bi_child, "es>e" },
    { "has", 2, 2, bi_has, "es>b" }, { "add", 2, 2, bi_add, "es>c" },
    { "remove_component", 2, 2, bi_remove_component, "es>-" },
    { "apply_force", 2, 3, bi_apply_force, "eVV>-" }, { "apply_impulse", 2, 3, bi_apply_impulse, "eVV>-" },
    { "raycast", 2, 4, bi_raycast, "VVqn>a" }, { "entity_at", 1, 2, bi_entity_at, "Vn>e" },
    { "apply_torque", 2, 2, bi_apply_torque, "e3>-" }, { "overlap_sphere", 2, 2, bi_overlap_sphere, "3n>L" },
    { "look_at", 2, 2, bi_look_at, "e3>-" }, { "move_and_slide", 2, 2, bi_move_and_slide, "e3>3" },
    { "is_on_floor", 1, 1, bi_is_on_floor, "e>b" }, { "is_on_wall", 1, 1, bi_is_on_wall, "e>b" },
    { "is_on_ceiling", 1, 1, bi_is_on_ceiling, "e>b" },
    { "emit", 1, 2, bi_emit, "en>-" }, { "particle_count", 1, 1, bi_particle_count, "e>i" },
    { "tween", 4, 6, bi_tween, "esansn>i" }, { "tween_repeat", 2, 3, bi_tween_repeat, "inb>-" },
    { "tween_then", 2, 2, bi_tween_then, "is>-" }, { "stop_tween", 1, 1, bi_stop_tween, "a>-" },
    { "is_tweening", 1, 2, bi_is_tweening, "as>b" }, { "ease", 2, 2, bi_ease, "sn>n" },
    { "screen_ray", 1, 1, bi_screen_ray, "v>l" },
    { "play_sound", 1, 4, bi_play_sound, "snnn>n" }, { "send", 2, 16, bi_send, "esa*>a" },
    { "load_scene", 1, 1, bi_load_scene, "s>-" }, { "quit", 0, 0, bi_quit, ">-" },
    { "tile_at", 2, 2, bi_tile_at, "ev>s" }, { "set_tile", 3, 3, bi_set_tile, "evs>b" },
    { "tile_solid", 2, 2, bi_tile_solid, "ev>b" }, { "tile_center", 2, 2, bi_tile_center, "ev>v" },
    { "substr", 2, 3, bi_substr, "snn>s" },
    { "save_data", 2, 2, bi_save_data, "sa>b" }, { "load_data", 1, 2, bi_load_data, "sa>a" },
    { "has_data", 1, 1, bi_has_data, "s>b" }, { "delete_data", 1, 1, bi_delete_data, "s>b" },
    { "data_names", 0, 0, bi_data_names, ">W" },
    { "last_key", 0, 0, bi_last_key, ">s" }, { "set_volume", 2, 2, bi_set_volume, "sn>-" },
    { "volume", 1, 1, bi_volume, "s>n" },
    /* drawing, in draw() */
    { "draw_rect", 3, 4, bi_draw_rect, "vvab>-" }, { "draw_line", 3, 3, bi_draw_line, "vva>-" },
    { "draw_circle", 3, 4, bi_draw_circle, "vnab>-" }, { "draw_text", 3, 4, bi_draw_text, "avan>-" },
    { "draw_image", 2, 5, bi_draw_image, "svnvb>-" }, { "text_size", 1, 2, bi_text_size, "an>v" },
    /* cameras and animation */
    { "camera", 0, 0, bi_camera, ">e" }, { "shake_camera", 2, 2, bi_shake_camera, "nn>-" },
    { "snap_camera", 0, 0, bi_snap_camera, ">-" }, { "screen_to_world", 1, 1, bi_screen_to_world, "v>v" },
    { "world_to_screen", 1, 1, bi_world_to_screen, "V>v" }, { "play_animation", 2, 4, bi_play_animation, "esnb>-" },
    { "stop_animation", 1, 1, bi_stop_animation, "e>-" }, { "animation_clips", 1, 1, bi_animation_clips, "e>W" },
    /* sounds */
    { "stop_sound", 1, 1, bi_stop_sound, "n>-" }, { "sound_playing", 1, 1, bi_sound_playing, "n>b" },
    { "set_sound_volume", 2, 2, bi_set_sound_volume, "nn>-" }, { "set_sound_pitch", 2, 2, bi_set_sound_pitch, "nn>-" },
    { "set_sound_pan", 2, 2, bi_set_sound_pan, "nn>-" }, { "pause_sound", 2, 2, bi_pause_sound, "nb>-" },
    { "stop_sounds", 0, 1, bi_stop_sounds, "s>-" }, { "sound_length", 1, 1, bi_sound_length, "s>n" },
    { "play_music", 1, 3, bi_play_music, "snn>-" }, { "stop_music", 0, 1, bi_stop_music, "n>-" },
    { "music", 0, 0, bi_music, ">s" }, { "play_tone", 3, 4, bi_play_tone, "snnn>n" },
    /* messages, scenes and globals */
    { "broadcast", 1, 16, bi_broadcast, "sa*>i" },
    { "push_scene", 1, 3, bi_push_scene, "sbb>-" }, { "pop_scene", 0, 0, bi_pop_scene, ">-" },
    { "set_global", 2, 2, bi_set_global, "sa>-" }, { "global", 1, 2, bi_global, "sa>a" },
    { "has_global", 1, 1, bi_has_global, "s>b" }, { "delete_global", 1, 1, bi_delete_global, "s>b" },
    /* the window */
    { "set_fullscreen", 1, 1, bi_set_fullscreen, "b>-" }, { "fullscreen", 0, 0, bi_fullscreen, ">b" },
    { "set_window_title", 1, 1, bi_set_window_title, "s>-" }, { "show_cursor", 1, 1, bi_show_cursor, "b>-" },
    { "screen_size", 0, 0, bi_screen_size, ">v" },
    /* more input */
    { "mouse_released", 0, 1, bi_mouse_released, "n>b" }, { "mouse_wheel", 0, 0, bi_mouse_wheel, ">v" },
    { "mouse_motion", 0, 0, bi_mouse_motion, ">v" }, { "lock_mouse", 1, 1, bi_lock_mouse, "b>-" }, { "mouse_locked", 0, 0, bi_mouse_locked, ">b" },
    { "text_typed", 0, 0, bi_text_typed, ">s" }, { "touches", 0, 0, bi_touches, ">l" },
    { "bind", 2, 7, bi_bind, "sssssss>-" }, { "unbind", 1, 1, bi_unbind, "s>-" },
    { "action_down", 1, 1, bi_action_down, "s>b" }, { "action_pressed", 1, 1, bi_action_pressed, "s>b" },
    { "action_released", 1, 1, bi_action_released, "s>b" }, { "action_axis", 2, 2, bi_action_axis, "ss>i" },
    /* physics and tilemaps */
    { "gravity", 0, 0, bi_gravity, ">v" }, { "set_gravity", 1, 1, bi_set_gravity, "v>-" },
    { "tile_cell", 2, 2, bi_tile_cell, "ev>v" }, { "tile_get", 3, 3, bi_tile_get, "enn>s" },
    { "tile_put", 4, 4, bi_tile_put, "enns>b" }, { "tile_cell_center", 3, 3, bi_tile_cell_center, "enn>v" },
    { "tile_box_solid", 3, 3, bi_tile_box_solid, "evv>b" },
    /* odds and ends */
    { "random_seed", 1, 1, bi_random_seed, "n>-" }, { "clipboard", 0, 0, bi_clipboard, ">s" },
    { "set_clipboard", 1, 1, bi_set_clipboard, "s>b" }, { "fps", 0, 0, bi_fps, ">n" },
    { "frame_count", 0, 0, bi_frame_count, ">i" }, { "log_warning", 0, 16, bi_log_warning, "a*>-" },
    { "log_error", 0, 16, bi_log_error, "a*>-" }, { "open_url", 1, 1, bi_open_url, "s>b" },
    { "image_size", 1, 1, bi_image_size, "s>v" },
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
bool br_script_builtin_arity(int index, int *min_args, int *max_args) {
    if (index < 0 || index >= br_script_builtin_count()) return false;
    *min_args = k_builtin_table[index].min_args;
    *max_args = k_builtin_table[index].max_args;
    return true;
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
    br_free(rt->tweens);
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
    br_ids3d_of(w, &rt->ids3);
    rt->body3_id = br_component_find(w, "RigidBody3D");
    rt->collider_id = br_component_find(w, "Collider");
    br_rng_seed(&rt->rng, 0x5C21u);
    br_rng_seed(&rt->draw_rng, 0xD2A3u);
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
        bool used = false;                             /* the old version may still be running, or have objects about */
        sc_obj *o;
        for (k = 0; k < rt->instance_count; ++k) used |= rt->instances[k]->prog == rt->programs[i];
        for (o = rt->heap.objects; o && !used; o = o->next)
            used = o->kind == OBJ_OBJECT && ((sc_object *)o)->cls->prog == rt->programs[i];
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

enum { SC_RUN_GLOBALS = -1, SC_RUN_STATICS = -2 };

/* Runs `fn` (or the variables' starting values, SC_RUN_GLOBALS, or the
 * classes' static fields', SC_RUN_STATICS) safely: an error stops this script only. */
static bool sc_run(sc_runtime *rt, sc_instance *inst, int fn, const sc_value *args, int argc, sc_value *result) {
    sc_vm vm;
    jmp_buf jb;
    volatile bool ok = true;
    memset(&vm, 0, offsetof(sc_vm, frames));                 /* (the frames fill as functions run) */
    vm.rt = rt;
    vm.inst = inst;
    vm.budget = SC_BUDGET;
    vm.fail = &jb;
    vm.serial = ++g_debug_serial;
    rt->busy++;
    if (setjmp(jb) == 0) {
        if (fn == SC_RUN_STATICS) {
            int i, k;
            for (i = 0; i < inst->prog->class_count; ++i) {
                sc_class *c = inst->prog->classes[i];
                vm.cls = c;
                for (k = 0; k < c->member_count; ++k) {
                    const sc_member *m = &c->members[k];
                    sc_value v;
                    if (m->fn || !m->is_static) continue;
                    v = m->init ? sc_own(&vm, sc_eval(&vm, m->init), m->line) : sc_default(&vm, m->ty, m->line);
                    if (m->ty.t && m->init) sc_check_value(&vm, m->ty, &v, m->line, m->name);
                    c->statics[m->slot] = v;
                }
            }
        } else if (fn < 0) {
            int i;
            for (i = 0; i < inst->prog->global_count; ++i) {
                const sc_global *g = &inst->prog->globals[i];
                sc_value v = g->init ? sc_own(&vm, sc_eval(&vm, g->init), g->line) : sc_default(&vm, g->ty, g->line);
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
    if (!inst->prog->statics_ready) {                          /* once per file (and world): the classes' static fields */
        inst->prog->statics_ready = true;
        if (!sc_run(rt, inst, SC_RUN_STATICS, NULL, 0, NULL)) return;
    }
    if (!sc_run(rt, inst, SC_RUN_GLOBALS, NULL, 0, NULL)) return;
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
    for (i = 0; i < rt->program_count; ++i) {                  /* static fields */
        const sc_program *p = rt->programs[i];
        for (k = 0; k < p->class_count; ++k) {
            int s;
            for (s = 0; s < p->classes[k]->static_count; ++s) sc_mark_value(&p->classes[k]->statics[s]);
        }
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
    if (g_music.mixer == &((br_engine *)w->context)->mixer) sc_music_tick(((br_engine *)w->context)->time);   /* fades */
    count = w->pools[rt->script_id].count;              /* scripts spawned now start next step */
    arg = sc_num(dt);
    rt->dt = dt;
    for (i = 0; i < count && i < w->pools[rt->script_id].count; ++i) {   /* every script's variables first, */
        br_component_pool *p = &w->pools[rt->script_id];                  /* so each start() can use the others' */
        br_script *s = (br_script *)(p->data + (size_t)i * p->stride);
        if (!s->instance) s->instance = sc_instance_create(rt, p->entities[i], s);
        if (s->instance && !((sc_instance *)s->instance)->ready) sc_make_ready(rt, (sc_instance *)s->instance, s);
    }
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
    sc_tweens_step(rt, dt);
    sc_collect(rt);
}

/* `normal`: a vec2 (2D physics) or a vec3 (3D), pointing from self toward other. */
static void sc_deliver(sc_runtime *rt, br_entity self, br_entity other, br_contact_kind kind, sc_value normal, float speed) {
    sc_instance *inst = sc_instance_of(rt, self);
    sc_value args[3];
    if (!inst || !inst->started || inst->failed) return;
    if (inst->beh) {
        if ((kind == BR_CONTACT_BEGIN || kind == BR_CONTACT_END) && inst->beh->on_collision)
            inst->beh->on_collision(rt->w, self, inst->state, other, kind == BR_CONTACT_BEGIN);
        if ((kind == BR_TRIGGER_ENTER || kind == BR_TRIGGER_EXIT) && inst->beh->on_trigger)
            inst->beh->on_trigger(rt->w, self, inst->state, other, kind == BR_TRIGGER_ENTER);
        return;
    }
    args[0] = sc_ent(other);
    args[1] = normal;
    args[2] = sc_num(speed);
    switch (kind) {
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
            sc_deliver(rt, copy[k].a, copy[k].b, copy[k].kind, sc_vec(copy[k].normal.x, copy[k].normal.y), copy[k].speed);
            sc_deliver(rt, copy[k].b, copy[k].a, copy[k].kind, sc_vec(-copy[k].normal.x, -copy[k].normal.y), copy[k].speed);
        }
        ev = br_physics_events(w, &n);
    }
    {                                                   /* and 3D's */
        const br_contact3d_event *e3 = br_physics3d_events(w, &n);
        br_contact3d_event copy3[128];
        for (i = 0; i < n; i += 128) {
            uint32_t chunk = n - i < 128 ? n - i : 128;
            memcpy(copy3, e3 + i, sizeof(br_contact3d_event) * chunk);
            for (k = 0; k < chunk; ++k) {
                sc_deliver(rt, copy3[k].a, copy3[k].b, copy3[k].kind, sc_v3val(copy3[k].normal), copy3[k].speed);
                sc_deliver(rt, copy3[k].b, copy3[k].a, copy3[k].kind, sc_v3val(br_v3_neg(copy3[k].normal)), copy3[k].speed);
            }
            e3 = br_physics3d_events(w, &n);
        }
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

/* ---- the debugger ----------------------------------------------------------------------- */

#define SC_MAX_BREAKPOINTS 128
static struct {
    br_debug_hook hook;
    void *user;
    struct { char file[BR_PATH_MAX]; int line; } bp[SC_MAX_BREAKPOINTS];
    int bp_count;
    br_debug_action mode;           /* what the last stop asked for (CONTINUE: run to a breakpoint) */
    bool pause;                     /* stop at the next statement anywhere */
    bool detached;                  /* (until the next attach) */
    uint32_t stop_serial;           /* where the last stop was, for stepping */
    int stop_depth, stop_line;
    uint32_t prev_serial;           /* the statement before this one */
    int prev_depth, prev_line;
    sc_vm *vm;                      /* while stopped: the paused run */
} g_dbg;

static void sc_debug_refresh(void) {
    g_debug_on = g_dbg.hook && !g_dbg.detached && (g_dbg.bp_count > 0 || g_dbg.pause || g_dbg.mode != BR_DEBUG_CONTINUE);
}

/* "scripts/player.bs" matches "/home/me/game/scripts/player.bs" (and the other way round). */
static bool sc_same_file(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    const char *longer = la >= lb ? a : b, *shorter = la >= lb ? b : a;
    size_t ls = la >= lb ? lb : la, ll = la >= lb ? la : lb;
    if (ls == 0) return false;
    if (strcmp(longer + (ll - ls), shorter) != 0) return false;
    return ll == ls || longer[ll - ls - 1] == '/' || longer[ll - ls - 1] == '\\';
}

static const char *sc_frame_file(const sc_vm *vm, int k) {
    const sc_instance *inst = k >= 0 && k < vm->nframes && k <= SC_MAX_DEPTH ? vm->frames[k].inst : vm->inst;
    return inst && inst->prog ? inst->prog->path : "";
}

static void sc_debug_statement(sc_vm *vm, int line) {
    const char *file = sc_frame_file(vm, vm->nframes - 1), *reason = NULL;
    int depth = vm->nframes, i;
    bool same_line = vm->serial == g_dbg.prev_serial && depth == g_dbg.prev_depth && line == g_dbg.prev_line;
    g_dbg.prev_serial = vm->serial;
    g_dbg.prev_depth = depth;
    g_dbg.prev_line = line;
    if (g_dbg.vm || !vm->serial) return;                      /* (a stop inside a stop; or no proper run) */
    if (g_dbg.pause) reason = "paused";
    else if (g_dbg.mode == BR_DEBUG_STEP_INTO) { if (!same_line) reason = "step"; }
    else if (g_dbg.mode == BR_DEBUG_STEP_OVER) {
        if (vm->serial != g_dbg.stop_serial || depth < g_dbg.stop_depth || (depth == g_dbg.stop_depth && line != g_dbg.stop_line)) reason = "step";
    } else if (g_dbg.mode == BR_DEBUG_STEP_OUT) {
        if (vm->serial != g_dbg.stop_serial || depth < g_dbg.stop_depth) reason = "step";
    }
    if (!reason && !same_line)
        for (i = 0; i < g_dbg.bp_count; ++i)
            if (g_dbg.bp[i].line == line && sc_same_file(g_dbg.bp[i].file, file)) { reason = "breakpoint"; break; }
    if (!reason) return;
    {
        br_debug_stop stop;
        br_debug_action act;
        memset(&stop, 0, sizeof(stop));
        stop.world = vm->rt->w;
        stop.entity = vm->inst ? vm->inst->e : BR_ENTITY_NULL;
        stop.file = file;
        stop.line = line;
        stop.reason = reason;
        g_dbg.pause = false;
        g_dbg.vm = vm;
        vm->rt->busy++;                                         /* (no collecting while it's looked at) */
        act = g_dbg.hook(&stop, g_dbg.user);
        vm->rt->busy--;
        g_dbg.vm = NULL;
        g_dbg.stop_serial = vm->serial;
        g_dbg.stop_depth = depth;
        g_dbg.stop_line = line;
        if (act == BR_DEBUG_DETACH) { g_dbg.detached = true; act = BR_DEBUG_CONTINUE; }
        g_dbg.mode = act;
        sc_debug_refresh();
    }
}

void br_script_debug_attach(br_debug_hook hook, void *user) {
    g_dbg.hook = hook;
    g_dbg.user = user;
    g_dbg.mode = BR_DEBUG_CONTINUE;
    g_dbg.pause = false;
    g_dbg.detached = false;
    sc_debug_refresh();
}

bool br_script_debug_set_breakpoint(const char *file, int line, bool on) {
    int i;
    for (i = 0; i < g_dbg.bp_count; ++i)
        if (g_dbg.bp[i].line == line && strcmp(g_dbg.bp[i].file, file) == 0) break;
    if (on && i == g_dbg.bp_count) {
        if (g_dbg.bp_count == SC_MAX_BREAKPOINTS) return false;
        snprintf(g_dbg.bp[g_dbg.bp_count].file, BR_PATH_MAX, "%s", file);
        g_dbg.bp[g_dbg.bp_count++].line = line;
    } else if (!on && i < g_dbg.bp_count) {
        g_dbg.bp[i] = g_dbg.bp[--g_dbg.bp_count];
    }
    sc_debug_refresh();
    return true;
}

bool br_script_debug_has_breakpoint(const char *file, int line) {
    int i;
    for (i = 0; i < g_dbg.bp_count; ++i)
        if (g_dbg.bp[i].line == line && sc_same_file(g_dbg.bp[i].file, file)) return true;
    return false;
}

int br_script_debug_breakpoint(int index, const char **file, int *line) {
    if (index < 0 || index >= g_dbg.bp_count) return g_dbg.bp_count;
    if (file) *file = g_dbg.bp[index].file;
    if (line) *line = g_dbg.bp[index].line;
    return g_dbg.bp_count;
}

void br_script_debug_clear_breakpoints(void) { g_dbg.bp_count = 0; sc_debug_refresh(); }
void br_script_debug_pause(void) { g_dbg.pause = true; g_dbg.detached = false; sc_debug_refresh(); }
void br_script_debug_resume(void) { g_dbg.pause = false; g_dbg.mode = BR_DEBUG_CONTINUE; g_dbg.detached = false; sc_debug_refresh(); }
bool br_script_debug_stopped(void) { return g_dbg.vm != NULL; }

int br_script_debug_frame_count(void) {
    if (!g_dbg.vm) return 0;
    return g_dbg.vm->nframes > SC_MAX_DEPTH + 1 ? SC_MAX_DEPTH + 1 : g_dbg.vm->nframes;
}

/* Frame 0 is the innermost (where it stopped). */
bool br_script_debug_frame(int index, const char **function, const char **file, int *line) {
    const sc_vm *vm = g_dbg.vm;
    int n = br_script_debug_frame_count(), k;
    if (index < 0 || index >= n) return false;
    k = n - 1 - index;
    if (function) *function = vm->frames[k].f->name;
    if (file) *file = sc_frame_file(vm, k);
    if (line) *line = vm->frames[k].line;
    return true;
}

static void sc_debug_value(sc_vm *vm, const sc_value *v, char *out, size_t size) {
    sc_value s = sc_to_string(vm, v);
    if (v->t == V_STR) snprintf(out, size, "\"%s\"", v->as.s->chars);
    else snprintf(out, size, "%s", s.t == V_STR ? s.as.s->chars : "?");
}

/* A frame's local variables (those whose lines it has reached). */
int br_script_debug_locals(int index, br_debug_var *out, int max) {
    sc_vm *vm = g_dbg.vm;
    int n = br_script_debug_frame_count(), k, i, count = 0;
    const sc_func *f;
    if (index < 0 || index >= n) return 0;
    k = n - 1 - index;
    f = vm->frames[k].f;
    for (i = 0; i < f->dbg_count && count < max; ++i) {
        const sc_dbg_local *d = &f->dbg[i];
        int j;
        bool shadowed = false;
        if (vm->frames[k].line < d->from || vm->frames[k].line > d->to) continue;
        for (j = i + 1; j < f->dbg_count; ++j)                   /* a later one in the same slot, also in scope */
            if (f->dbg[j].slot == d->slot && vm->frames[k].line >= f->dbg[j].from && vm->frames[k].line <= f->dbg[j].to) shadowed = true;
        if (shadowed) continue;
        snprintf(out[count].name, sizeof(out[count].name), "%s", d->name);
        sc_debug_value(vm, &vm->stack[vm->frames[k].base + d->slot], out[count].value, sizeof(out[count].value));
        ++count;
    }
    return count;
}

/* The variables of the script a frame belongs to (the ones at the top of its file). */
int br_script_debug_globals(int index, br_debug_var *out, int max) {
    sc_vm *vm = g_dbg.vm;
    int n = br_script_debug_frame_count(), i, count = 0;
    const sc_instance *inst;
    if (!vm) return 0;
    inst = index >= 0 && index < n ? vm->frames[n - 1 - index].inst : vm->inst;
    if (!inst || !inst->prog || !inst->globals) return 0;
    for (i = 0; i < inst->prog->global_count && count < max; ++i) {
        snprintf(out[count].name, sizeof(out[count].name), "%s", inst->prog->globals[i].name);
        sc_debug_value(vm, &inst->globals[i], out[count].value, sizeof(out[count].value));
        ++count;
    }
    return count;
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

bool br_script_get_vec2(br_world *w, br_entity e, const char *var, double *x, double *y) {
    sc_instance *inst = sc_running(w, e);
    int g = inst ? sc_find_global(inst->prog, var) : -1;
    if (g < 0 || inst->globals[g].t != V_VEC) return false;
    *x = inst->globals[g].as.v.x;
    *y = inst->globals[g].as.v.y;
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

bool br_script_get_entity(br_world *w, br_entity e, const char *var, br_entity *out) {
    sc_instance *inst = sc_running(w, e);
    int g = inst ? sc_find_global(inst->prog, var) : -1;
    if (g < 0 || inst->globals[g].t != V_ENT) return false;
    *out = inst->globals[g].as.e;
    return br_entity_alive(w, *out);
}

bool br_script_get_string(br_world *w, br_entity e, const char *var, char *out, size_t size) {
    sc_instance *inst = sc_running(w, e);
    int g = inst ? sc_find_global(inst->prog, var) : -1;
    char buf[8192];
    sc_vm vm;
    if (g < 0 || size == 0) return false;
    if (inst->globals[g].t == V_STR) { snprintf(out, size, "%s", inst->globals[g].as.s->chars); return true; }
    memset(&vm, 0, offsetof(sc_vm, frames));
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

bool br_script_push_request(br_world *w, char *path, size_t size, bool *transparent, bool *update_below) {
    sc_runtime *rt = (sc_runtime *)br_world_attached(w, &k_runtime_key);
    if (!rt || !rt->push_request[0]) return false;
    snprintf(path, size, "%s", rt->push_request);
    if (transparent) *transparent = rt->push_transparent;
    if (update_below) *update_below = rt->push_update_below;
    rt->push_request[0] = 0;
    return true;
}

bool br_script_pop_request(br_world *w) {
    sc_runtime *rt = (sc_runtime *)br_world_attached(w, &k_runtime_key);
    bool p = rt && rt->pop_request;
    if (rt) rt->pop_request = false;
    return p;
}

int br_script_notify(br_world *w, const char *function) {
    sc_runtime *rt = (sc_runtime *)br_world_attached(w, &k_runtime_key);
    br_entity *targets;
    int count = 0, i, called = 0;
    if (!rt || !rt->instance_count || !(targets = (br_entity *)br_alloc(sizeof(br_entity) * (size_t)rt->instance_count))) return 0;
    for (i = 0; i < rt->instance_count; ++i) targets[count++] = rt->instances[i]->e;
    for (i = 0; i < count; ++i) {
        sc_instance *inst = sc_instance_of(rt, targets[i]);
        int fn;
        if (!inst || !inst->prog || inst->failed || !inst->started || (fn = sc_find_func(inst->prog, function)) < 0) continue;
        sc_run(rt, inst, fn, NULL, 0, NULL);
        ++called;
    }
    br_free(targets);
    sc_collect(rt);
    return called;
}

bool br_script_draw_layer(const br_world *w, br_entity e, int *layer) {
    sc_runtime *rt = (sc_runtime *)br_world_attached(w, &k_runtime_key);
    sc_instance *inst = rt ? sc_instance_of(rt, e) : NULL;
    int g;
    if (!inst || !inst->prog || !inst->started || inst->failed || inst->prog->callbacks[CB_DRAW] < 0) return false;
    *layer = 0;
    if ((g = sc_find_global(inst->prog, "draw_layer")) >= 0 && inst->globals[g].t == V_NUM) *layer = (int)floor(inst->globals[g].as.n);
    return true;
}

void br_script_draw(br_world *w, br_entity e, br_renderer *r) {
    sc_runtime *rt = (sc_runtime *)br_world_attached(w, &k_runtime_key);
    sc_instance *inst = rt ? sc_instance_of(rt, e) : NULL;
    if (!inst || !inst->prog || !inst->started || inst->failed || rt->draw_r) return;
    rt->draw_r = r;
    sc_callback(rt, inst, CB_DRAW, NULL, 0);
    rt->draw_r = NULL;
    br_set_blend(r, BR_BLEND_ALPHA);
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
