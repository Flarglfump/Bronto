/* Tests: memory, math, events, input. */
#include "test.h"
#include <string.h>

/* ======================= memory ========================================= */

static void test_alloc_stats(void) {
    br_mem_stats s0 = br_mem_get_stats(), s1;
    char *p = (char *)br_alloc(100);
    int *z = BR_NEW_ARRAY(int, 50);
    int i, all_zero = 1;
    CHECK(p != NULL && z != NULL);
    for (i = 0; i < 50; ++i) all_zero &= z[i] == 0;
    CHECK(all_zero);
    s1 = br_mem_get_stats();
    CHECK_EQ_INT(s1.live_allocations - s0.live_allocations, 2);
    CHECK_EQ_INT(s1.live_bytes - s0.live_bytes, 100 + 50 * sizeof(int));
    memset(p, 'x', 100);
    p = (char *)br_realloc(p, 4000);
    CHECK(p[0] == 'x' && p[99] == 'x');
    CHECK_EQ_INT(br_mem_get_stats().live_bytes - s0.live_bytes, 4000 + 50 * sizeof(int));
    br_free(p);
    br_free(z);
    br_free(NULL);
    CHECK_EQ_INT(br_mem_get_stats().live_allocations, s0.live_allocations);
    {
        char *d = br_strdup("bronto");
        CHECK(strcmp(d, "bronto") == 0);
        br_free(d);
    }
}

static void test_arena(void) {
    br_arena a;
    void *p1, *p2, *p3;
    size_t mark;
    CHECK(br_arena_init(&a, 1024));
    p1 = br_arena_alloc(&a, 3);
    p2 = br_arena_alloc(&a, 10);
    CHECK(((uintptr_t)p1 & 15u) == 0 && ((uintptr_t)p2 & 15u) == 0);
    CHECK((uint8_t *)p2 >= (uint8_t *)p1 + 3);
    mark = br_arena_mark(&a);
    p3 = br_arena_alloc(&a, 100);
    CHECK(p3 != NULL);
    br_arena_restore(&a, mark);
    CHECK(br_arena_alloc(&a, 100) == p3);       /* same memory handed out again */
    CHECK(br_arena_alloc(&a, 5000) == NULL);    /* exhaustion */
    br_arena_reset(&a);
    CHECK_EQ_INT(a.used, 0);
    CHECK(br_arena_alloc(&a, 8) == a.base);
    br_arena_destroy(&a);
}

static void test_pool(void) {
    br_pool p;
    void *blocks[8];
    int i;
    CHECK(br_pool_init(&p, 24, 8));
    for (i = 0; i < 8; ++i) { blocks[i] = br_pool_alloc(&p); CHECK(blocks[i] != NULL); }
    CHECK(br_pool_alloc(&p) == NULL);
    CHECK_EQ_INT(p.used_count, 8);
    br_pool_free(&p, blocks[3]);
    CHECK(br_pool_alloc(&p) == blocks[3]);
    for (i = 0; i < 8; ++i) br_pool_free(&p, blocks[i]);
    CHECK_EQ_INT(p.used_count, 0);
    br_pool_destroy(&p);
}

static void test_file_io(void) {
    const char *path = test_path("file_io.bin");
    const char msg[] = "hello\0bronto";
    size_t size = 0;
    char *data;
    CHECK(br_file_write_all(path, msg, sizeof(msg)));
    data = (char *)br_file_read_all(path, &size);
    CHECK(data != NULL);
    CHECK_EQ_INT(size, sizeof(msg));
    CHECK(data && memcmp(data, msg, sizeof(msg)) == 0);
    br_free(data);
    CHECK(br_file_read_all("definitely/not/here.txt", &size) == NULL);
}

void suite_memory(void) {
    RUN(test_alloc_stats);
    RUN(test_arena);
    RUN(test_pool);
    RUN(test_file_io);
}

/* ======================= math =========================================== */

static int m4_near(br_mat4 a, br_mat4 b, float eps) {
    int i;
    for (i = 0; i < 16; ++i) if (fabsf(a.m[i] - b.m[i]) > eps) return 0;
    return 1;
}

static void test_vectors(void) {
    br_vec2 a = br_v2(3, 4), r;
    br_vec3 x = br_v3(1, 0, 0), y = br_v3(0, 1, 0), z;
    CHECK_NEAR(br_v2_len(a), 5, 1e-6);
    CHECK_NEAR(br_v2_len(br_v2_norm(a)), 1, 1e-6);
    CHECK_NEAR(br_v2_dot(a, br_v2(1, 1)), 7, 1e-6);
    r = br_v2_rotate(br_v2(1, 0), BR_PI / 2);
    CHECK_NEAR(r.x, 0, 1e-6); CHECK_NEAR(r.y, 1, 1e-6);
    z = br_v3_cross(x, y);
    CHECK_NEAR(z.z, 1, 1e-6);
    CHECK_NEAR(br_v2_len(br_v2_norm(br_v2(0, 0))), 0, 1e-9); /* no NaN */
    CHECK_NEAR(br_lerpf(2, 4, 0.25f), 2.5, 1e-6);
}

static void test_matrices(void) {
    br_mat4 t = br_m4_translate(br_v3(1, 2, 3));
    br_mat4 r = br_m4_rotate(br_v3(0.3f, 1, 0.2f), 0.7f);
    br_mat4 s = br_m4_scale(br_v3(2, 3, 4));
    br_mat4 m = br_m4_mul(t, br_m4_mul(r, s));
    br_vec3 p = br_m4_mul_point(t, br_v3(1, 1, 1));
    br_vec3 q;
    CHECK_NEAR(p.x, 2, 1e-6); CHECK_NEAR(p.y, 3, 1e-6); CHECK_NEAR(p.z, 4, 1e-6);
    CHECK(m4_near(br_m4_mul(m, br_m4_inverse(m)), br_m4_identity(), 1e-5f));
    CHECK(m4_near(br_m4_transpose(br_m4_transpose(m)), m, 0));
    CHECK(m4_near(br_m4_rotate(br_v3(0, 0, 1), 0.5f), br_m4_rotate_z(0.5f), 1e-6f));
    CHECK(m4_near(br_quat_to_m4(br_quat_axis_angle(br_v3(0.3f, 1, 0.2f), 0.7f)), r, 1e-5f));
    /* Ortho maps the box corners to clip-space corners. */
    q = br_m4_mul_point(br_m4_ortho(0, 320, 180, 0, -1, 1), br_v3(320, 0, 0));
    CHECK_NEAR(q.x, 1, 1e-6); CHECK_NEAR(q.y, 1, 1e-6);
    /* look_at: the target ends up straight ahead (-Z) in view space. */
    q = br_m4_mul_point(br_m4_look_at(br_v3(0, 0, 5), br_v3(0, 0, 0), br_v3(0, 1, 0)), br_v3(0, 0, 0));
    CHECK_NEAR(q.x, 0, 1e-6); CHECK_NEAR(q.z, -5, 1e-5);
    /* Perspective: a point on the near plane maps to z = -1. */
    q = br_m4_mul_point(br_m4_perspective(BR_PI / 2, 1, 1, 100), br_v3(0, 0, -1));
    CHECK_NEAR(q.z, -1, 1e-5);
}

static void test_rng_rect(void) {
    br_rng a, b;
    int i, ok = 1, in_range = 1;
    br_recti ri;
    br_rng_seed(&a, 42); br_rng_seed(&b, 42);
    for (i = 0; i < 1000; ++i) ok &= br_rng_u32(&a) == br_rng_u32(&b);
    for (i = 0; i < 1000; ++i) {
        float f;
        int n;
        f = br_rng_float(&a);
        n = br_rng_int(&a, -3, 3);
        in_range &= f >= 0 && f < 1 && n >= -3 && n <= 3;
    }
    CHECK(ok); CHECK(in_range);
    CHECK(br_rect_overlaps(br_rect_make(0, 0, 10, 10), br_rect_make(5, 5, 10, 10)));
    CHECK(!br_rect_overlaps(br_rect_make(0, 0, 10, 10), br_rect_make(10, 0, 10, 10)));
    CHECK(br_rect_contains(br_rect_make(0, 0, 10, 10), br_v2(9.9f, 0)));
    ri = br_recti_intersect(br_recti_make(0, 0, 10, 10), br_recti_make(5, -5, 10, 10));
    CHECK(ri.x == 5 && ri.y == 0 && ri.w == 5 && ri.h == 5);
}

void suite_math(void) {
    RUN(test_vectors);
    RUN(test_matrices);
    RUN(test_rng_rect);
}

/* ======================= events ========================================= */

typedef struct ev_log { char text[256]; int count; } ev_log;
static void log_append(ev_log *l, const char *s) {
    strncat(l->text, s, sizeof(l->text) - strlen(l->text) - 1);
    l->count++;
}

static bool h_a(const br_event *e, void *u) { BR_UNUSED(e); log_append((ev_log *)u, "a"); return false; }
static bool h_b(const br_event *e, void *u) { BR_UNUSED(e); log_append((ev_log *)u, "b"); return false; }
static bool h_consume(const br_event *e, void *u) { BR_UNUSED(e); log_append((ev_log *)u, "C"); return true; }

static void test_queue_fifo_and_growth(void) {
    br_events ev;
    br_event e;
    int i, ok = 1;
    CHECK(br_events_init(&ev, 4));
    for (i = 0; i < 1000; ++i) {
        e = br_event_make(BR_EVENT_KEY_DOWN);
        e.u.key.key = (br_key)i;
        CHECK(br_events_post(&ev, &e));
        if (i % 3 == 0) { br_event out; br_events_poll(&ev, &out); }  /* force wrap-around */
    }
    i = 334;
    while (br_events_poll(&ev, &e)) { ok &= (int)e.u.key.key == i; ++i; }
    CHECK(ok);
    CHECK_EQ_INT(i, 1000);
    CHECK_EQ_INT(br_events_pending(&ev), 0);
    br_events_destroy(&ev);
}

static void test_subscribe_priority_consume(void) {
    br_events ev;
    ev_log l;
    br_event e;
    memset(&l, 0, sizeof(l));
    br_events_init(&ev, 16);
    br_events_subscribe(&ev, BR_EVENT_KEY_DOWN, h_a, &l, 0);
    br_events_subscribe(&ev, BR_EVENT_ANY, h_b, &l, 10);        /* higher runs first */
    br_events_subscribe(&ev, BR_EVENT_MOUSE_MOVE, h_consume, &l, 5);
    br_events_subscribe(&ev, BR_EVENT_MOUSE_MOVE, h_a, &l, 0);  /* blocked by consumer */
    e = br_event_make(BR_EVENT_KEY_DOWN);  br_events_post(&ev, &e);
    e = br_event_make(BR_EVENT_MOUSE_MOVE); br_events_post(&ev, &e);
    e = br_event_make(BR_EVENT_QUIT);       br_events_post(&ev, &e);
    CHECK_EQ_INT(br_events_dispatch(&ev), 3);
    CHECK(strcmp(l.text, "babCb") == 0);
    br_events_destroy(&ev);
}

typedef struct mutating_ctx { br_events *ev; ev_log *log; br_subscription self, other; } mutating_ctx;

static bool h_unsub_self_and_add(const br_event *e, void *u) {
    mutating_ctx *c = (mutating_ctx *)u;
    BR_UNUSED(e);
    log_append(c->log, "U");
    br_events_unsubscribe(c->ev, c->self);
    br_events_unsubscribe(c->ev, c->other);   /* lower priority: must not run now */
    br_events_subscribe(c->ev, BR_EVENT_ANY, h_b, c->log, 100); /* only from next event */
    return false;
}

static void test_mutation_during_dispatch(void) {
    br_events ev;
    ev_log l;
    mutating_ctx c;
    br_event e = br_event_make(BR_EVENT_QUIT);
    memset(&l, 0, sizeof(l));
    br_events_init(&ev, 16);
    c.ev = &ev; c.log = &l;
    c.self = br_events_subscribe(&ev, BR_EVENT_ANY, h_unsub_self_and_add, &c, 50);
    c.other = br_events_subscribe(&ev, BR_EVENT_ANY, h_a, &l, 0);
    br_events_post(&ev, &e);
    br_events_post(&ev, &e);
    br_events_dispatch(&ev);
    CHECK(strcmp(l.text, "Ub") == 0);
    CHECK_EQ_INT(ev.sub_count, 1);
    br_events_destroy(&ev);
}

typedef struct coin_msg { int amount; float x, y; } coin_msg;
typedef struct chain_ctx { br_events *ev; uint32_t coin, bonus; int total, bonuses; } chain_ctx;

static bool h_coin(const br_event *e, void *u) {
    chain_ctx *c = (chain_ctx *)u;
    const coin_msg *m = BR_EVENT_DATA(e, coin_msg);
    c->total += m->amount;
    if (m->amount >= 10) br_events_post_data(c->ev, c->bonus, NULL, 0);  /* chained message */
    return true;
}
static bool h_bonus(const br_event *e, void *u) { BR_UNUSED(e); ((chain_ctx *)u)->bonuses++; return true; }

static void test_user_messages(void) {
    br_events ev;
    chain_ctx c;
    coin_msg m;
    br_event direct;
    br_events_init(&ev, 16);
    memset(&c, 0, sizeof(c));
    c.ev = &ev;
    c.coin = br_events_register_type(&ev, "coin_collected");
    c.bonus = br_events_register_type(&ev, "bonus");
    CHECK(c.coin >= BR_EVENT_USER && c.bonus == c.coin + 1);
    CHECK(br_events_register_type(&ev, "coin_collected") == c.coin);
    CHECK(br_events_find_type(&ev, "quit") == BR_EVENT_QUIT);
    CHECK(strcmp(br_events_type_name(&ev, c.bonus), "bonus") == 0);
    br_events_subscribe(&ev, c.coin, h_coin, &c, 0);
    br_events_subscribe(&ev, c.bonus, h_bonus, &c, 0);
    m.amount = 5;  m.x = 1; m.y = 2; br_events_post_data(&ev, c.coin, &m, sizeof(m));
    m.amount = 10;                    br_events_post_data(&ev, c.coin, &m, sizeof(m));
    CHECK_EQ_INT(br_events_dispatch(&ev), 3);   /* chained bonus delivered same dispatch */
    CHECK_EQ_INT(c.total, 15);
    CHECK_EQ_INT(c.bonuses, 1);
    /* emit is synchronous and reports consumption */
    direct = br_event_make(c.bonus);
    CHECK(br_events_emit(&ev, &direct));
    CHECK_EQ_INT(c.bonuses, 2);
    { char big[100]; memset(big, 0, sizeof(big)); QUIET(CHECK(!br_events_post_data(&ev, c.coin, big, sizeof(big)))); }
    br_events_destroy(&ev);
}

static bool h_feedback(const br_event *e, void *u) { br_events_post((br_events *)u, e); return false; }

static void test_feedback_loop_guard(void) {
    br_events ev;
    br_event e = br_event_make(BR_EVENT_QUIT);
    br_log_level lvl = br_log_get_level();
    br_events_init(&ev, 16);
    br_events_subscribe(&ev, BR_EVENT_QUIT, h_feedback, &ev, 0);
    br_events_post(&ev, &e);
    br_log_set_level(BR_LOG_ERROR);
    CHECK_EQ_INT(br_events_dispatch(&ev), 65536);  /* terminates instead of hanging */
    br_log_set_level(lvl);
    br_events_destroy(&ev);
}

void suite_events(void) {
    RUN(test_queue_fifo_and_growth);
    RUN(test_subscribe_priority_consume);
    RUN(test_mutation_during_dispatch);
    RUN(test_user_messages);
    RUN(test_feedback_loop_guard);
}

/* ======================= input ========================================== */

static void feed(br_input *in, br_event e) { br_input_handle_event(in, &e); }

static void test_key_edges(void) {
    br_input in;
    br_input_init(&in);
    br_input_begin_frame(&in);
    feed(&in, br_event_key(BR_EVENT_KEY_DOWN, BR_KEY_A, 0, false));
    br_input_end_events(&in);
    CHECK(br_key_down(&in, BR_KEY_A) && br_key_pressed(&in, BR_KEY_A) && !br_key_released(&in, BR_KEY_A));

    br_input_begin_frame(&in);
    feed(&in, br_event_key(BR_EVENT_KEY_DOWN, BR_KEY_A, 0, true));   /* OS repeat */
    br_input_end_events(&in);
    CHECK(br_key_down(&in, BR_KEY_A) && !br_key_pressed(&in, BR_KEY_A) && br_key_repeated(&in, BR_KEY_A));

    br_input_begin_frame(&in);
    feed(&in, br_event_key(BR_EVENT_KEY_UP, BR_KEY_A, 0, false));
    br_input_end_events(&in);
    CHECK(!br_key_down(&in, BR_KEY_A) && br_key_released(&in, BR_KEY_A));

    /* Tap within one frame is still seen as pressed + released. */
    br_input_begin_frame(&in);
    feed(&in, br_event_key(BR_EVENT_KEY_DOWN, BR_KEY_SPACE, 0, false));
    feed(&in, br_event_key(BR_EVENT_KEY_UP, BR_KEY_SPACE, 0, false));
    br_input_end_events(&in);
    CHECK(br_key_pressed(&in, BR_KEY_SPACE) && br_key_released(&in, BR_KEY_SPACE) && !br_key_down(&in, BR_KEY_SPACE));

    br_input_begin_frame(&in);
    br_input_end_events(&in);
    CHECK(!br_key_pressed(&in, BR_KEY_SPACE) && !br_key_released(&in, BR_KEY_SPACE));
    CHECK(!br_key_down(&in, (br_key)9999));   /* out of range is safe */
    CHECK(strcmp(br_key_name(BR_KEY_Q), "Q") == 0 && strcmp(br_key_name(BR_KEY_F5), "F5") == 0);
}

static void test_mouse_text_focus(void) {
    br_input in;
    br_event t = br_event_make(BR_EVENT_TEXT_INPUT), w = br_event_make(BR_EVENT_MOUSE_WHEEL);
    br_input_init(&in);
    br_input_begin_frame(&in);
    feed(&in, br_event_mouse_move(10, 20, 0, 0));
    feed(&in, br_event_mouse_move(15, 18, 0, 0));
    feed(&in, br_event_mouse_button(BR_EVENT_MOUSE_BUTTON_DOWN, BR_MOUSE_LEFT, 15, 18));
    t.u.text.codepoint = 0x00E9; feed(&in, t);
    w.u.wheel.dy = -2; feed(&in, w);
    br_input_end_events(&in);
    CHECK_NEAR(in.mouse_x, 15, 0); CHECK_NEAR(in.mouse_dy, 18, 0);
    CHECK(br_mouse_pressed(&in, BR_MOUSE_LEFT) && br_mouse_down(&in, BR_MOUSE_LEFT));
    CHECK(in.text_count == 1 && in.text[0] == 0x00E9);
    CHECK_NEAR(in.wheel_y, -2, 0);

    br_input_begin_frame(&in);
    feed(&in, br_event_key(BR_EVENT_KEY_DOWN, BR_KEY_W, 0, false));
    feed(&in, br_event_make(BR_EVENT_WINDOW_FOCUS_LOST));
    br_input_end_events(&in);
    CHECK(!br_key_down(&in, BR_KEY_W) && !br_mouse_down(&in, BR_MOUSE_LEFT));
    CHECK(br_mouse_released(&in, BR_MOUSE_LEFT));
    CHECK_NEAR(in.mouse_dx, 0, 0);
    CHECK_EQ_INT(in.text_count, 0);
}

static void test_actions_and_gamepad(void) {
    br_input in;
    br_action jump, left, right;
    br_event ax = br_event_make(BR_EVENT_GAMEPAD_AXIS), bt = br_event_make(BR_EVENT_GAMEPAD_BUTTON_DOWN);
    br_input_init(&in);
    jump = br_input_action(&in, "jump");
    left = br_input_action(&in, "left");
    right = br_input_action(&in, "right");
    CHECK(br_input_action(&in, "jump") == jump && br_input_find_action(&in, "nope") == -1);
    br_input_bind(&in, jump, BR_BIND_KEY, BR_KEY_SPACE, -1);
    br_input_bind(&in, jump, BR_BIND_PAD_BUTTON, BR_PAD_A, -1);
    br_input_bind(&in, left, BR_BIND_KEY, BR_KEY_LEFT, -1);
    br_input_bind(&in, left, BR_BIND_PAD_AXIS_NEG, BR_PAD_AXIS_LEFT_X, 0);
    br_input_bind(&in, right, BR_BIND_KEY, BR_KEY_RIGHT, -1);

    /* frame 1: gamepad A pressed, stick pushed left past deadzone */
    br_input_begin_frame(&in);
    bt.u.gamepad.pad = 1; bt.u.gamepad.button = BR_PAD_A; feed(&in, bt);
    ax.u.gamepad.pad = 0; ax.u.gamepad.axis = BR_PAD_AXIS_LEFT_X; ax.u.gamepad.value = -0.6f; feed(&in, ax);
    br_input_end_events(&in);
    CHECK(br_action_pressed(&in, jump) && br_action_down(&in, jump));
    CHECK(br_action_down(&in, left));
    CHECK_NEAR(br_action_axis(&in, left, right), -1, 0);
    CHECK_NEAR(br_pad_axis(&in, 0, BR_PAD_AXIS_LEFT_X), -0.5, 1e-6);  /* deadzone rescaled */
    CHECK(br_pad_connected(&in, 1) && br_pad_pressed(&in, 1, BR_PAD_A));

    /* frame 2: still held -> down but not pressed; small stick value inside deadzone */
    br_input_begin_frame(&in);
    ax.u.gamepad.value = -0.1f; feed(&in, ax);
    br_input_end_events(&in);
    CHECK(br_action_down(&in, jump) && !br_action_pressed(&in, jump));
    CHECK(!br_action_down(&in, left) && br_action_released(&in, left));
    CHECK_NEAR(br_pad_axis(&in, 0, BR_PAD_AXIS_LEFT_X), 0, 0);

    /* frame 3: release A */
    br_input_begin_frame(&in);
    bt.type = BR_EVENT_GAMEPAD_BUTTON_UP; feed(&in, bt);
    br_input_end_events(&in);
    CHECK(br_action_released(&in, jump));

    /* frame 4: tap space within a single frame still triggers the action once */
    br_input_begin_frame(&in);
    feed(&in, br_event_key(BR_EVENT_KEY_DOWN, BR_KEY_SPACE, 0, false));
    feed(&in, br_event_key(BR_EVENT_KEY_UP, BR_KEY_SPACE, 0, false));
    br_input_end_events(&in);
    CHECK(br_action_pressed(&in, jump));
    br_input_begin_frame(&in);
    br_input_end_events(&in);
    CHECK(!br_action_down(&in, jump) && br_action_released(&in, jump));

    br_input_clear_bindings(&in, jump);
    CHECK_EQ_INT(in.actions[jump].binding_count, 0);
}

void suite_input(void) {
    RUN(test_key_edges);
    RUN(test_mouse_text_focus);
    RUN(test_actions_and_gamepad);
}
