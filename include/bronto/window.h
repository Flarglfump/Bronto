/*
 * Bronto Engine - window management.
 *
 * The game renders into a fixed-size framebuffer (e.g. 320x180). The window
 * can be any size; the framebuffer is presented into a *viewport* inside
 * the window according to a scale mode:
 *
 *   BR_SCALE_FIT       largest size that preserves aspect ratio; bars fill
 *                      the rest (letterbox / pillarbox). Default.
 *   BR_SCALE_INTEGER   largest whole-number multiple that fits, centred -
 *                      crisp, evenly sized pixels for pixel art. Falls back
 *                      to FIT when the window is smaller than the framebuffer.
 *   BR_SCALE_STRETCH   fill the whole window, ignoring aspect ratio.
 *   BR_SCALE_RESIZE    the framebuffer itself is resized to follow the window
 *                      (window pixels / pixel_scale), for games that show
 *                      more of the world in a bigger window.
 *
 * All of the policy (viewport maths, mouse mapping, state tracking) lives
 * here in portable code; platform backends only report window pixel sizes
 * and blit the framebuffer into the rectangle they are given.
 *
 * Units: "points" are the OS's logical units (what window sizes are
 * specified in); "pixels" are physical. pixels = points * scale, where
 * scale is 2 on a Retina display, 1.5 at 150% on Windows, etc.
 */
#ifndef BRONTO_WINDOW_H
#define BRONTO_WINDOW_H

#include "types.h"
#include "bmath.h"
#include "render.h"

typedef enum br_scale_mode {
    BR_SCALE_FIT = 0,
    BR_SCALE_INTEGER,
    BR_SCALE_STRETCH,
    BR_SCALE_RESIZE
} br_scale_mode;

typedef struct br_viewport {
    br_recti rect;          /* where the framebuffer lands, window pixels */
    float scale_x, scale_y; /* window pixels per framebuffer pixel */
} br_viewport;

/* Pure functions - no platform involved. */
br_viewport br_viewport_compute(int fb_w, int fb_h, int win_w, int win_h, br_scale_mode mode);
br_vec2 br_viewport_to_framebuffer(const br_viewport *vp, br_vec2 window_px);
br_vec2 br_viewport_to_window(const br_viewport *vp, br_vec2 fb_px);
/* Software presentation helper for backends without a scaling blitter
 * (X11, web): scales `fb` into `vp` inside a 32-bit destination (nearest
 * neighbour), converting RGBA to the native layout given by channel shifts
 * (alpha_shift < 0 = no alpha channel), and clears the rest to black. */
void br_viewport_blit(const br_image *fb, br_recti vp, uint32_t *dst, int dst_w, int dst_h,
                      size_t dst_stride_px, int r_shift, int g_shift, int b_shift, int a_shift);

/* Framebuffer size for BR_SCALE_RESIZE (at least 1x1). */
void br_viewport_resize_dims(int win_w, int win_h, int pixel_scale, int *fb_w, int *fb_h);

/* Live window state, kept up to date by the engine from window events. */
typedef struct br_window_state {
    int width, height;        /* client area, pixels */
    int x, y;                 /* position (points), when the OS reports it */
    float scale;              /* pixels per point */
    bool focused;
    bool minimized;
    bool maximized;
    bool fullscreen;
    bool mouse_inside;
    bool cursor_visible;
    br_scale_mode scale_mode;
    int pixel_scale;          /* BR_SCALE_RESIZE: window pixels per fb pixel */
    br_viewport viewport;
} br_window_state;

/* ---- engine-level window control (see engine.h for br_engine) ---------- */
struct br_engine;

void  br_window_set_title(struct br_engine *e, const char *title);
/* Client-area size in points (the OS may clamp it). */
void  br_window_set_size(struct br_engine *e, int width_points, int height_points);
void  br_window_get_size(const struct br_engine *e, int *width_px, int *height_px);
void  br_window_set_fullscreen(struct br_engine *e, bool fullscreen);
void  br_window_toggle_fullscreen(struct br_engine *e);
bool  br_window_is_fullscreen(const struct br_engine *e);
void  br_window_set_cursor_visible(struct br_engine *e, bool visible);
void  br_window_set_scale_mode(struct br_engine *e, br_scale_mode mode);
float br_window_scale(const struct br_engine *e);
bool  br_window_is_focused(const struct br_engine *e);
bool  br_window_is_minimized(const struct br_engine *e);
const br_window_state *br_window_state_get(const struct br_engine *e);

#endif /* BRONTO_WINDOW_H */
