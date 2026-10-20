/*
 * POP DEFENSE - the side panel: money, lives, shop, upgrades, round button.
 *
 * layout() lists every widget and its rectangle for the current state.
 * Drawing and click testing both use it, so they can never disagree.
 */
#include "view.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define PX ((float)PANEL_X)
#define MAX_WIDGETS 16

typedef struct widget { ui_widget id; br_rect rect; } widget;

static const br_color k_panel = { 34, 40, 52, 255 };
static const br_color k_button = { 52, 62, 80, 255 };
static const br_color k_text_dim = { 150, 160, 178, 255 };

static int layout(const ui_state *u, widget out[MAX_WIDGETS]) {
    int n = 0, i;
    const td_game *g = u->game;
    if (u->selected >= 0 && u->selected < g->tower_count) {
        out[n].id = UI_DESELECT;     out[n++].rect = br_rect_make(PX + 140, 60, 16, 14);
        out[n].id = UI_TARGET;       out[n++].rect = br_rect_make(PX + 4, 88, 152, 18);
        out[n].id = UI_UPGRADE;      out[n++].rect = br_rect_make(PX + 4, 110, 152, 54);
        out[n].id = UI_UPGRADE + 1;  out[n++].rect = br_rect_make(PX + 4, 168, 152, 54);
        out[n].id = UI_SELL;         out[n++].rect = br_rect_make(PX + 4, 226, 152, 20);
    } else {
        for (i = 0; i < TD_TOWER_TYPE_COUNT; ++i) {
            out[n].id = (ui_widget)(UI_SHOP + i);
            out[n++].rect = br_rect_make(PX + 4 + (float)(i % 2) * 78, 62 + (float)(i / 2) * 56, 74, 52);
        }
    }
    out[n].id = UI_GO;    out[n++].rect = br_rect_make(PX + 4, 308, 116, 46);
    out[n].id = UI_AUTO;  out[n++].rect = br_rect_make(PX + 124, 308, 32, 46);
    out[n].id = UI_PANEL; out[n++].rect = br_rect_make(PX, 0, PANEL_W, FB_H);
    return n;
}

ui_widget view_ui_hit(const ui_state *u, br_vec2 p) {
    widget w[MAX_WIDGETS];
    int n = layout(u, w), i;
    for (i = 0; i < n; ++i)
        if (br_rect_contains(w[i].rect, p)) return w[i].id;
    return UI_NONE;
}

static br_rect find(const widget *w, int n, ui_widget id) {
    int i;
    for (i = 0; i < n; ++i) if (w[i].id == id) return w[i].rect;
    return br_rect_make(0, 0, 0, 0);
}

/* ---- drawing helpers -------------------------------------------------------- */

void view_draw_text_centered(br_renderer *r, const char *text, float cx, float y, float scale, br_color c) {
    br_vec2 size = br_measure_text(text, scale);
    br_draw_text(r, text, cx - size.x / 2 + scale, y + scale, scale, br_rgba(0, 0, 0, 150));
    br_draw_text(r, text, cx - size.x / 2, y, scale, c);
}

/* Greedy word wrap into lines of at most `width` characters. */
static void wrap(const char *text, int width, char *out, size_t size) {
    size_t o = 0;
    int col = 0;
    while (*text && o + 2 < size) {
        const char *word = text;
        int len = 0;
        while (word[len] && word[len] != ' ' && word[len] != '\n') ++len;
        if (col > 0 && col + 1 + len > width) { out[o++] = '\n'; col = 0; }
        else if (col > 0) { out[o++] = ' '; ++col; }
        while (len-- > 0 && o + 1 < size) { out[o++] = *text++; ++col; }
        while (*text == ' ' || *text == '\n') ++text;
    }
    out[o] = 0;
}

static void button(br_renderer *r, br_rect rect, bool hover, bool enabled, br_color accent) {
    br_color bg = enabled ? (hover ? br_color_lerp(k_button, accent, 0.25f) : k_button) : br_rgb(40, 46, 58);
    br_fill_rect(r, rect, bg);
    br_fill_rect(r, br_rect_make(rect.x, rect.y, rect.w, 1), br_color_lerp(bg, BR_WHITE, 0.15f));
    br_draw_rect(r, rect, enabled && hover ? accent : br_rgb(24, 28, 36));
}

static void draw_coin(br_renderer *r, float x, float y) {
    br_fill_circle(r, br_v2(x, y), 6, br_rgb(200, 150, 30));
    br_fill_circle(r, br_v2(x - 0.5f, y - 0.5f), 4.5f, br_rgb(250, 210, 70));
}

static void draw_heart(br_renderer *r, float x, float y) {
    br_color c = br_rgb(235, 60, 80);
    br_fill_circle(r, br_v2(x - 2.6f, y - 1.5f), 3.4f, c);
    br_fill_circle(r, br_v2(x + 2.6f, y - 1.5f), 3.4f, c);
    br_fill_triangle(r, br_v2(x - 6, y - 0.5f), br_v2(x + 6, y - 0.5f), br_v2(x, y + 6), c);
}

static void draw_pips(br_renderer *r, float x, float y, int tier, br_color c) {
    int i;
    for (i = 0; i < TD_TIERS; ++i)
        br_fill_rect(r, br_rect_make(x + (float)i * 9, y, 7, 4), i < tier ? c : br_rgb(70, 78, 94));
}

/* ---- the panel ----------------------------------------------------------------- */

static void draw_header(br_renderer *r, const td_game *g) {
    char buf[32];
    snprintf(buf, sizeof(buf), "ROUND %d/%d", g->round < 1 ? 1 : g->round, td_round_count());
    br_draw_text(r, buf, PX + 8, 6, 1, k_text_dim);
    draw_coin(r, PX + 14, 25);
    snprintf(buf, sizeof(buf), "$%d", g->cash);
    br_draw_text(r, buf, PX + 26, 18, 2, br_rgb(255, 225, 100));
    draw_heart(r, PX + 14, 45);
    snprintf(buf, sizeof(buf), "%d", g->lives);
    br_draw_text(r, buf, PX + 26, 38, 2, g->lives > 30 ? BR_WHITE : br_rgb(255, 110, 110));
}

static void draw_shop(br_renderer *r, const ui_state *u, const widget *w, int n, ui_widget hover) {
    int i;
    for (i = 0; i < TD_TOWER_TYPE_COUNT; ++i) {
        const td_tower_def *def = td_tower_info((td_tower_type)i);
        br_rect rc = find(w, n, (ui_widget)(UI_SHOP + i));
        bool afford = u->game->cash >= def->cost, active = u->placing == i;
        char buf[16];
        button(r, rc, (int)hover == UI_SHOP + i || active, true, br_rgb(120, 200, 255));
        if (active) br_draw_rect(r, br_rect_make(rc.x + 1, rc.y + 1, rc.w - 2, rc.h - 2), br_rgb(120, 200, 255));
        view_draw_tower(r, (td_tower_type)i, br_v2(rc.x + rc.w / 2, rc.y + 21), br_v2(0.6f, -0.8f), NULL, 1.15f);
        snprintf(buf, sizeof(buf), "$%d", def->cost);
        view_draw_text_centered(r, buf, rc.x + rc.w / 2, rc.y + rc.h - 11, 1, afford ? br_rgb(255, 225, 100) : br_rgb(200, 90, 90));
        snprintf(buf, sizeof(buf), "%d", i + 1);
        br_draw_text(r, buf, rc.x + 3, rc.y + 3, 1, k_text_dim);
    }
}

static void draw_upgrade_button(br_renderer *r, const ui_state *u, br_rect rc, int path, bool hover) {
    const td_tower *t = &u->game->towers[u->selected];
    const td_tower_def *def = td_tower_info(t->type);
    td_result can = td_can_upgrade(u->game, u->selected, path);
    br_color accent = path == 0 ? br_rgb(255, 170, 40) : br_rgb(80, 190, 255);
    char buf[24];
    button(r, rc, hover, can == TD_OK || can == TD_ERR_CASH, accent);
    draw_pips(r, rc.x + 6, rc.y + 6, t->tier[path], accent);
    if (can == TD_ERR_MAXED || can == TD_ERR_PATH_LOCKED) {
        br_draw_text(r, def->paths[path][t->tier[path] - (can == TD_ERR_MAXED)].name, rc.x + 6, rc.y + 18, 1,
                     can == TD_ERR_MAXED ? accent : k_text_dim);
        br_draw_text(r, can == TD_ERR_MAXED ? "MAXED" : "LOCKED", rc.x + 6, rc.y + 36, 1, k_text_dim);
        return;
    }
    br_draw_text(r, def->paths[path][t->tier[path]].name, rc.x + 6, rc.y + 18, 1, BR_WHITE);
    snprintf(buf, sizeof(buf), "$%d", def->paths[path][t->tier[path]].cost);
    br_draw_text(r, buf, rc.x + 6, rc.y + 36, 1, can == TD_OK ? br_rgb(255, 225, 100) : br_rgb(200, 90, 90));
    if (can == TD_OK) br_draw_text(r, path == 0 ? "[,]" : "[.]", rc.x + rc.w - 28, rc.y + 36, 1, k_text_dim);
}

static void draw_tower_panel(br_renderer *r, const ui_state *u, const widget *w, int n, ui_widget hover) {
    const td_tower *t = &u->game->towers[u->selected];
    const td_tower_def *def = td_tower_info(t->type);
    br_rect rc;
    char buf[48];
    bool aims = t->stats.attack == TD_ATK_AIMED || t->stats.attack == TD_ATK_HITSCAN;

    br_draw_text(r, def->name, PX + 6, 62, 1, BR_WHITE);
    rc = find(w, n, UI_DESELECT);
    br_draw_text(r, "x", rc.x + 4, rc.y + 3, 1, hover == UI_DESELECT ? BR_WHITE : k_text_dim);
    snprintf(buf, sizeof(buf), "Pops %d", t->pops);
    br_draw_text(r, buf, PX + 6, 75, 1, k_text_dim);

    rc = find(w, n, UI_TARGET);
    button(r, rc, hover == UI_TARGET, aims, BR_WHITE);
    snprintf(buf, sizeof(buf), aims ? "< %s >" : "All in range", td_targeting_name(t->targeting));
    view_draw_text_centered(r, buf, rc.x + rc.w / 2, rc.y + 5, 1, aims ? BR_WHITE : k_text_dim);

    draw_upgrade_button(r, u, find(w, n, UI_UPGRADE), 0, hover == UI_UPGRADE);
    draw_upgrade_button(r, u, find(w, n, UI_UPGRADE + 1), 1, hover == UI_UPGRADE + 1);

    rc = find(w, n, UI_SELL);
    button(r, rc, hover == UI_SELL, true, br_rgb(255, 120, 100));
    snprintf(buf, sizeof(buf), "SELL $%d", td_sell_value(t));
    view_draw_text_centered(r, buf, rc.x + rc.w / 2, rc.y + 6, 1, br_rgb(255, 190, 170));
}

/* The info box under the shop / upgrades: what the cursor is over. */
static void draw_info(br_renderer *r, const ui_state *u, ui_widget hover) {
    char text[160], wrapped[200];
    const char *title = NULL;
    float y = 252;
    text[0] = 0;
    if (u->selected < 0 && u->placing < 0) y = 234;
    if (u->message_time > 0 && u->message) {
        wrap(u->message, 19, wrapped, sizeof(wrapped));
        br_draw_text(r, wrapped, PX + 6, y, 1, br_rgb(255, 120, 110));
        return;
    }
    if (hover >= UI_SHOP && hover < UI_SHOP + TD_TOWER_TYPE_COUNT) {
        const td_tower_def *def = td_tower_info((td_tower_type)(hover - UI_SHOP));
        title = def->name;
        snprintf(text, sizeof(text), "%s", def->desc);
    } else if (u->placing >= 0) {
        title = td_tower_info((td_tower_type)u->placing)->name;
        snprintf(text, sizeof(text), "Click the grass to build. Right click cancels.");
    } else if (u->selected >= 0 && (hover == UI_UPGRADE || hover == UI_UPGRADE + 1)) {
        const td_tower *t = &u->game->towers[u->selected];
        int path = hover - UI_UPGRADE, tier = t->tier[path];
        if (tier < TD_TIERS) snprintf(text, sizeof(text), "%s", td_tower_info(t->type)->paths[path][tier].desc);
    } else if (u->selected >= 0) {
        const td_stats *s = &u->game->towers[u->selected].stats;
        if (s->attack == TD_ATK_HITSCAN) snprintf(text, sizeof(text), "Damage %d. Range: everywhere.", s->damage);
        else if (s->attack == TD_ATK_PULSE && s->freeze > 0) snprintf(text, sizeof(text), "Range %d. Freezes %.1fs.", (int)s->range, (double)s->freeze);
        else snprintf(text, sizeof(text), "Range %d. Pops %d per shot.", (int)s->range, s->pierce);
    } else {
        snprintf(text, sizeof(text), "Pick a tower to build, then click the grass.");
    }
    if (title) { br_draw_text(r, title, PX + 6, y, 1, br_rgb(120, 200, 255)); y += 12; }
    wrap(text, 19, wrapped, sizeof(wrapped));
    br_draw_text(r, wrapped, PX + 6, y, 1, br_rgb(205, 212, 225));
}

static void draw_round_buttons(br_renderer *r, const ui_state *u, const widget *w, int n, ui_widget hover) {
    const td_game *g = u->game;
    br_rect go = find(w, n, UI_GO), au = find(w, n, UI_AUTO);
    char buf[24];
    bool over = td_game_over(g);
    br_color green = br_rgb(90, 200, 90);
    button(r, go, hover == UI_GO, !over, green);
    if (g->phase == TD_PHASE_BUILD) {
        float pulse = 0.5f + 0.5f * sinf(u->time * 5.0f);
        br_fill_rect(r, br_rect_make(go.x + 1, go.y + 1, go.w - 2, go.h - 2), br_rgba(90, 200, 90, (uint8_t)(40 + 50 * pulse)));
        view_draw_text_centered(r, "PLAY", go.x + go.w / 2, go.y + 9, 2, BR_WHITE);
        snprintf(buf, sizeof(buf), "round %d", g->round + 1);
        view_draw_text_centered(r, buf, go.x + go.w / 2, go.y + 31, 1, br_rgb(200, 240, 200));
    } else if (!over) {
        static const char *arrows[] = { "", ">", ">>", ">>>" };
        snprintf(buf, sizeof(buf), "%s x%d", arrows[u->speed], u->speed);
        view_draw_text_centered(r, buf, go.x + go.w / 2, go.y + 9, 2, u->speed > 1 ? br_rgb(255, 225, 100) : BR_WHITE);
        view_draw_text_centered(r, "speed", go.x + go.w / 2, go.y + 31, 1, k_text_dim);
    }
    button(r, au, hover == UI_AUTO, !over, green);
    br_fill_circle(r, br_v2(au.x + au.w / 2, au.y + 14), 4, u->auto_start ? br_rgb(110, 240, 110) : br_rgb(60, 70, 60));
    view_draw_text_centered(r, "AUTO", au.x + au.w / 2, au.y + 28, 1, u->auto_start ? BR_WHITE : k_text_dim);
}

void view_draw_ui(br_renderer *r, const ui_state *u) {
    widget w[MAX_WIDGETS];
    int n = layout(u, w);
    ui_widget hover = view_ui_hit(u, u->cursor);

    br_fill_rect(r, br_rect_make(PX, 0, PANEL_W, FB_H), k_panel);
    br_fill_rect(r, br_rect_make(PX, 0, 2, FB_H), br_rgb(20, 24, 32));
    draw_header(r, u->game);
    br_fill_rect(r, br_rect_make(PX + 6, 56, PANEL_W - 12, 1), br_rgb(60, 70, 88));
    if (u->selected >= 0 && u->selected < u->game->tower_count) draw_tower_panel(r, u, w, n, hover);
    else draw_shop(r, u, w, n, hover);
    draw_info(r, u, hover);
    draw_round_buttons(r, u, w, n, hover);
}

/* ---- title screen: what each balloon does ------------------------------------------ */

void view_draw_balloon_legend(br_renderer *r) {
    static const char *notes[TD_BALLOON_TYPE_COUNT] = {
        "", "", "", "fast", "faster", "bomb-proof", "ice-proof", "sharp-proof",
        "no bombs/ice", "47 layers", "10 hits", "200 hits",
    };
    int i;
    float y = 30;
    br_fill_rect(r, br_rect_make(PX, 0, PANEL_W, FB_H), k_panel);
    br_fill_rect(r, br_rect_make(PX, 0, 2, FB_H), br_rgb(20, 24, 32));
    view_draw_text_centered(r, "BALLOONS", PX + PANEL_W / 2.0f, 10, 1, br_rgb(120, 200, 255));
    for (i = 0; i < TD_BALLOON_TYPE_COUNT; ++i, y += 24) {
        br_vec2 at = br_v2(PX + 16, y + 9);
        if (i == TD_BLIMP) {
            br_fill_circle(r, br_v2(at.x - 5, at.y), 6, br_rgb(94, 134, 184));
            br_fill_circle(r, br_v2(at.x + 5, at.y), 6, br_rgb(94, 134, 184));
            br_fill_rect(r, br_rect_make(at.x - 5, at.y - 6, 10, 12), br_rgb(94, 134, 184));
            br_fill_rect(r, br_rect_make(at.x - 4, at.y - 3, 8, 2), br_rgb(150, 185, 225));
        } else {
            view_draw_balloon(r, (td_balloon_type)i, false, at, 1.0f);
        }
        br_draw_text(r, td_balloon_info((td_balloon_type)i)->name, PX + 32, y + 2, 1, BR_WHITE);
        br_draw_text(r, notes[i], PX + 32, y + 12, 1, k_text_dim);
    }
    view_draw_balloon(r, TD_GREEN, true, br_v2(PX + 16, y + 9), 1.0f);
    br_draw_text(r, "Camo", PX + 32, y + 2, 1, BR_WHITE);
    br_draw_text(r, "needs detection", PX + 32, y + 12, 1, k_text_dim);
}
