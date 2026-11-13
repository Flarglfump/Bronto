/*
 * The Metal presenter (src/platform/gpu_metal.c), type-checked on a computer
 * without macOS's headers: the few Objective-C runtime and CoreGraphics
 * declarations it uses are stood in for below, with the Cocoa backend's
 * message helpers. Compiled only (make gpu-check); nothing here runs.
 */
#include "bronto/bronto.h"
#include <stdio.h>
#include <string.h>

typedef struct objc_object *id;
typedef struct objc_selector *SEL;
typedef signed char BOOL;
#define YES ((BOOL)1)
#define NO ((BOOL)0)
void objc_msgSend(void);
SEL sel_registerName(const char *name);
void *objc_getClass(const char *name);
typedef struct { double x, y; } CGPoint;
typedef struct { double width, height; } CGSize;
typedef struct { CGPoint origin; CGSize size; } CGRect;
typedef struct CGColorSpace *CGColorSpaceRef;

/* (as platform_macos.c has them) */
#define SEL_(s) sel_registerName(s)
static id cls(const char *name) { return (id)objc_getClass(name); }
static id msg_id(id o, const char *s) { return ((id (*)(id, SEL))objc_msgSend)(o, SEL_(s)); }
static id msg_id_id(id o, const char *s, id a) { return ((id (*)(id, SEL, id))objc_msgSend)(o, SEL_(s), a); }
static void msg_v(id o, const char *s) { ((void (*)(id, SEL))objc_msgSend)(o, SEL_(s)); }
static void msg_v_id(id o, const char *s, id a) { ((void (*)(id, SEL, id))objc_msgSend)(o, SEL_(s), a); }
static void msg_v_bool(id o, const char *s, BOOL b) { ((void (*)(id, SEL, BOOL))objc_msgSend)(o, SEL_(s), b); }
static void msg_v_ulong(id o, const char *s, unsigned long v) { ((void (*)(id, SEL, unsigned long))objc_msgSend)(o, SEL_(s), v); }
static void msg_v_double(id o, const char *s, double v) { ((void (*)(id, SEL, double))objc_msgSend)(o, SEL_(s), v); }
static void msg_v_size(id o, const char *s, CGSize v) { ((void (*)(id, SEL, CGSize))objc_msgSend)(o, SEL_(s), v); }
static void msg_v_rect(id o, const char *s, CGRect v) { ((void (*)(id, SEL, CGRect))objc_msgSend)(o, SEL_(s), v); }
static void msg_v_ptr(id o, const char *s, const void *p) { ((void (*)(id, SEL, const void *))objc_msgSend)(o, SEL_(s), p); }
static const char *msg_cstr(id o, const char *s) { return ((const char *(*)(id, SEL))objc_msgSend)(o, SEL_(s)); }
static id nsstring(const char *utf8) {
    return ((id (*)(id, SEL, const char *))objc_msgSend)(cls("NSString"), SEL_("stringWithUTF8String:"), utf8 ? utf8 : "");
}
static id alloc_init(const char *class_name) { return msg_id(msg_id(cls(class_name), "alloc"), "init"); }

#include "../../src/platform/gpu_metal.c"

_Static_assert(sizeof(br_MTLRegion) == 48, "MTLRegion");
_Static_assert(sizeof(br_MTLViewport) == 48, "MTLViewport");
_Static_assert(sizeof(br_MTLClearColor) == 32, "MTLClearColor");

void gpu_mac_check_uses(void);
void gpu_mac_check_uses(void) {
    br_recti r = { 0, 0, 0, 0 };
    CGRect b;
    memset(&b, 0, sizeof(b));
    mtl_start((id)0, NULL);
    mtl_present(NULL, r, 0, 0, 1.0, b);
    mtl_stop();
}
