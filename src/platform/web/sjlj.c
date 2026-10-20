/*
 * Bronto web runtime - what setjmp / longjmp need at run time.
 * (Kept apart from any code that calls setjmp: clang's lowering of
 * setjmp trips over these definitions if they share a file with it.)
 */
#include "bw.h"

#include <stdlib.h>

/* ==== setjmp / longjmp ======================================================
 * clang -mllvm -wasm-enable-sjlj turns setjmp/longjmp into WebAssembly
 * exceptions and calls these helpers. Their names and arguments changed in
 * LLVM 19, so both sets are here; each compiler uses its own. (They follow
 * the definitions in wasi-libc and Emscripten.) */
struct bw_longjmp_arg { void *env; int val; };
static struct bw_longjmp_arg g_longjmp_arg;

void __wasm_longjmp(void *env, int val);
void __wasm_longjmp(void *env, int val) {
    g_longjmp_arg.env = env;
    g_longjmp_arg.val = val ? val : 1;
    __builtin_wasm_throw(1, &g_longjmp_arg);
}

/* LLVM 19 and later. */
struct bw_jmp_state { void *invocation; uint32_t label; };
void __wasm_setjmp(void *env, uint32_t label, void *invocation);
uint32_t __wasm_setjmp_test(void *env, void *invocation);
void __wasm_setjmp(void *env, uint32_t label, void *invocation) {
    struct bw_jmp_state *st = (struct bw_jmp_state *)env;
    st->invocation = invocation;
    st->label = label;
}
uint32_t __wasm_setjmp_test(void *env, void *invocation) {
    struct bw_jmp_state *st = (struct bw_jmp_state *)env;
    return st->invocation == invocation ? st->label : 0;
}

/* LLVM 18 and earlier: a table of (setjmp id, label) pairs per function. */
struct bw_sj_entry { uint32_t id, label; };
static uint32_t g_sj_next_id, g_sj_temp;
void *saveSetjmp(void *env, uint32_t label, void *table, uint32_t size);
uint32_t testSetjmp(uint32_t id, void *table, uint32_t size);
uint32_t getTempRet0(void);
void setTempRet0(uint32_t v);
void *saveSetjmp(void *env, uint32_t label, void *table, uint32_t size) {
    struct bw_sj_entry *e = (struct bw_sj_entry *)table;
    uint32_t i, id = ++g_sj_next_id;
    *(uint32_t *)env = id;
    for (i = 0; i < size; ++i) {
        if (e[i].id == 0) {
            e[i].id = id;
            e[i].label = label;
            e[i + 1].id = 0;                         /* the table always has room for one more */
            g_sj_temp = size;
            return table;
        }
    }
    {   /* full: double it */
        uint32_t grown = size * 2;
        struct bw_sj_entry *t = (struct bw_sj_entry *)realloc(table, sizeof(*e) * (grown + 1));
        if (!t) __builtin_trap();
        t[size].id = id;
        t[size].label = label;
        t[size + 1].id = 0;
        g_sj_temp = grown;
        return t;
    }
}
uint32_t testSetjmp(uint32_t id, void *table, uint32_t size) {
    struct bw_sj_entry *e = (struct bw_sj_entry *)table;
    uint32_t i;
    for (i = 0; i < size && e[i].id; ++i) if (e[i].id == id) return e[i].label;
    return 0;
}
uint32_t getTempRet0(void) { return g_sj_temp; }
void setTempRet0(uint32_t v) { g_sj_temp = v; }

