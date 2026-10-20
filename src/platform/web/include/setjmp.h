/* Bronto web C library: <setjmp.h>. setjmp/longjmp are lowered by clang
 * (-mllvm -wasm-enable-sjlj) onto WebAssembly exceptions; the runtime
 * functions it calls are in src/platform/web/libc.c. */
#ifndef BW_SETJMP_H
#define BW_SETJMP_H
#include "_bw_common.h"
BW_BEGIN
typedef struct { unsigned long long __bw_slots[4]; } jmp_buf[1];
int setjmp(jmp_buf env) __attribute__((returns_twice));
BW_NORETURN void longjmp(jmp_buf env, int value);
BW_END
#endif
