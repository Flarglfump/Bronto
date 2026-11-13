/*
 * Bronto Engine - showing the framebuffer through OpenGL (platform.h, br_gpu).
 *
 * Shared by the native backends: each makes an OpenGL context for its window
 * its own way (GLX, WGL, macOS's NSOpenGLContext), hands this file a function
 * that finds GL's functions, and swaps the buffers after gl_present.
 *
 * Only OpenGL 1.1 is used - one texture, the frame copied into it, drawn as one
 * quad with nearest-neighbour filtering into the viewport, the window cleared
 * black around it - so any OpenGL there is will do, from an old laptop's to
 * macOS's legacy profile to Mesa's software one. The texture is a power of two
 * in size (1.1 has nothing else), with the frame in its top-left corner.
 *
 * The few declarations it needs are below: no OpenGL headers or libraries to
 * build with. The platform loads the library at run time.
 *
 * Included by the native backends (platform_x11.c, platform_win32.c,
 * platform_macos.c).
 */
#if defined(_WIN32)
#  define BR_GLAPI __stdcall
#else
#  define BR_GLAPI
#endif

typedef unsigned int br_GLenum;
typedef unsigned int br_GLuint;
typedef int br_GLint;
typedef int br_GLsizei;
typedef float br_GLfloat;
typedef double br_GLdouble;
typedef unsigned int br_GLbitfield;
typedef unsigned char br_GLubyte;

#define BR_GL_COLOR_BUFFER_BIT 0x4000
#define BR_GL_QUADS 0x0007
#define BR_GL_TEXTURE_2D 0x0DE1
#define BR_GL_RGBA 0x1908
#define BR_GL_UNSIGNED_BYTE 0x1401
#define BR_GL_TEXTURE_MAG_FILTER 0x2800
#define BR_GL_TEXTURE_MIN_FILTER 0x2801
#define BR_GL_TEXTURE_WRAP_S 0x2802
#define BR_GL_TEXTURE_WRAP_T 0x2803
#define BR_GL_NEAREST 0x2600
#define BR_GL_CLAMP 0x2900
#define BR_GL_UNPACK_ALIGNMENT 0x0CF5
#define BR_GL_UNPACK_ROW_LENGTH 0x0CF2
#define BR_GL_PROJECTION 0x1701
#define BR_GL_MODELVIEW 0x1700
#define BR_GL_TEXTURE 0x1702
#define BR_GL_DEPTH_TEST 0x0B71
#define BR_GL_BLEND 0x0BE2
#define BR_GL_SCISSOR_TEST 0x0C11
#define BR_GL_CULL_FACE 0x0B44
#define BR_GL_LIGHTING 0x0B50
#define BR_GL_DITHER 0x0BD0
#define BR_GL_TEXTURE_ENV 0x2300
#define BR_GL_TEXTURE_ENV_MODE 0x2200
#define BR_GL_REPLACE 0x1E01
#define BR_GL_VENDOR 0x1F00
#define BR_GL_RENDERER 0x1F01
#define BR_GL_VERSION 0x1F02
#define BR_GL_MAX_TEXTURE_SIZE 0x0D33

#define GL_FUNCS(F) \
    F(void, Viewport, (br_GLint, br_GLint, br_GLsizei, br_GLsizei)) \
    F(void, ClearColor, (br_GLfloat, br_GLfloat, br_GLfloat, br_GLfloat)) \
    F(void, Clear, (br_GLbitfield)) \
    F(void, Enable, (br_GLenum)) \
    F(void, Disable, (br_GLenum)) \
    F(void, GenTextures, (br_GLsizei, br_GLuint *)) \
    F(void, DeleteTextures, (br_GLsizei, const br_GLuint *)) \
    F(void, BindTexture, (br_GLenum, br_GLuint)) \
    F(void, TexParameteri, (br_GLenum, br_GLenum, br_GLint)) \
    F(void, TexEnvi, (br_GLenum, br_GLenum, br_GLint)) \
    F(void, TexImage2D, (br_GLenum, br_GLint, br_GLint, br_GLsizei, br_GLsizei, br_GLint, br_GLenum, br_GLenum, const void *)) \
    F(void, TexSubImage2D, (br_GLenum, br_GLint, br_GLint, br_GLint, br_GLsizei, br_GLsizei, br_GLenum, br_GLenum, const void *)) \
    F(void, PixelStorei, (br_GLenum, br_GLint)) \
    F(void, MatrixMode, (br_GLenum)) \
    F(void, LoadIdentity, (void)) \
    F(void, Begin, (br_GLenum)) \
    F(void, End, (void)) \
    F(void, TexCoord2f, (br_GLfloat, br_GLfloat)) \
    F(void, Vertex2f, (br_GLfloat, br_GLfloat)) \
    F(void, Color4f, (br_GLfloat, br_GLfloat, br_GLfloat, br_GLfloat)) \
    F(void, GetIntegerv, (br_GLenum, br_GLint *)) \
    F(const br_GLubyte *, GetString, (br_GLenum)) \
    F(br_GLenum, GetError, (void))

#define GL_DECLARE(ret, name, args) typedef ret (BR_GLAPI *br_gl_##name##_fn) args;
GL_FUNCS(GL_DECLARE)
#undef GL_DECLARE

static struct {
#define GL_FIELD(ret, name, args) br_gl_##name##_fn name;
    GL_FUNCS(GL_FIELD)
#undef GL_FIELD
    br_GLuint texture;
    int tex_w, tex_h;                 /* the texture's size (powers of two) */
    int max_size;
} GLP;

typedef void *(*br_gl_loader)(const char *name);

/* Finds the functions (through the platform's loader) and checks the context works. */
static bool gl_begin(br_gl_loader load, char *info, size_t info_size) {
    const char *version, *renderer;
    memset(&GLP, 0, sizeof(GLP));
#define GL_LOAD(ret, name, args) \
    { union { void *o; br_gl_##name##_fn f; } u_; u_.o = load("gl" #name); GLP.name = u_.f; \
      if (!GLP.name) { BR_WARN("OpenGL: no gl%s", #name); return false; } }
    GL_FUNCS(GL_LOAD)
#undef GL_LOAD
    version = (const char *)GLP.GetString(BR_GL_VERSION);
    renderer = (const char *)GLP.GetString(BR_GL_RENDERER);
    if (!version) { BR_WARN("OpenGL: the context doesn't answer"); return false; }
    GLP.GetIntegerv(BR_GL_MAX_TEXTURE_SIZE, &GLP.max_size);
    if (GLP.max_size < 256) GLP.max_size = 1024;            /* (1.1 promises 64; every real one has far more) */
    snprintf(info, info_size, "OpenGL %s, %s", version, renderer ? renderer : "?");
    return true;
}

static int gl_pow2(int n) {
    int p = 64;
    while (p < n && p < (1 << 15)) p <<= 1;
    return p;
}

/* Shows `fb` scaled into `vp` (window pixels, y down) of a win_w x win_h window,
 * black around it. The caller swaps the buffers. */
static void gl_present(const br_image *fb, br_recti vp, int win_w, int win_h) {
    float u, v;
    if (win_w <= 0 || win_h <= 0) return;
    GLP.Viewport(0, 0, win_w, win_h);
    GLP.ClearColor(0, 0, 0, 1);
    GLP.Clear(BR_GL_COLOR_BUFFER_BIT);
    if (!fb || fb->width <= 0 || fb->height <= 0 || fb->width > GLP.max_size || fb->height > GLP.max_size) return;
    if (!GLP.texture || fb->width > GLP.tex_w || fb->height > GLP.tex_h) {   /* (made again only when the frame outgrows it) */
        if (GLP.texture) GLP.DeleteTextures(1, &GLP.texture);
        GLP.GenTextures(1, &GLP.texture);
        GLP.BindTexture(BR_GL_TEXTURE_2D, GLP.texture);
        GLP.tex_w = gl_pow2(fb->width);
        GLP.tex_h = gl_pow2(fb->height);
        GLP.TexParameteri(BR_GL_TEXTURE_2D, BR_GL_TEXTURE_MAG_FILTER, BR_GL_NEAREST);
        GLP.TexParameteri(BR_GL_TEXTURE_2D, BR_GL_TEXTURE_MIN_FILTER, BR_GL_NEAREST);
        GLP.TexParameteri(BR_GL_TEXTURE_2D, BR_GL_TEXTURE_WRAP_S, BR_GL_CLAMP);
        GLP.TexParameteri(BR_GL_TEXTURE_2D, BR_GL_TEXTURE_WRAP_T, BR_GL_CLAMP);
        GLP.TexImage2D(BR_GL_TEXTURE_2D, 0, BR_GL_RGBA, GLP.tex_w, GLP.tex_h, 0, BR_GL_RGBA, BR_GL_UNSIGNED_BYTE, NULL);
    }
    GLP.BindTexture(BR_GL_TEXTURE_2D, GLP.texture);
    GLP.PixelStorei(BR_GL_UNPACK_ALIGNMENT, 4);
    GLP.TexSubImage2D(BR_GL_TEXTURE_2D, 0, 0, 0, fb->width, fb->height, BR_GL_RGBA, BR_GL_UNSIGNED_BYTE, fb->pixels);
    if (vp.w <= 0 || vp.h <= 0) return;
    GLP.Viewport(vp.x, win_h - vp.y - vp.h, vp.w, vp.h);   /* (GL counts rows from the bottom) */
    GLP.MatrixMode(BR_GL_PROJECTION);
    GLP.LoadIdentity();
    GLP.MatrixMode(BR_GL_MODELVIEW);
    GLP.LoadIdentity();
    GLP.Disable(BR_GL_DEPTH_TEST);
    GLP.Disable(BR_GL_BLEND);
    GLP.Disable(BR_GL_LIGHTING);
    GLP.Disable(BR_GL_CULL_FACE);
    GLP.Disable(BR_GL_DITHER);
    GLP.Enable(BR_GL_TEXTURE_2D);
    GLP.TexEnvi(BR_GL_TEXTURE_ENV, BR_GL_TEXTURE_ENV_MODE, BR_GL_REPLACE);
    GLP.Color4f(1, 1, 1, 1);
    u = (float)fb->width / (float)GLP.tex_w;
    v = (float)fb->height / (float)GLP.tex_h;
    GLP.Begin(BR_GL_QUADS);                                 /* row 0 of the frame at the top */
    GLP.TexCoord2f(0, 0); GLP.Vertex2f(-1, 1);
    GLP.TexCoord2f(u, 0); GLP.Vertex2f(1, 1);
    GLP.TexCoord2f(u, v); GLP.Vertex2f(1, -1);
    GLP.TexCoord2f(0, v); GLP.Vertex2f(-1, -1);
    GLP.End();
}

static void gl_end(void) {
    if (GLP.texture && GLP.DeleteTextures) GLP.DeleteTextures(1, &GLP.texture);
    GLP.texture = 0;
}
