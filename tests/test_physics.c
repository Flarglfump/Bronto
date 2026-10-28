/* Tests: rigid-body physics (physics.h). */
#include "test.h"
#include <math.h>
#include <string.h>

static br_world W;
static br_builtin_ids ID;
static br_component RB, COL, PW;
#define DT (1.0f / 60.0f)

static void setup(void) {
    br_world_init(&W);
    br_builtin_register(&W, &ID);
    RB = br_component_find(&W, "RigidBody");
    COL = br_component_find(&W, "Collider");
    PW = br_component_find(&W, "PhysicsWorld");
}

static br_transform *xf(br_entity e) { return (br_transform *)br_get(&W, e, ID.transform); }
static br_rigidbody *rb(br_entity e) { return (br_rigidbody *)br_get(&W, e, RB); }
static br_collider *col(br_entity e) { return (br_collider *)br_get(&W, e, COL); }

static br_entity box(float x, float y, float w, float h, br_body_type type) {
    br_entity e = br_entity_create(&W);
    br_collider *c;
    br_add(&W, e, ID.transform, NULL);
    xf(e)->position = br_v2(x, y);
    c = (br_collider *)br_add(&W, e, COL, NULL);
    c->size = br_v2(w, h);
    ((br_rigidbody *)br_add(&W, e, RB, NULL))->type = (int)type;
    return e;
}

static br_entity ball(float x, float y, float r) {
    br_entity e = box(x, y, 0, 0, BR_BODY_DYNAMIC);
    col(e)->shape = BR_COLLIDER_CIRCLE;
    col(e)->radius = r;
    return e;
}

static br_entity ground(void) {                    /* top surface at y = 160 */
    br_entity g = br_entity_create(&W);
    br_add(&W, g, ID.transform, NULL);
    xf(g)->position = br_v2(160, 170);
    ((br_collider *)br_add(&W, g, COL, NULL))->size = br_v2(1000, 20);   /* no RigidBody: scenery */
    return g;
}

static void run(float seconds) {
    int i, n = (int)(seconds * 60 + 0.5f);
    for (i = 0; i < n; ++i) br_world_run_phase(&W, BR_PHASE_FIXED_UPDATE, DT);
}

static int count_events(br_contact_kind kind) {
    uint32_t n, i;
    int c = 0;
    const br_contact_event *ev = br_physics_events(&W, &n);
    for (i = 0; i < n; ++i) c += ev[i].kind == kind;
    return c;
}

static void test_falls_and_rests(void) {
    br_entity b;
    setup();
    ground();
    b = box(160, 40, 20, 20, BR_BODY_DYNAMIC);
    run(0.5f);
    CHECK(xf(b)->position.y > 60 && rb(b)->velocity.y > 100);       /* gravity pulls down (+y) */
    run(3);
    CHECK_NEAR(xf(b)->position.y, 150, 1.0);                          /* resting on the ground */
    CHECK(br_absf(rb(b)->velocity.y) < 2 && br_absf(xf(b)->position.x - 160) < 0.01f);
    CHECK(br_absf(xf(b)->rotation) < 0.1f);
    br_world_destroy(&W);
}

static void test_stack_is_stable(void) {
    br_entity s[6];
    int i;
    setup();
    ground();
    for (i = 0; i < 6; ++i) s[i] = box(160, 150 - 20.5f * (float)i, 20, 20, BR_BODY_DYNAMIC);
    run(6);
    for (i = 0; i < 6; ++i) {
        CHECK_NEAR(xf(s[i])->position.y, 150 - 20 * (float)i, 2.5);
        CHECK(br_absf(xf(s[i])->position.x - 160) < 1.0f && br_absf(xf(s[i])->rotation) < 1.0f);
    }
    br_world_destroy(&W);
}

static void test_bounce(void) {
    br_entity b;
    float top = 1000;
    int i;
    setup();
    ground();
    b = ball(160, 60, 8);
    col(b)->bounce = 0.9f;
    run(1.0f);                                                         /* falls ~92 px, bounces */
    for (i = 0; i < 90; ++i) {
        run(DT);
        if (xf(b)->position.y < top) top = xf(b)->position.y;
    }
    CHECK(top < 80);                                                   /* back up most of the way */
    col(b)->bounce = 0;                                                /* a dead ball stops */
    run(4);
    CHECK_NEAR(xf(b)->position.y, 152, 1.0);
    br_world_destroy(&W);
}

static void test_tumbles_flat(void) {
    br_entity b;
    float r;
    setup();
    ground();
    b = box(160, 100, 30, 14, BR_BODY_DYNAMIC);
    xf(b)->rotation = 35;                                              /* lands on a corner */
    run(5);
    r = fmodf(br_absf(xf(b)->rotation), 180.0f);
    CHECK(r < 1.5f || r > 178.5f);                                     /* settled on its long side */
    CHECK_NEAR(xf(b)->position.y, 153, 1.5);
    br_world_destroy(&W);
}

static void test_friction(void) {
    br_entity a, b;
    setup();
    ground();
    a = box(60, 150, 20, 20, BR_BODY_DYNAMIC);
    b = box(260, 150, 20, 20, BR_BODY_DYNAMIC);
    col(b)->friction = 0;
    run(0.2f);
    rb(a)->velocity.x = 200;
    rb(b)->velocity.x = 200;
    run(2);
    CHECK(br_absf(rb(a)->velocity.x) < 1 && xf(a)->position.x < 160);   /* friction stopped it */
    CHECK(rb(b)->velocity.x > 190);                                     /* ice: still sliding */
    br_world_destroy(&W);
}

static void test_triggers_and_events(void) {
    br_entity zone, b;
    int enter = 0, exits = 0, begins = 0, i;
    setup();
    ground();
    zone = box(160, 80, 60, 20, BR_BODY_STATIC);
    col(zone)->trigger = true;
    b = box(160, 20, 10, 10, BR_BODY_DYNAMIC);
    for (i = 0; i < 180; ++i) {
        run(DT);
        enter += count_events(BR_TRIGGER_ENTER);
        exits += count_events(BR_TRIGGER_EXIT);
        begins += count_events(BR_CONTACT_BEGIN);
    }
    CHECK(enter == 1 && exits == 1);                                    /* fell straight through */
    CHECK(begins == 1);                                                 /* then landed, once */
    CHECK_NEAR(xf(b)->position.y, 155, 1.0);
    br_physics_apply_impulse(&W, b, br_v2(0, -300), NULL);             /* jump */
    for (i = 0; i < 5; ++i) { run(DT); if (count_events(BR_CONTACT_END)) break; }
    CHECK(i < 5 && rb(b)->velocity.y < 0);
    br_world_destroy(&W);
}

static void test_layers(void) {
    br_entity a, b;
    setup();
    ground();
    a = box(100, 100, 10, 10, BR_BODY_DYNAMIC);
    b = box(200, 100, 10, 10, BR_BODY_DYNAMIC);
    col(b)->layer = 3;
    col(b)->mask = ~1;                                                  /* ignores layer 0: the ground */
    run(2);
    CHECK_NEAR(xf(a)->position.y, 155, 1.0);
    CHECK(xf(b)->position.y > 400);                                     /* fell through */
    br_world_destroy(&W);
}

static void test_queries(void) {
    br_entity g, b;
    br_raycast_hit hit;
    setup();
    g = ground();
    b = ball(100, 100, 10);
    rb(b)->type = BR_BODY_STATIC;
    CHECK(br_physics_raycast(&W, br_v2(160, 0), br_v2(160, 300), -1, BR_ENTITY_NULL, &hit));
    CHECK(hit.entity == g && br_absf(hit.point.y - 160) < 0.01f && hit.normal.y < -0.99f);
    CHECK(br_physics_raycast(&W, br_v2(100, 0), br_v2(100, 300), -1, BR_ENTITY_NULL, &hit));
    CHECK(hit.entity == b && br_absf(hit.point.y - 90) < 0.01f);       /* the ball is in the way */
    CHECK(br_physics_raycast(&W, br_v2(100, 0), br_v2(100, 300), -1, b, &hit) && hit.entity == g);
    CHECK(!br_physics_raycast(&W, br_v2(0, 0), br_v2(300, 0), -1, BR_ENTITY_NULL, &hit));
    CHECK(br_physics_point(&W, br_v2(100, 95), -1) == b && br_physics_point(&W, br_v2(10, 10), -1) == BR_ENTITY_NULL);
    CHECK(br_physics_gravity(&W).y == 600);
    ((br_physics_world *)br_add(&W, br_entity_create(&W), PW, NULL))->gravity = br_v2(0, -100);
    CHECK(br_physics_gravity(&W).y == -100);
    br_world_destroy(&W);
}

static void test_ramp_and_kinematic(void) {
    br_entity ramp, b, pusher, crate;
    setup();
    ground();
    ramp = box(100, 120, 200, 10, BR_BODY_STATIC);
    xf(ramp)->rotation = 20;                                            /* slopes down to the right */
    b = ball(40, 70, 8);
    run(1.5f);
    CHECK(xf(b)->position.x > 80 && rb(b)->angular_velocity > 30);      /* rolled (clockwise) */
    pusher = box(250, 150, 10, 20, BR_BODY_KINEMATIC);
    crate = box(275, 150, 20, 20, BR_BODY_DYNAMIC);
    rb(pusher)->velocity = br_v2(60, 0);
    run(1);
    CHECK_NEAR(xf(pusher)->position.x, 310, 0.5);                       /* not slowed by the crate */
    CHECK(xf(crate)->position.x > 318);                                 /* pushed ahead of it */
    br_world_destroy(&W);
}

static void test_deterministic(void) {
    float first[4];
    int round, i;
    for (round = 0; round < 2; ++round) {
        br_entity e[4];
        setup();
        ground();
        for (i = 0; i < 4; ++i) {
            e[i] = i % 2 ? ball(150 + 7 * (float)i, 40 - 25 * (float)i, 7) : box(160 + 5 * (float)i, 40 - 25 * (float)i, 14, 14, BR_BODY_DYNAMIC);
            xf(e[i])->rotation = 11 * (float)i;
        }
        run(3);
        for (i = 0; i < 4; ++i) {
            if (round == 0) first[i] = xf(e[i])->position.x + xf(e[i])->rotation;
            else CHECK(first[i] == xf(e[i])->position.x + xf(e[i])->rotation);
        }
        br_world_destroy(&W);
    }
}

static void test_bodies_in_a_tree(void) {
    br_entity group, wall, crate, holder, kid;
    br_component parent;
    setup();
    parent = br_component_find(&W, "Parent");
    ground();
    group = br_entity_create(&W);                      /* a moving group: kinematic, no shape of its own */
    br_add(&W, group, ID.transform, NULL);
    xf(group)->position = br_v2(100, 150);
    ((br_rigidbody *)br_add(&W, group, RB, NULL))->type = BR_BODY_KINEMATIC;
    rb(group)->velocity = br_v2(60, 0);
    wall = br_entity_create(&W);                       /* its child: a collider with no body */
    br_add(&W, wall, ID.transform, NULL);
    xf(wall)->position = br_v2(10, 0);
    ((br_collider *)br_add(&W, wall, COL, NULL))->size = br_v2(10, 20);
    ((br_parent *)br_add(&W, wall, parent, NULL))->entity = group;
    crate = box(140, 150, 20, 20, BR_BODY_DYNAMIC);
    run(1);
    CHECK_NEAR(xf(group)->position.x, 160, 0.5);
    CHECK(xf(wall)->position.x == 10);                 /* still local */
    CHECK(xf(crate)->position.x > 175);                /* pushed by the child's collider */
    rb(group)->velocity = br_v2(0, 0);
    holder = br_entity_create(&W);                     /* a dynamic body under a turned, scaled parent */
    br_add(&W, holder, ID.transform, NULL);
    xf(holder)->position = br_v2(250, 20);
    xf(holder)->rotation = 90;
    xf(holder)->scale = br_v2(2, 2);
    kid = ball(0, 0, 5);
    ((br_parent *)br_add(&W, kid, parent, NULL))->entity = holder;
    run(2);
    {
        br_transform wt = br_world_transform(&W, kid);
        CHECK_NEAR(wt.position.x, 250, 0.5);            /* fell straight down the screen */
        CHECK_NEAR(wt.position.y, 150, 1.5);            /* resting on the ground (radius 5 x scale 2) */
        CHECK(xf(kid)->position.y < -1 || xf(kid)->position.x > 1);   /* its local position changed */
    }
    br_world_destroy(&W);
}

/* One-way platforms: a ball thrown up from below passes through, then lands on top;
 * a ball dropped from above lands. */
static void test_one_way_platforms(void) {
    br_entity plat, up_ball, down_ball;
    float lowest = 1e9f;
    int i;
    setup();
    ground();
    plat = box(160, 100, 120, 8, BR_BODY_STATIC);
    col(plat)->one_way = true;
    up_ball = ball(130, 150, 5);
    rb(up_ball)->velocity = br_v2(0, -520);                /* thrown up through it */
    down_ball = ball(190, 40, 5);
    for (i = 0; i < 180; ++i) {
        run(1.0f / 60);
        if (xf(up_ball)->position.y < lowest) lowest = xf(up_ball)->position.y;
    }
    CHECK(lowest < 90);                                    /* went up past the platform */
    CHECK_NEAR(xf(up_ball)->position.y, 100 - 4 - 5, 1.0);  /* and came down on top of it */
    CHECK_NEAR(xf(down_ball)->position.y, 100 - 4 - 5, 1.0);
    br_world_destroy(&W);
}

/* Joints: a pendulum on a hinge swings and keeps its length; a rod keeps two balls
 * apart; a rope lets them come closer but not further; a spring settles at its length. */
static br_joint *joint(br_entity e, br_entity other, int kind) {
    br_joint *j = (br_joint *)br_add(&W, e, br_component_find(&W, "Joint"), NULL);
    j->kind = kind;
    j->other = other;
    return j;
}

static void test_joints(void) {
    br_entity bob, a, b, c, d, hang, anchor_box;
    br_joint *j;
    float max_x = -1e9f, len;
    int i;
    setup();
    PW = br_component_find(&W, "PhysicsWorld");
    /* a pendulum: pinned to the world 60 px to its left */
    bob = ball(160, 50, 6);
    j = joint(bob, BR_ENTITY_NULL, BR_JOINT_DISTANCE);
    j->anchor = br_v2(0, 0);
    j->kind = BR_JOINT_HINGE;
    j->anchor = br_v2(-60, 0);                             /* the pin, 60 px to the left (in the bob's space) */
    for (i = 0; i < 120; ++i) {
        run(1.0f / 60);
        if (xf(bob)->position.x > max_x) max_x = xf(bob)->position.x;
    }
    len = br_v2_dist(xf(bob)->position, br_v2(100, 50));
    CHECK_NEAR(len, 60, 2.0);                              /* still on its circle */
    CHECK(xf(bob)->position.y > 60);                       /* it swung down */
    /* a rod between two free balls: thrown apart, they stay 40 apart */
    a = ball(400, 50, 5); b = ball(440, 50, 5);
    rb(a)->gravity_scale = rb(b)->gravity_scale = 0;
    rb(a)->velocity = br_v2(-100, 0); rb(b)->velocity = br_v2(100, 30);
    joint(a, b, BR_JOINT_DISTANCE);
    /* a rope: pushed together, they come closer; pulled apart, no further than 50 */
    c = ball(600, 50, 5); d = ball(650, 50, 5);
    rb(c)->gravity_scale = rb(d)->gravity_scale = 0;
    rb(c)->velocity = br_v2(40, 0);
    joint(c, d, BR_JOINT_ROPE);
    run(0.5f);
    CHECK_NEAR(br_v2_dist(xf(a)->position, xf(b)->position), 40, 1.5);
    CHECK(br_v2_dist(xf(c)->position, xf(d)->position) < 45);   /* slack: closer */
    rb(c)->velocity = br_v2(-300, 0);
    run(0.5f);
    CHECK(br_v2_dist(xf(c)->position, xf(d)->position) < 51.5f);
    CHECK(xf(d)->position.x < 650);                         /* pulled along */
    /* a spring hanging from a static box: settles below its length (gravity stretches it) */
    anchor_box = box(800, 20, 10, 10, BR_BODY_STATIC);
    hang = ball(800, 60, 5);
    j = joint(hang, anchor_box, BR_JOINT_SPRING);
    j->stiffness = 3;
    j->damping = 0.7f;
    run(4);
    {   /* stretch = g / omega^2 = 600 / (2 pi 3)^2 = 1.69 */
        float stretch = br_v2_dist(xf(hang)->position, xf(anchor_box)->position) - 40;
        CHECK(stretch > 1.0f && stretch < 2.5f);
        if (!(stretch > 1.0f && stretch < 2.5f)) printf("      spring stretch %.2f\n", (double)stretch);
        CHECK(fabsf(rb(hang)->velocity.y) < 2);
    }
    br_world_destroy(&W);
}

/* A bullet (a fast small ball) stops at a thin wall instead of passing through it;
 * the same ball without `bullet` tunnels. */
static void test_bullets(void) {
    br_entity fast, plain;
    setup();
    box(300, 50, 4, 400, BR_BODY_STATIC);                  /* a thin wall at x = 298..302 */
    fast = ball(100, 20, 3);
    rb(fast)->gravity_scale = 0;
    rb(fast)->velocity = br_v2(3000, 0);                    /* 50 px a step */
    rb(fast)->bullet = true;
    plain = ball(100, 80, 3);
    rb(plain)->gravity_scale = 0;
    rb(plain)->velocity = br_v2(3000, 0);
    run(0.25f);
    CHECK(xf(fast)->position.x < 300);
    CHECK(xf(plain)->position.x > 300);                     /* (what bullet is for) */
    br_world_destroy(&W);
}

void suite_physics(void) {
    RUN(test_bodies_in_a_tree);
    RUN(test_falls_and_rests);
    RUN(test_stack_is_stable);
    RUN(test_bounce);
    RUN(test_tumbles_flat);
    RUN(test_friction);
    RUN(test_triggers_and_events);
    RUN(test_layers);
    RUN(test_queries);
    RUN(test_ramp_and_kinematic);
    RUN(test_deterministic);
    RUN(test_one_way_platforms);
    RUN(test_joints);
    RUN(test_bullets);
}
