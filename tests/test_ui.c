/* Tests: the immediate-mode GUI (ui.h), driven by a scripted br_input. */
#include "test.h"
#include <string.h>

static br_input IN;
static br_ui UI;
static br_image IMG;
static br_renderer R;
static double T;

static void ui_setup(void) {
    br_input_init(&IN);
    br_ui_init(&UI);
    br_image_init(&IMG, 320, 240);
    br_renderer_init(&R, &IMG);
    T = 0;
}

static void ui_teardown(void) { br_image_destroy(&IMG); }

static void begin(void) {
    br_ui_begin(&UI, &R, &IN, T);
    br_ui_panel_begin(&UI, "panel", br_rect_make(0, 0, 200, 200));
}

static void end(void) {
    br_ui_panel_end(&UI);
    br_ui_end(&UI);
    br_input_begin_frame(&IN);          /* clear this frame's edges */
    T += 1.0 / 60.0;
}

static void move(float x, float y) { IN.mouse_x = x; IN.mouse_y = y; }
static void press(void) { IN.mouse[BR_MOUSE_LEFT] = true; IN.mouse_pressed[BR_MOUSE_LEFT] = true; }
static void release(void) { IN.mouse[BR_MOUSE_LEFT] = false; IN.mouse_released[BR_MOUSE_LEFT] = true; }
static void click(void) { press(); release(); }      /* down and up within one frame */
static void type(const char *s) { while (*s) IN.text[IN.text_count++] = (uint32_t)*s++; }
static void key(br_key k) { IN.keys_pressed[k] = true; IN.keys_repeated[k] = true; }

/* Panel padding 6, rows 16 high with 3 between: row i spans y = 6 + 19 i .. +16. */
#define ROW_Y(i) (6.0f + 19.0f * (float)(i) + 8.0f)

static void test_ui_buttons(void) {
    bool a, b;
    ui_setup();
    move(50, ROW_Y(0));
    click();
    begin(); a = br_ui_button(&UI, "OK##first"); b = br_ui_button(&UI, "OK##second"); end();
    CHECK(a && !b);                                       /* same text, different ids */
    move(50, ROW_Y(1));
    click();
    begin(); a = br_ui_button(&UI, "OK##first"); b = br_ui_button(&UI, "OK##second"); end();
    CHECK(!a && b);
    /* press inside, release outside: not a click */
    move(50, ROW_Y(0)); press();
    begin(); a = br_ui_button(&UI, "Go"); end();
    CHECK(!a && UI.active != 0);
    move(300, 230); release();
    begin(); a = br_ui_button(&UI, "Go"); end();
    CHECK(!a && UI.active == 0);
    /* disabled: drawn, not clickable */
    br_ui_set_disabled(&UI, true);
    move(50, ROW_Y(0)); click();
    begin(); a = br_ui_button(&UI, "Go"); end();
    CHECK(!a);
    br_ui_set_disabled(&UI, false);
    ui_teardown();
}

static void test_ui_checkbox_selectable(void) {
    bool v = false, clicked;
    ui_setup();
    move(120, ROW_Y(0)); click();                         /* the box sits right of the label */
    begin(); clicked = br_ui_checkbox(&UI, "Grid", &v); end();
    CHECK(clicked && v);
    move(120, ROW_Y(0)); click();
    begin(); br_ui_checkbox(&UI, "Grid", &v); end();
    CHECK(!v);
    move(40, ROW_Y(1)); click();
    begin(); br_ui_selectable(&UI, "first", true); clicked = br_ui_selectable(&UI, "second", false); end();
    CHECK(clicked);
    ui_teardown();
}

static void test_ui_drag_and_type_numbers(void) {
    float f = 1.0f;
    int i = 5;
    bool changed;
    ui_setup();
    move(120, ROW_Y(0)); press();
    begin(); br_ui_drag_float(&UI, "Speed", &f, 0.1f, 0, 0); end();
    move(170, ROW_Y(0));                                  /* drag 50 px right */
    begin(); changed = br_ui_drag_float(&UI, "Speed", &f, 0.1f, 0, 0); end();
    CHECK(changed);
    CHECK_NEAR(f, 6.0, 1e-4);
    move(400, ROW_Y(0));                                  /* clamped to the range */
    begin(); br_ui_drag_int(&UI, "Lives", &i, 0.5f, 0, 9); br_ui_drag_float(&UI, "Speed", &f, 0.1f, 0, 10); end();
    CHECK_NEAR(f, 10.0, 1e-4);
    release();
    begin(); br_ui_drag_float(&UI, "Speed", &f, 0.1f, 0, 10); end();
    CHECK(!br_ui_wants_keyboard(&UI));                    /* a drag doesn't start typing */
    /* click (no drag) to type a value */
    move(120, ROW_Y(0)); click();
    begin(); br_ui_drag_float(&UI, "Speed", &f, 0.1f, 0, 10); end();
    CHECK(br_ui_wants_keyboard(&UI));
    type("2.5");                                          /* replaces the selected text */
    begin(); br_ui_drag_float(&UI, "Speed", &f, 0.1f, 0, 10); end();
    key(BR_KEY_ENTER);
    begin(); changed = br_ui_drag_float(&UI, "Speed", &f, 0.1f, 0, 10); end();
    CHECK(changed);
    CHECK_NEAR(f, 2.5, 1e-6);
    CHECK(!br_ui_wants_keyboard(&UI) && UI.edits_committed == 1);
    /* typed values are clamped too */
    move(120, ROW_Y(0)); click();
    begin(); br_ui_drag_float(&UI, "Speed", &f, 0.1f, 0, 10); end();
    type("99");
    key(BR_KEY_ENTER);
    begin(); br_ui_drag_float(&UI, "Speed", &f, 0.1f, 0, 10); end();
    CHECK_NEAR(f, 10.0, 1e-6);
    ui_teardown();
}

static void test_ui_text_field(void) {
    char name[8] = "abc";
    bool committed;
    ui_setup();
    move(120, ROW_Y(0)); click();
    begin(); br_ui_text_field(&UI, "Name", name, sizeof(name)); end();
    CHECK(br_ui_wants_keyboard(&UI));
    key(BR_KEY_END);                                      /* keep the text, append */
    begin(); br_ui_text_field(&UI, "Name", name, sizeof(name)); end();
    type("defghij");                                      /* only 7 characters fit */
    begin(); br_ui_text_field(&UI, "Name", name, sizeof(name)); end();
    key(BR_KEY_BACKSPACE);
    begin(); br_ui_text_field(&UI, "Name", name, sizeof(name)); end();
    CHECK(strcmp(name, "abc") == 0);                      /* nothing written until commit */
    key(BR_KEY_ENTER);
    begin(); committed = br_ui_text_field(&UI, "Name", name, sizeof(name)); end();
    CHECK(committed && strcmp(name, "abcdef") == 0);
    /* Escape cancels */
    move(120, ROW_Y(0)); click();
    begin(); br_ui_text_field(&UI, "Name", name, sizeof(name)); end();
    type("zz");
    begin(); br_ui_text_field(&UI, "Name", name, sizeof(name)); end();
    key(BR_KEY_ESCAPE);
    begin(); br_ui_text_field(&UI, "Name", name, sizeof(name)); end();
    CHECK(strcmp(name, "abcdef") == 0 && !br_ui_wants_keyboard(&UI));
    /* clicking somewhere else commits */
    move(120, ROW_Y(0)); click();
    begin(); br_ui_text_field(&UI, "Name", name, sizeof(name)); end();
    type("new");
    begin(); br_ui_text_field(&UI, "Name", name, sizeof(name)); end();
    move(300, 230); click();
    begin(); br_ui_text_field(&UI, "Name", name, sizeof(name)); end();
    CHECK(strcmp(name, "new") == 0 && !br_ui_wants_keyboard(&UI));
    ui_teardown();
}

static void test_ui_combo(void) {
    static const char *const items[] = { "rect", "ellipse", "triangle" };
    int kind = 0;
    bool changed;
    ui_setup();
    move(120, ROW_Y(0)); click();
    begin(); br_ui_combo(&UI, "Kind", &kind, items, 3); end();
    CHECK(UI.popup_id != 0);
    CHECK(br_ui_wants_mouse(&UI, br_v2(120, ROW_Y(0) + 16 + 8)));
    /* the list opens below: item i at y = row bottom + 1 + 16 i */
    move(120, ROW_Y(0) + 8 + 1 + 16 * 2 + 4); click();
    begin(); changed = br_ui_combo(&UI, "Kind", &kind, items, 3); br_ui_button(&UI, "under the list"); end();
    CHECK(!changed && UI.popup_id == 0);                  /* reported on the next frame */
    begin(); changed = br_ui_combo(&UI, "Kind", &kind, items, 3); end();
    CHECK(changed && kind == 2);
    ui_teardown();
}

static void test_ui_layout_and_scroll(void) {
    br_rect a, b, c, d;
    int i, row;
    ui_setup();
    begin();
    br_ui_columns(&UI, 2);
    a = br_ui_row(&UI, 0);
    b = br_ui_row(&UI, 0);
    c = br_ui_row(&UI, 0);
    end();
    CHECK(a.y == b.y && b.x > a.x + a.w && c.y > a.y);   /* side by side, then the next row */
    CHECK_NEAR(a.x, 6, 1e-4);
    CHECK(br_ui_wants_mouse(&UI, br_v2(100, 100)));      /* over the panel */
    CHECK(!br_ui_wants_mouse(&UI, br_v2(250, 100)));
    /* 30 rows in a 200 px panel: the wheel scrolls, within limits */
    for (i = 0; i < 3; ++i) {
        move(100, 100);
        IN.wheel_y = -1;
        begin();
        for (row = 0; row < 30; ++row) br_ui_label(&UI, "row");
        end();
        IN.wheel_y = 0;
    }
    begin();
    d = br_ui_row(&UI, 0);
    end();
    CHECK_NEAR(d.y, 6 - 3 * 48, 1e-3);                   /* 3 notches of 3 rows */
    for (i = 0; i < 20; ++i) {
        IN.wheel_y = -1;
        begin(); for (row = 0; row < 30; ++row) br_ui_label(&UI, "row"); end();
    }
    IN.wheel_y = 0;
    begin(); d = br_ui_row(&UI, 0); for (row = 0; row < 29; ++row) br_ui_label(&UI, "row"); end();
    CHECK_NEAR(d.y, -(30.0 * 19.0 - 3.0 + 12.0 - 200.0) + 6.0, 1e-3);    /* stops at the bottom */
    ui_teardown();
}

static void test_ui_area_double_click(void) {
    int f;
    ui_setup();
    move(50, ROW_Y(0)); click();
    begin(); f = br_ui_area(&UI, "tile", 0); end();
    CHECK((f & BR_UI_CLICKED) && !(f & BR_UI_DOUBLE_CLICKED));
    click();
    begin(); f = br_ui_area(&UI, "tile", 0); end();
    CHECK((f & BR_UI_CLICKED) && (f & BR_UI_DOUBLE_CLICKED));
    click();                                                  /* a third click starts over */
    begin(); f = br_ui_area(&UI, "tile", 0); end();
    CHECK((f & BR_UI_CLICKED) && !(f & BR_UI_DOUBLE_CLICKED));
    T += 1.0;                                                 /* too slow: two single clicks */
    click();
    begin(); f = br_ui_area(&UI, "tile", 0); end();
    CHECK(!(f & BR_UI_DOUBLE_CLICKED));
    begin();
    br_ui_area(&UI, "a", 30);
    {
        br_rect r = br_ui_last_rect(&UI);
        CHECK(r.y == 6 && r.h == 30);
    }
    end();
    ui_teardown();
}

/* One frame: a draggable row "item" in the panel, and a drop zone outside it. */
static const char *drag_frame(bool *dragging) {
    const char *got;
    begin();
    br_ui_area(&UI, "item", 0);
    *dragging = br_ui_drag_source(&UI, "asset", "art/hero.bmp", "hero");
    br_ui_panel_end(&UI);
    got = br_ui_drop_target(&UI, br_rect_make(220, 0, 100, 100), "asset");
    br_ui_end(&UI);
    br_input_begin_frame(&IN);
    T += 1.0 / 60.0;
    return got;
}

static void test_ui_drag_drop(void) {
    bool dragging;
    const char *got;
    int f;
    ui_setup();
    move(50, ROW_Y(0)); press();
    got = drag_frame(&dragging);
    CHECK(!dragging && !got && br_ui_dragging(&UI, NULL) == NULL);
    move(52, ROW_Y(0));                                      /* under the threshold */
    got = drag_frame(&dragging);
    CHECK(!dragging);
    move(120, ROW_Y(0));
    got = drag_frame(&dragging);
    CHECK(dragging && !got && strcmp(br_ui_dragging(&UI, "asset"), "art/hero.bmp") == 0);
    CHECK(br_ui_dragging(&UI, "entity") == NULL);
    CHECK(br_ui_wants_mouse(&UI, br_v2(300, 230)));          /* the drag owns the mouse */
    move(250, 50);
    got = drag_frame(&dragging);
    CHECK(dragging && !got);                                 /* hovering the target */
    release();
    got = drag_frame(&dragging);
    CHECK(got && strcmp(got, "art/hero.bmp") == 0);          /* dropped */
    CHECK(br_ui_dragging(&UI, NULL) == NULL && UI.active == 0);
    got = drag_frame(&dragging);
    CHECK(!got && !dragging);
    /* the drag never clicks its source, and a drop outside any target is lost */
    move(50, ROW_Y(0)); press();
    drag_frame(&dragging);
    move(50, ROW_Y(0) + 40);
    drag_frame(&dragging);
    CHECK(dragging);
    move(50, ROW_Y(0)); release();
    begin(); f = br_ui_area(&UI, "item", 0); end();
    CHECK(!(f & BR_UI_CLICKED) && br_ui_dragging(&UI, NULL) == NULL);
    /* Escape cancels */
    press();
    drag_frame(&dragging);
    move(250, 50);
    drag_frame(&dragging);
    CHECK(dragging);
    key(BR_KEY_ESCAPE);
    got = drag_frame(&dragging);
    CHECK(!dragging && !got && br_ui_dragging(&UI, NULL) == NULL);
    release();
    got = drag_frame(&dragging);
    CHECK(!got);
    ui_teardown();
}


/* ---- context menus ---- */

static void rpress(void) { IN.mouse[BR_MOUSE_RIGHT] = true; IN.mouse_pressed[BR_MOUSE_RIGHT] = true; }
static void rrelease(void) { IN.mouse[BR_MOUSE_RIGHT] = false; IN.mouse_released[BR_MOUSE_RIGHT] = true; }
static bool g_row_clicked;

/* One frame: a button with a menu, empty space with another. Returns the item chosen. */
static int menu_frame(void) {
    int chosen = -1;
    begin();
    g_row_clicked = br_ui_button(&UI, "Row");
    if (br_ui_last_flags(&UI) & BR_UI_RIGHT_CLICKED) br_ui_menu_open(&UI, "m");
    if (br_ui_context_area(&UI, br_rect_make(0, 0, 200, 200))) br_ui_menu_open(&UI, "blank");
    if (br_ui_menu_begin(&UI, "m")) {
        if (br_ui_menu_item(&UI, "Copy", "Ctrl+C", true)) chosen = 0;
        if (br_ui_menu_item(&UI, "Paste", NULL, false)) chosen = 1;
        br_ui_menu_separator(&UI);
        if (br_ui_menu_sub_begin(&UI, "More", true)) {
            if (br_ui_menu_item(&UI, "Deep", NULL, true)) chosen = 2;
            br_ui_menu_sub_end(&UI);
        }
        if (br_ui_menu_check(&UI, "Tick", true)) chosen = 3;
        br_ui_menu_end(&UI);
    }
    if (br_ui_menu_begin(&UI, "blank")) {
        if (br_ui_menu_item(&UI, "New", NULL, true)) chosen = 10;
        br_ui_menu_end(&UI);
    }
    end();
    return chosen;
}

static br_vec2 item_center(int i) {        /* items are 18 high, from the menu's top + 1 (separator: 7) */
    br_rect m = UI.menu.rect;
    float y = m.y + 1;
    int k;
    for (k = 0; k < i; ++k) y += k == 2 ? 7.0f : 18.0f;
    return br_v2(m.x + 30, y + (i == 2 ? 3.0f : 9.0f));
}

static void test_ui_context_menu(void) {
    br_vec2 p;
    int got;
    ui_setup();
    move(50, ROW_Y(0)); rpress();
    CHECK(menu_frame() == -1 && br_ui_menu_is_open(&UI, "m") && !br_ui_menu_is_open(&UI, "blank"));
    rrelease();
    CHECK(menu_frame() == -1 && UI.menu.rect.w >= 120 && br_ui_wants_mouse(&UI, br_v2(300, 230)));
    p = item_center(1); move(p.x, p.y); click();             /* Paste is disabled */
    CHECK(menu_frame() == -1 && br_ui_menu_active(&UI));
    p = item_center(0); move(p.x, p.y); click();             /* Copy: reported the frame after */
    got = menu_frame();
    CHECK(got == -1);
    CHECK(menu_frame() == 0 && !br_ui_menu_active(&UI));
    CHECK(menu_frame() == -1);

    /* a submenu opens on hover */
    move(50, ROW_Y(0)); rpress(); menu_frame(); rrelease(); menu_frame();
    p = item_center(3); move(p.x, p.y);
    menu_frame();
    CHECK(UI.menu.sub == 3 && UI.menu.sub_rect.w > 0);
    move(UI.menu.sub_rect.x + 20, UI.menu.sub_rect.y + 10); menu_frame();
    click(); menu_frame();
    CHECK(menu_frame() == 2 && !br_ui_menu_active(&UI));

    /* keys: Down picks the first usable item, Enter chooses it; Escape closes */
    move(50, ROW_Y(0)); rpress(); menu_frame(); rrelease(); menu_frame();
    key(BR_KEY_DOWN); menu_frame();
    key(BR_KEY_DOWN); menu_frame();                          /* skips Paste and the separator */
    CHECK(UI.menu.hot == 3);
    key(BR_KEY_RIGHT); menu_frame();                         /* into More */
    CHECK(UI.menu.sub == 3 && UI.menu.hot == 4);
    key(BR_KEY_ESCAPE); menu_frame();                        /* back out */
    CHECK(br_ui_menu_active(&UI) && UI.menu.sub == -1 && UI.menu.hot == 3);
    key(BR_KEY_ESCAPE); menu_frame();
    CHECK(!br_ui_menu_active(&UI));

    /* a click outside closes it and is used up: the button under it isn't clicked */
    move(50, ROW_Y(0)); rpress(); menu_frame(); rrelease(); menu_frame();
    move(20, ROW_Y(0)); click();
    CHECK(menu_frame() == -1 && !br_ui_menu_active(&UI) && !g_row_clicked);

    /* empty space has its own menu; a right-click elsewhere moves the menu there */
    move(50, ROW_Y(6)); rpress();
    menu_frame(); rrelease();
    CHECK(br_ui_menu_is_open(&UI, "blank"));
    move(50, ROW_Y(0)); rpress();
    menu_frame(); rrelease();
    CHECK(br_ui_menu_is_open(&UI, "m"));
    /* a menu nobody lists any more closes */
    begin(); end(); begin(); end();
    CHECK(!br_ui_menu_active(&UI));
    ui_teardown();
}

/* Ctrl+A / Ctrl+C / Ctrl+X / Ctrl+V in a text field, with the GUI's own clipboard and a given one. */
static char *g_clip;
static bool fake_set(void *user, const char *t) {
    size_t n = strlen(t);
    BR_UNUSED(user);
    br_free(g_clip);
    g_clip = (char *)br_alloc(n + 1);
    memcpy(g_clip, t, n + 1);
    return true;
}
static char *fake_get(void *user) {
    char *c;
    BR_UNUSED(user);
    if (!g_clip) return NULL;
    c = (char *)br_alloc(strlen(g_clip) + 1);
    strcpy(c, g_clip);
    return c;
}
static void cmd_key(br_key k) { IN.mods = BR_MOD_CONTROL; key(k); IN.text[IN.text_count++] = 'x'; }   /* (the stray text is ignored) */

static void field_frame(char *buf, size_t size) { begin(); br_ui_text_field(&UI, "Name", buf, size); end(); IN.mods = 0; }

static void test_ui_clipboard(void) {
    char a[32] = "Ball", b[32] = "";
    br_ui_clipboard cb;
    ui_setup();
    move(150, ROW_Y(0)); click();
    field_frame(a, sizeof(a));                               /* editing, all selected */
    cmd_key(BR_KEY_C); field_frame(a, sizeof(a));
    CHECK(strcmp(UI.own_clip, "Ball") == 0);
    key(BR_KEY_END); field_frame(a, sizeof(a));
    cmd_key(BR_KEY_V); field_frame(a, sizeof(a));            /* at the caret */
    key(BR_KEY_ENTER); field_frame(a, sizeof(a));
    CHECK(strcmp(a, "BallBall") == 0);

    cb.set = fake_set; cb.get = fake_get; cb.user = NULL;
    br_ui_set_clipboard(&UI, &cb);
    fake_set(NULL, "Rock\tSolid\nsecond line");
    move(150, ROW_Y(0)); click(); field_frame(b, sizeof(b));
    cmd_key(BR_KEY_V); field_frame(b, sizeof(b));            /* the first line, tabs as spaces */
    key(BR_KEY_ENTER); field_frame(b, sizeof(b));
    CHECK(strcmp(b, "Rock Solid") == 0);
    move(150, ROW_Y(0)); click(); field_frame(b, sizeof(b));
    cmd_key(BR_KEY_X); field_frame(b, sizeof(b));            /* cut: copied, and the field empties */
    key(BR_KEY_ENTER); field_frame(b, sizeof(b));
    CHECK(strcmp(b, "") == 0 && g_clip && strcmp(g_clip, "Rock Solid") == 0);
    move(150, ROW_Y(0)); click(); field_frame(a, sizeof(a));
    key(BR_KEY_END); field_frame(a, sizeof(a));
    type("!"); field_frame(a, sizeof(a));
    cmd_key(BR_KEY_A); field_frame(a, sizeof(a));            /* select all, then paste over it */
    cmd_key(BR_KEY_V); field_frame(a, sizeof(a));
    key(BR_KEY_ENTER); field_frame(a, sizeof(a));
    CHECK(strcmp(a, "Rock Solid") == 0);
    br_ui_set_clipboard(&UI, NULL);
    br_free(g_clip);
    g_clip = NULL;
    ui_teardown();
}

static void test_ui_focus_and_stick(void) {
    char name[32] = "Ball";
    int i, slot;
    ui_setup();
    br_ui_focus_next(&UI);
    begin(); br_ui_text_field(&UI, "Name", name, sizeof(name)); end();
    CHECK(br_ui_wants_keyboard(&UI));
    type("Rock"); key(BR_KEY_ENTER);
    begin(); CHECK(br_ui_text_field(&UI, "Name", name, sizeof(name))); end();
    CHECK(strcmp(name, "Rock") == 0);

    for (i = 0; i < 3; ++i) {                                 /* a log that grows: it stays scrolled to the end */
        int k;
        br_ui_begin(&UI, &R, &IN, T);
        br_ui_panel_begin(&UI, "log", br_rect_make(0, 0, 200, 100));
        br_ui_stick_to_bottom(&UI);
        for (k = 0; k < 10 + 5 * i; ++k) br_ui_labelf(&UI, "line %d", k);
        br_ui_panel_end(&UI);
        br_ui_end(&UI);
        br_input_begin_frame(&IN);
    }
    for (slot = 0; slot < UI.scroll_count && UI.scrolls[slot].content < 150; ++slot) {}
    CHECK(slot < UI.scroll_count && UI.scrolls[slot].scroll > 0 &&
          fabs(UI.scrolls[slot].scroll - (UI.scrolls[slot].content - 100)) < 0.5);
    ui_teardown();
}

void suite_ui(void) {
    RUN(test_ui_area_double_click);
    RUN(test_ui_context_menu);
    RUN(test_ui_focus_and_stick);
    RUN(test_ui_clipboard);
    RUN(test_ui_drag_drop);
    RUN(test_ui_buttons);
    RUN(test_ui_checkbox_selectable);
    RUN(test_ui_drag_and_type_numbers);
    RUN(test_ui_text_field);
    RUN(test_ui_combo);
    RUN(test_ui_layout_and_scroll);
}
