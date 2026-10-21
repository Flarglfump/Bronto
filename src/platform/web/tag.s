# Bronto web runtime - the exception tag that setjmp / longjmp throw with.
#
# clang -mllvm -wasm-enable-sjlj turns longjmp into a WebAssembly exception
# thrown with the tag __c_longjmp. Up to LLVM 19 every object file carried
# its own weak copy of the tag; from LLVM 20 on, the C library has to define
# it, as wasi-libc and Emscripten do. This strong definition serves both:
# older compilers' weak copies give way to it. It is assembly because clang
# 18 can't compile a tag written as inline assembly in a C file.

        .globl  __c_longjmp
        .tagtype __c_longjmp i32
__c_longjmp:
