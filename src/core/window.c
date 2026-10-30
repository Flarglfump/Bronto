/* Window management: viewport maths and the engine-level window API. */
#include "bronto/window.h"
#include "bronto/engine.h"

#include <math.h>
#include <string.h>

br_viewport br_viewport_compute(int fb_w, int fb_h, int win_w, int win_h, br_scale_mode mode) {
    br_viewport vp;
    float sx, sy, s;
    int w, h;
    vp.rect = br_recti_make(0, 0, 0, 0);
    vp.scale_x = vp.scale_y = 1.0f;
    if (fb_w <= 0 || fb_h <= 0 || win_w <= 0 || win_h <= 0) return vp;
    sx = (float)win_w / (float)fb_w;
    sy = (float)win_h / (float)fb_h;

    if (mode == BR_SCALE_STRETCH) {
        vp.rect = br_recti_make(0, 0, win_w, win_h);
        vp.scale_x = sx;
        vp.scale_y = sy;
        return vp;
    }
    s = sx < sy ? sx : sy;
    if ((mode == BR_SCALE_INTEGER || mode == BR_SCALE_RESIZE) && s >= 1.0f) {
        s = floorf(s);
        w = fb_w * (int)s;
        h = fb_h * (int)s;
    } else {
        /* FIT: keep aspect; snap the long side exactly to the window. */
        if (sx < sy) { w = win_w; h = (int)floorf((float)fb_h * s + 0.5f); }
        else         { h = win_h; w = (int)floorf((float)fb_w * s + 0.5f); }
        if (w > win_w) w = win_w;
        if (h > win_h) h = win_h;
        if (w < 1) w = 1;
        if (h < 1) h = 1;
    }
    vp.rect = br_recti_make((win_w - w) / 2, (win_h - h) / 2, w, h);
    vp.scale_x = (float)w / (float)fb_w;
    vp.scale_y = (float)h / (float)fb_h;
    return vp;
}

br_vec2 br_viewport_to_framebuffer(const br_viewport *vp, br_vec2 p) {
    return br_v2((p.x - (float)vp->rect.x) / vp->scale_x, (p.y - (float)vp->rect.y) / vp->scale_y);
}

br_vec2 br_viewport_to_window(const br_viewport *vp, br_vec2 p) {
    return br_v2(p.x * vp->scale_x + (float)vp->rect.x, p.y * vp->scale_y + (float)vp->rect.y);
}

void br_viewport_resize_dims(int win_w, int win_h, int pixel_scale, int *fb_w, int *fb_h) {
    if (pixel_scale < 1) pixel_scale = 1;
    *fb_w = win_w / pixel_scale;
    *fb_h = win_h / pixel_scale;
    if (*fb_w < 1) *fb_w = 1;
    if (*fb_h < 1) *fb_h = 1;
}

void br_viewport_blit(const br_image *fb, br_recti vp, uint32_t *dst, int dst_w, int dst_h,
                      size_t stride, int rs, int gs, int bs, int as) {
    br_recti clip;
    int x, y, prev_sy = -1;
    int *xmap;
    uint32_t *native;
    if (!fb->pixels || dst_w <= 0 || dst_h <= 0) return;
    clip = br_recti_intersect(vp, br_recti_make(0, 0, dst_w, dst_h));
    /* bars */
    for (y = 0; y < dst_h; ++y) {
        uint32_t *row = dst + (size_t)y * stride;
        if (y < clip.y || y >= clip.y + clip.h || clip.w <= 0) {
            for (x = 0; x < dst_w; ++x) row[x] = 0;
        } else {
            for (x = 0; x < clip.x; ++x) row[x] = 0;
            for (x = clip.x + clip.w; x < dst_w; ++x) row[x] = 0;
        }
    }
    if (clip.w <= 0 || clip.h <= 0 || vp.w <= 0 || vp.h <= 0) return;
    xmap = (int *)br_alloc((size_t)clip.w * sizeof(int));
    native = (uint32_t *)br_alloc((size_t)fb->width * sizeof(uint32_t));
    if (!xmap || !native) { br_free(xmap); br_free(native); return; }
    for (x = 0; x < clip.w; ++x) {
        long sx = ((long)(clip.x + x - vp.x) * fb->width) / vp.w;
        xmap[x] = (int)(sx < 0 ? 0 : (sx >= fb->width ? fb->width - 1 : sx));
    }
    for (y = clip.y; y < clip.y + clip.h; ++y) {
        long sy = ((long)(y - vp.y) * fb->height) / vp.h;
        uint32_t *out = dst + (size_t)y * stride + clip.x;
        if (sy < 0) sy = 0;
        if (sy >= fb->height) sy = fb->height - 1;
        if ((int)sy == prev_sy) {           /* repeated source row: copy */
            memcpy(out, out - stride, (size_t)clip.w * sizeof(uint32_t));
            continue;
        }
        {
            const br_color *src = fb->pixels + (size_t)sy * (size_t)fb->width;
            for (x = 0; x < fb->width; ++x) {
                uint32_t p = ((uint32_t)src[x].r << rs) | ((uint32_t)src[x].g << gs) | ((uint32_t)src[x].b << bs);
                if (as >= 0) p |= (uint32_t)src[x].a << as;
                native[x] = p;
            }
            for (x = 0; x < clip.w; ++x) out[x] = native[xmap[x]];
        }
        prev_sy = (int)sy;
    }
    br_free(xmap);
    br_free(native);
}

/* ---- engine-level API ------------------------------------------------------ */

void br_window_set_title(br_engine *e, const char *title) {
    if (e->platform && e->platform->set_title && title) e->platform->set_title(title);
}

void br_window_set_size(br_engine *e, int w, int h) {
    if (w < 1 || h < 1) return;
    if (e->platform && e->platform->set_window_size) e->platform->set_window_size(w, h);
}

void br_window_get_size(const br_engine *e, int *w, int *h) {
    if (w) *w = e->window.width;
    if (h) *h = e->window.height;
}

void br_window_set_fullscreen(br_engine *e, bool fullscreen) {
    if (e->window.fullscreen == fullscreen) return;
    if (e->platform && e->platform->set_fullscreen) e->platform->set_fullscreen(fullscreen);
}

void br_window_toggle_fullscreen(br_engine *e) { br_window_set_fullscreen(e, !e->window.fullscreen); }
bool br_window_is_fullscreen(const br_engine *e) { return e->window.fullscreen; }

void br_window_set_mouse_locked(br_engine *e, bool locked) {
    if (e->window.mouse_locked == locked) return;
    e->window.mouse_locked = locked;
    if (e->platform && e->platform->set_mouse_locked) e->platform->set_mouse_locked(locked);
}

void br_window_set_cursor_visible(br_engine *e, bool visible) {
    e->window.cursor_visible = visible;
    if (e->platform && e->platform->set_cursor_visible) e->platform->set_cursor_visible(visible);
}

void br_window_set_scale_mode(br_engine *e, br_scale_mode mode) {
    e->window.scale_mode = mode;
    br_engine_sync_window(e);
}

float br_window_scale(const br_engine *e) { return e->window.scale; }
bool br_window_is_focused(const br_engine *e) { return e->window.focused; }
bool br_window_is_minimized(const br_engine *e) { return e->window.minimized; }
const br_window_state *br_window_state_get(const br_engine *e) { return &e->window; }

void br_window_set_icon(br_engine *e, const br_image *icon) {
    if (e->platform && e->platform->set_icon) e->platform->set_icon(icon && icon->pixels ? icon : NULL);
}

int br_pick_folder(br_engine *e, const char *title, const char *start, char *out, size_t size) {
    if (!out || size == 0) return -1;
    out[0] = 0;
    if (!e->platform || !e->platform->pick_folder) return -1;
    return e->platform->pick_folder(title ? title : "Choose a folder", start, out, size);
}

bool br_open_path(br_engine *e, const char *path) {
    return path && e->platform && e->platform->open_path && e->platform->open_path(path);
}

bool br_launch(br_engine *e, const char *command_line) {
    return command_line && command_line[0] && e->platform && e->platform->launch && e->platform->launch(command_line);
}

static char *wn_strdup(const char *s) {
    size_t n = strlen(s);
    char *c = (char *)br_alloc(n + 1);
    if (c) memcpy(c, s, n + 1);
    return c;
}

bool br_clipboard_set(br_engine *e, const char *text) {
    if (!text) text = "";
    if (!e) return false;
    if (e->platform && e->platform->clipboard_set) return e->platform->clipboard_set(text);
    br_free(e->clipboard);                        /* no system clipboard: a private one */
    e->clipboard = wn_strdup(text);
    return e->clipboard != NULL;
}

char *br_clipboard_get(br_engine *e) {
    char *text, *r, *w;
    if (!e) return NULL;
    if (e->platform && e->platform->clipboard_get) text = e->platform->clipboard_get();
    else text = e->clipboard ? wn_strdup(e->clipboard) : NULL;
    if (!text) return NULL;
    for (r = w = text; *r; ++r) {                 /* \r\n and a lone \r become \n */
        if (*r == '\r') { *w++ = '\n'; if (r[1] == '\n') ++r; }
        else *w++ = *r;
    }
    *w = 0;
    return text;
}
