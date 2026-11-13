/*
 * Bronto Engine - showing the framebuffer through Metal (platform.h, br_gpu).
 *
 * macOS. Metal.framework is opened at run time (MTLCreateSystemDefaultDevice is
 * its one C function) and the rest is Objective-C, called from C through the
 * runtime like the rest of the Cocoa backend; CAMetalLayer is QuartzCore's,
 * which the backend links already. Nothing else is needed to build.
 *
 * A CAMetalLayer covers the view. Each frame the framebuffer is copied into a
 * texture the frame's size, the drawable is cleared black, and one triangle
 * covering the viewport samples the texture with a nearest-neighbour sampler.
 * The two shaders are Metal source, compiled when it starts.
 *
 * Included by platform_macos.c (after its msg_* helpers), which calls
 * mtl_start(view_layer), mtl_present(frame, viewport, w, h, scale, bounds) and
 * mtl_stop().
 */
#include <dlfcn.h>

typedef struct { unsigned long x, y, z; } br_MTLOrigin;
typedef struct { unsigned long width, height, depth; } br_MTLSize;
typedef struct { br_MTLOrigin origin; br_MTLSize size; } br_MTLRegion;
typedef struct { double red, green, blue, alpha; } br_MTLClearColor;
typedef struct { double originX, originY, width, height, znear, zfar; } br_MTLViewport;

#define MTL_PIXEL_RGBA8 70
#define MTL_PIXEL_BGRA8 80
#define MTL_LOAD_CLEAR 2
#define MTL_STORE_STORE 1
#define MTL_TRIANGLE 3

static const char k_mtl_source[] =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct V { float4 pos [[position]]; float2 uv; };\n"
    "vertex V vs(uint id [[vertex_id]]) {\n"               /* one triangle over the viewport */
    "    V o;\n"
    "    float2 p = float2(float((id << 1) & 2u), float(id & 2u));\n"
    "    o.pos = float4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);\n"
    "    o.uv = p;\n"
    "    return o;\n"
    "}\n"
    "fragment float4 ps(V i [[stage_in]], texture2d<float> frame [[texture(0)]]) {\n"
    "    constexpr sampler nearest(filter::nearest, address::clamp_to_edge);\n"
    "    return frame.sample(nearest, i.uv);\n"
    "}\n";

static struct {
    void *lib;
    id device, queue, layer, pipeline, texture;
    int tex_w, tex_h;
} MTL;

typedef id (*MTLCreateSystemDefaultDevice_fn)(void);

static id mtl_msg_id_id_id_ptr(id o, const char *s, id a, id b, id *c) {
    return ((id (*)(id, SEL, id, id, id *))objc_msgSend)(o, SEL_(s), a, b, c);
}
static id mtl_msg_id_id_ptr(id o, const char *s, id a, id *b) { return ((id (*)(id, SEL, id, id *))objc_msgSend)(o, SEL_(s), a, b); }
static id mtl_msg_id_ulong(id o, const char *s, unsigned long v) { return ((id (*)(id, SEL, unsigned long))objc_msgSend)(o, SEL_(s), v); }

/* An NSError's text, for the log. */
static const char *mtl_error_text(id error) {
    id text = error ? msg_id(error, "localizedDescription") : (id)0;
    return text ? msg_cstr(text, "UTF8String") : "?";
}

static void mtl_stop(void) {
    if (MTL.layer) { msg_v(MTL.layer, "removeFromSuperlayer"); msg_v(MTL.layer, "release"); }
    if (MTL.texture) msg_v(MTL.texture, "release");
    if (MTL.pipeline) msg_v(MTL.pipeline, "release");
    if (MTL.queue) msg_v(MTL.queue, "release");
    if (MTL.device) msg_v(MTL.device, "release");
    {
        void *lib = MTL.lib;
        memset(&MTL, 0, sizeof(MTL));
        MTL.lib = lib;
    }
}

/* Adds a Metal layer to `host` (the view's layer). */
static bool mtl_start(id host, CGColorSpaceRef colorspace) {
    MTLCreateSystemDefaultDevice_fn create;
    id library, vs, ps, desc, attachment, error = (id)0;
    char info[200];
    if (!cls("CAMetalLayer")) { BR_INFO("Metal: no CAMetalLayer (macOS 10.11 or later)"); return false; }
    if (!MTL.lib) MTL.lib = dlopen("/System/Library/Frameworks/Metal.framework/Metal", RTLD_LAZY | RTLD_LOCAL);
    {
        void *p = MTL.lib ? dlsym(MTL.lib, "MTLCreateSystemDefaultDevice") : NULL;
        memcpy(&create, &p, sizeof(p));
    }
    if (!create) { BR_INFO("Metal: not on this Mac"); return false; }
    if (!(MTL.device = create())) { BR_INFO("Metal: no GPU for it"); return false; }
    MTL.queue = msg_id(MTL.device, "newCommandQueue");
    library = mtl_msg_id_id_id_ptr(MTL.device, "newLibraryWithSource:options:error:", nsstring(k_mtl_source), (id)0, &error);
    if (!library) { BR_INFO("Metal: the shaders didn't compile: %s", mtl_error_text(error)); mtl_stop(); return false; }
    vs = msg_id_id(library, "newFunctionWithName:", nsstring("vs"));
    ps = msg_id_id(library, "newFunctionWithName:", nsstring("ps"));
    desc = alloc_init("MTLRenderPipelineDescriptor");
    msg_v_id(desc, "setVertexFunction:", vs);
    msg_v_id(desc, "setFragmentFunction:", ps);
    attachment = mtl_msg_id_ulong(msg_id(desc, "colorAttachments"), "objectAtIndexedSubscript:", 0);
    msg_v_ulong(attachment, "setPixelFormat:", MTL_PIXEL_BGRA8);
    error = (id)0;
    MTL.pipeline = mtl_msg_id_id_ptr(MTL.device, "newRenderPipelineStateWithDescriptor:error:", desc, &error);
    msg_v(desc, "release");
    if (vs) msg_v(vs, "release");
    if (ps) msg_v(ps, "release");
    msg_v(library, "release");
    if (!MTL.pipeline || !MTL.queue) { BR_INFO("Metal: no pipeline: %s", mtl_error_text(error)); mtl_stop(); return false; }
    MTL.layer = msg_id(msg_id(cls("CAMetalLayer"), "layer"), "retain");
    msg_v_id(MTL.layer, "setDevice:", MTL.device);
    msg_v_ulong(MTL.layer, "setPixelFormat:", MTL_PIXEL_BGRA8);
    msg_v_bool(MTL.layer, "setFramebufferOnly:", YES);
    if (colorspace) msg_v_ptr(MTL.layer, "setColorspace:", colorspace);
    msg_v_id(host, "addSublayer:", MTL.layer);
    {
        id name = msg_id(MTL.device, "name");
        snprintf(info, sizeof(info), "Metal, %.150s", name ? msg_cstr(name, "UTF8String") : "?");
    }
    br_gpu_set_active(BR_GPU_METAL, info);
    return true;
}

/* Shows `fb` in `vp` (pixels, y down) of a w x h pixel view, `bounds` in points. */
static void mtl_present(const br_image *fb, br_recti vp, int w, int h, double scale, CGRect bounds) {
    id drawable, pass, attachment, cmd, enc;
    CGSize size;
    br_MTLClearColor black = { 0, 0, 0, 1 };
    if (w <= 0 || h <= 0 || !fb || fb->width <= 0 || fb->height <= 0) return;
    msg_v(cls("CATransaction"), "begin");
    msg_v_bool(cls("CATransaction"), "setDisableActions:", YES);
    msg_v_rect(MTL.layer, "setFrame:", bounds);
    msg_v_double(MTL.layer, "setContentsScale:", scale);
    size.width = w;
    size.height = h;
    msg_v_size(MTL.layer, "setDrawableSize:", size);
    msg_v(cls("CATransaction"), "commit");
    if (fb->width != MTL.tex_w || fb->height != MTL.tex_h) {   /* the frame's texture, made again when its size changes */
        id td = ((id (*)(id, SEL, unsigned long, unsigned long, unsigned long, BOOL))objc_msgSend)(cls("MTLTextureDescriptor"),
                    SEL_("texture2DDescriptorWithPixelFormat:width:height:mipmapped:"), MTL_PIXEL_RGBA8,
                    (unsigned long)fb->width, (unsigned long)fb->height, NO);
        if (MTL.texture) msg_v(MTL.texture, "release");
        MTL.texture = msg_id_id(MTL.device, "newTextureWithDescriptor:", td);
        MTL.tex_w = MTL.texture ? fb->width : 0;
        MTL.tex_h = MTL.texture ? fb->height : 0;
        if (!MTL.texture) return;
    }
    {
        br_MTLRegion region;
        memset(&region, 0, sizeof(region));
        region.size.width = (unsigned long)fb->width;
        region.size.height = (unsigned long)fb->height;
        region.size.depth = 1;
        ((void (*)(id, SEL, br_MTLRegion, unsigned long, const void *, unsigned long))objc_msgSend)(MTL.texture,
            SEL_("replaceRegion:mipmapLevel:withBytes:bytesPerRow:"), region, 0, fb->pixels, (unsigned long)fb->width * 4u);
    }
    if (!(drawable = msg_id(MTL.layer, "nextDrawable"))) return;   /* (none free this moment: skip the frame) */
    pass = msg_id(cls("MTLRenderPassDescriptor"), "renderPassDescriptor");
    attachment = mtl_msg_id_ulong(msg_id(pass, "colorAttachments"), "objectAtIndexedSubscript:", 0);
    msg_v_id(attachment, "setTexture:", msg_id(drawable, "texture"));
    msg_v_ulong(attachment, "setLoadAction:", MTL_LOAD_CLEAR);
    msg_v_ulong(attachment, "setStoreAction:", MTL_STORE_STORE);
    ((void (*)(id, SEL, br_MTLClearColor))objc_msgSend)(attachment, SEL_("setClearColor:"), black);
    cmd = msg_id(MTL.queue, "commandBuffer");
    enc = msg_id_id(cmd, "renderCommandEncoderWithDescriptor:", pass);
    if (vp.w > 0 && vp.h > 0) {
        br_MTLViewport v;
        v.originX = vp.x;
        v.originY = vp.y;
        v.width = vp.w;
        v.height = vp.h;
        v.znear = 0;
        v.zfar = 1;
        msg_v_id(enc, "setRenderPipelineState:", MTL.pipeline);
        ((void (*)(id, SEL, br_MTLViewport))objc_msgSend)(enc, SEL_("setViewport:"), v);
        ((void (*)(id, SEL, id, unsigned long))objc_msgSend)(enc, SEL_("setFragmentTexture:atIndex:"), MTL.texture, 0);
        ((void (*)(id, SEL, unsigned long, unsigned long, unsigned long))objc_msgSend)(enc, SEL_("drawPrimitives:vertexStart:vertexCount:"),
                                                                                         MTL_TRIANGLE, 0, 3);
    }
    msg_v(enc, "endEncoding");
    msg_v_id(cmd, "presentDrawable:", drawable);
    msg_v(cmd, "commit");
}
