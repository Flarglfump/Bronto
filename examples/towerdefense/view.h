/*
 * POP DEFENSE - presentation layer (everything the player sees and hears).
 *
 *   view_world.c   the map, towers, balloons and projectiles
 *   view_ui.c      the side panel: shop, upgrades, round button
 *   view_fx.c      pops, explosions and floating text (ECS entities)
 *   sfx.c          synthesised sound effects and music
 *   main.c         engine setup, scenes, input -> game commands
 *
 * None of these change the rules' state except main.c, and only through
 * the commands in td.h.
 */
#ifndef TD_VIEW_H
#define TD_VIEW_H

#include "bronto/bronto.h"
#include "td.h"

#define FB_W 640
#define FB_H 360
#define PANEL_X TD_MAP_W              /* the side panel fills the right of the screen */
#define PANEL_W (FB_W - TD_MAP_W)

/* ---- view_world.c --------------------------------------------------------- */
void view_world_init(const td_map *map);      /* paints the static background once */
void view_world_shutdown(void);
void view_draw_world(br_renderer *r, const td_game *g, float time, int selected_tower);
void view_draw_range(br_renderer *r, br_vec2 pos, float range, br_color color);
void view_draw_tower(br_renderer *r, td_tower_type type, br_vec2 pos, br_vec2 aim,
                     const int tiers[TD_PATHS], float scale);
void view_draw_balloon(br_renderer *r, td_balloon_type type, bool camo, br_vec2 pos, float scale);

/* ---- view_ui.c -------------------------------------------------------------- */
typedef enum ui_widget {
    UI_NONE = 0,
    UI_SHOP,                /* + tower type */
    UI_SHOP_END = UI_SHOP + TD_TOWER_TYPE_COUNT,   /* one past the last shop button */
    UI_TARGET = UI_SHOP_END,
    UI_UPGRADE,             /* + path */
    UI_SELL = UI_UPGRADE + TD_PATHS,
    UI_DESELECT,
    UI_GO,
    UI_AUTO,
    UI_PANEL                /* the panel background: swallows clicks */
} ui_widget;

/* What the panel shows; filled in by the play scene every frame. */
typedef struct ui_state {
    const td_game *game;
    int selected;           /* tower index or -1 */
    int placing;            /* tower type being placed or -1 */
    int speed;              /* 1..3 */
    bool auto_start;
    const char *message;    /* short-lived notice ("Not enough cash") */
    float message_time;
    br_vec2 cursor;
    float time;
} ui_state;

ui_widget view_ui_hit(const ui_state *u, br_vec2 p);
void view_draw_ui(br_renderer *r, const ui_state *u);
void view_draw_text_centered(br_renderer *r, const char *text, float cx, float y, float scale, br_color c);
void view_draw_balloon_legend(br_renderer *r);   /* the panel on the title screen */

/* ---- view_fx.c ---------------------------------------------------------------- */
typedef struct view_fx {
    br_world *world;
    br_component particle, ring, text, tracer;
    br_rng rng;
} view_fx;

void view_fx_init(view_fx *fx, br_world *world);   /* registers components + systems */
void view_fx_event(view_fx *fx, const td_event *e);
void view_fx_text(view_fx *fx, br_vec2 pos, const char *text, br_color color, float scale, float life);
void view_fx_clear(view_fx *fx);

/* ---- sfx.c ---------------------------------------------------------------------- */
typedef enum sfx_ui_sound { SFX_CLICK = 0, SFX_ERROR } sfx_ui_sound;
void sfx_init(br_engine *e, uint32_t event_type);     /* plays sounds for rule events */
void sfx_shutdown(br_engine *e);
void sfx_ui(br_engine *e, sfx_ui_sound s);
void sfx_toggle_music(br_engine *e);

#endif /* TD_VIEW_H */
