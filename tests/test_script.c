/* Tests: BrontoScript and the Script component (script.h). */
#include "test.h"
#include <string.h>

static br_world W;
static br_builtin_ids ID;
static br_component SCRIPT, RB, COL;
static br_engine g_engine;                 /* scripts need world->context; only input/mixer/time are used */
static char g_printed[512];

static void on_print(const char *text, void *user) {
    BR_UNUSED(user);
    snprintf(g_printed, sizeof(g_printed), "%s", text);
}

static void setup(void) {
    memset(&g_engine, 0, sizeof(g_engine));
    br_input_init(&g_engine.input);
    br_mixer_init(&g_engine.mixer, 44100);
    br_world_init(&W);
    br_builtin_register(&W, &ID);
    W.context = &g_engine;
    SCRIPT = br_component_find(&W, "Script");
    RB = br_component_find(&W, "RigidBody");
    COL = br_component_find(&W, "Collider");
    br_assets_set_root(g_out_dir);
    br_script_set_print(on_print, NULL);
    g_printed[0] = 0;
}

static void teardown(void) {
    br_world_destroy(&W);
    br_assets_clear();
    br_assets_set_root("");
    br_script_set_print(NULL, NULL);
}

static void write_script(const char *name, const char *source) {
    CHECK(br_file_write_all(test_path(name), source, strlen(source)));
}

static br_entity scripted(const char *name, const char *file, const char *vars, br_vec2 pos) {
    br_entity e = br_entity_create(&W);
    br_script *s;
    snprintf(((br_name *)br_add(&W, e, ID.name, NULL))->text, 32, "%s", name);
    ((br_transform *)br_add(&W, e, ID.transform, NULL))->position = pos;
    s = (br_script *)br_add(&W, e, SCRIPT, NULL);
    snprintf(s->file, sizeof(s->file), "%s", file);
    snprintf(s->vars, sizeof(s->vars), "%s", vars ? vars : "");
    return e;
}

static void steps(int n) { while (n-- > 0) br_world_run_phase(&W, BR_PHASE_FIXED_UPDATE, 1.0f / 60.0f); }

static double num(br_entity e, const char *var) {
    double v = -12345;
    CHECK(br_script_get_number(&W, e, var, &v));
    return v;
}

static void test_check_and_errors(void) {
    char err[256];
    CHECK(br_script_check("var a = 1\nfunc update(dt) {\n  a += dt\n}\n", "ok.bs", err, sizeof(err)));
    CHECK(!br_script_check("func f() {\n  x = 1\n}\n", "a.bs", err, sizeof(err)));
    CHECK(strstr(err, "a.bs:2") && strstr(err, "unknown name 'x'"));
    CHECK(!br_script_check("func f() {\n  jmup()\n}\n", "b.bs", err, sizeof(err)) && strstr(err, "b.bs:2: unknown function 'jmup'"));
    CHECK(!br_script_check("func f() {\n  if true {\n", "c.bs", err, sizeof(err)) && strstr(err, "never closed"));
    CHECK(!br_script_check("func f() {\n  sqrt(1, 2)\n}\n", "d.bs", err, sizeof(err)) && strstr(err, "sqrt() takes 1 argument, not 2"));
    CHECK(!br_script_check("print(1)\n", "e.bs", err, sizeof(err)) && strstr(err, "only 'var' and 'func'"));
    CHECK(!br_script_check("var s = \"open\n", "f.bs", err, sizeof(err)) && strstr(err, "unfinished string"));
    CHECK(!br_script_check("func f() { var a = 1 2 }\n", "g.bs", err, sizeof(err)) && strstr(err, "end of the line"));
}

static void test_language(void) {
    br_entity e;
    char s[256];
    setup();
    write_script("lang.bs",
        "# everything the language does, checked from C\n"
        "var fib10\nvar text\nvar listsum\nvar vecx\nvar loops\nvar logic\nvar mod\nvar neg\nvar elifs\n"
        "var nested = [1, [2, 3]]\n"
        "func fib(n) {\n"
        "    if n < 2 { return n }\n"
        "    return fib(n - 1) + fib(n - 2)\n"
        "}\n"
        "func classify(n) {\n"
        "    if n < 0 { return \"neg\" } elif n == 0 { return \"zero\" } else { return \"pos\" }\n"
        "}\n"
        "func start() {\n"
        "    fib10 = fib(10)\n"
        "    text = \"score: \" + 42 + \" \" + true + \" \" + vec2(1, 2)\n"
        "    var l = [1, 2, 3]\n"
        "    push(l, 4)\n"
        "    l[0] = 10\n"
        "    listsum = 0\n"
        "    for x in l { listsum += x }\n"
        "    listsum += l[-1] * 100 + len(l) * 1000 + nested[1][0] * 10000\n"
        "    var v = vec2(3, 4) * 2 + vec2(1, 1)\n"
        "    v.x += 1\n"
        "    vecx = v.x * 100 + length(vec2(3, 4))\n"
        "    loops = 0\n"
        "    var i = 0\n"
        "    while true {\n"
        "        i += 1\n"
        "        if i % 2 == 0 { continue }\n"
        "        if i > 9 { break }\n"
        "        loops += 1\n"
        "    }\n"
        "    for k in 3 { loops += 100 }\n"
        "    logic = (nil or 5) + (0 and 99) + (not false and 10 or 0)\n"
        "    mod = -7 % 3\n"
        "    neg = -vec2(1, 2).y\n"
        "    elifs = classify(-1) + classify(0) + classify(5)\n"
        "    print(\"done\", fib10)\n"
        "}\n");
    e = scripted("lang", "lang.bs", NULL, br_v2(0, 0));
    steps(1);
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    CHECK(num(e, "fib10") == 55);
    CHECK(br_script_get_string(&W, e, "text", s, sizeof(s)) && strcmp(s, "score: 42 true (1, 2)") == 0);
    CHECK(num(e, "listsum") == 10 + 2 + 3 + 4 + 400 + 4000 + 20000);
    CHECK(num(e, "vecx") == 800 + 5);
    CHECK(num(e, "loops") == 5 + 300);                     /* 1 3 5 7 9, then 3 more */
    CHECK(num(e, "logic") == 5 + 0 + 10);
    CHECK(num(e, "mod") == 2);                             /* like Python: never negative */
    CHECK(num(e, "neg") == -2);
    CHECK(br_script_get_string(&W, e, "elifs", s, sizeof(s)) && strcmp(s, "negzeropos") == 0);
    CHECK(strcmp(g_printed, "done 55") == 0);
    teardown();
}

static void test_entities_and_vars(void) {
    br_entity e;
    const br_shape *shape;
    setup();
    write_script("mover.bs",
        "var speed = 10\nvar label = \"none\"\nvar updates = 0\n"
        "func start() {\n"
        "    self.position = vec2(5, 6)\n"
        "    self.Shape.color = rgb(255, 0, 0)\n"
        "    self.Shape.kind = \"ellipse\"\n"
        "    self.Shape.size.x = 30\n"
        "    self.name = \"Moved\"\n"
        "}\n"
        "func update(dt) {\n"
        "    updates += 1\n"
        "    self.x += speed\n"
        "    if self.Sprite != nil { speed = 0 }\n"
        "}\n");
    e = scripted("mover", "mover.bs", "speed = 3, label = \"hi\"", br_v2(0, 0));
    br_add(&W, e, ID.shape, NULL);
    steps(4);
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    CHECK(num(e, "updates") == 4 && num(e, "speed") == 3);
    CHECK(((br_transform *)br_get(&W, e, ID.transform))->position.x == 5 + 12);
    shape = (const br_shape *)br_get(&W, e, ID.shape);
    CHECK(br_color_eq(shape->color, br_rgb(255, 0, 0)) && shape->kind == BR_SHAPE_ELLIPSE && shape->size.x == 30);
    CHECK(strcmp(((br_name *)br_get(&W, e, ID.name))->text, "Moved") == 0);
    {
        char s[32];
        CHECK(br_script_get_string(&W, e, "label", s, sizeof(s)) && strcmp(s, "hi") == 0);
    }
    CHECK(br_script_set_number(&W, e, "speed", 0));
    steps(1);
    CHECK(((br_transform *)br_get(&W, e, ID.transform))->position.x == 17);
    teardown();
}

static void test_runtime_errors(void) {
    br_entity bad, good, loop;
    setup();
    write_script("bad.bs", "var n = 0\nvar gone\nfunc update(dt) {\n    n += 1\n    if n == 3 { var x = gone + 1 }\n}\n");   /* (nil + 1 itself: caught before it runs) */
    write_script("good.bs", "var n = 0\nfunc update(dt) { n += 1 }\n");
    write_script("loop.bs", "func start() {\n    while true { }\n}\n");
    bad = scripted("bad", "bad.bs", NULL, br_v2(0, 0));
    good = scripted("good", "good.bs", NULL, br_v2(0, 0));
    loop = scripted("loop", "loop.bs", NULL, br_v2(0, 0));
    QUIET(steps(5));
    CHECK(strstr(br_script_last_error(&W), "bad.bs:5: can't use + on nil and a number") != NULL);
    CHECK_EQ_INT(br_script_error_count(&W), 2);            /* each reported once (loop.bs: too long) */
    {
        double v;
        CHECK(!br_script_get_number(&W, bad, "n", &v));      /* stopped */
    }
    CHECK(num(good, "n") == 5);                            /* unaffected */
    BR_UNUSED(loop);
    {   /* a missing file and a typo'd one are reported, nothing crashes */
        scripted("missing", "nope.bs", NULL, br_v2(0, 0));
        write_script("typo.bs", "func update(dt) { self.x += speeed }\n");
        scripted("typo", "typo.bs", NULL, br_v2(0, 0));
        QUIET(steps(2));
        CHECK(strstr(br_script_last_error(&W), "typo.bs:1: unknown name 'speeed'") != NULL);
        CHECK_EQ_INT(br_script_error_count(&W), 4);
    }
    teardown();
}

static void test_physics_callbacks_and_messages(void) {
    br_entity ball, ground, zone, enemy;
    setup();
    write_script("ball.bs",
        "var hits = 0\nvar last = \"\"\nvar zones = 0\nvar damage = 0\n"
        "func on_collision(other, normal, speed) {\n"
        "    hits += 1\n"
        "    last = other.name\n"
        "    if normal.y > 0.9 and speed > 50 { damage = other.hit(3) }\n"
        "}\n"
        "func on_trigger(other) { zones += 1 }\n");
    write_script("ground.bs",
        "var health = 100\n"
        "func hit(amount) {\n"
        "    health -= amount\n"
        "    return health\n"
        "}\n");
    write_script("enemy.bs",
        "var target\n"
        "func start() { target = find(\"Ground\") }\n"
        "func update(dt) {\n"
        "    if target.health < 100 and target.health > 90 { target.health -= 1 }\n"
        "    send(target, \"hit\", 1)\n"
        "    send(target, \"no_such_function\")\n"
        "}\n");
    ground = scripted("Ground", "ground.bs", NULL, br_v2(160, 170));
    ((br_collider *)br_add(&W, ground, COL, NULL))->size = br_v2(300, 20);
    zone = br_entity_create(&W);
    ((br_transform *)br_add(&W, zone, ID.transform, NULL))->position = br_v2(160, 100);
    {
        br_collider *c = (br_collider *)br_add(&W, zone, COL, NULL);
        c->size = br_v2(100, 10);
        c->trigger = true;
    }
    ball = scripted("Ball", "ball.bs", NULL, br_v2(160, 50));
    br_add(&W, ball, RB, NULL);
    {
        br_collider *c = (br_collider *)br_add(&W, ball, COL, NULL);
        c->shape = BR_COLLIDER_CIRCLE;
        c->radius = 5;
    }
    steps(120);
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    CHECK(num(ball, "hits") == 1 && num(ball, "zones") == 1);
    {
        char s[32];
        CHECK(br_script_get_string(&W, ball, "last", s, sizeof(s)) && strcmp(s, "Ground") == 0);
    }
    CHECK(num(ball, "damage") == 97 && num(ground, "health") == 97);
    enemy = scripted("Enemy", "enemy.bs", NULL, br_v2(0, 0));
    steps(3);                                              /* -1 (send) per step, -1 more while 90..100 */
    CHECK(num(ground, "health") == 97 - 6);
    BR_UNUSED(enemy);
    teardown();
}

static void test_spawn_destroy_input(void) {
    br_entity shooter;
    br_world prefab;
    setup();
    br_world_init(&prefab);
    br_builtin_register(&prefab, NULL);
    {   /* a bullet prefab */
        br_entity b = br_entity_create(&prefab);
        snprintf(((br_name *)br_add(&prefab, b, br_component_find(&prefab, "Name"), NULL))->text, 32, "Bullet");
        br_add(&prefab, b, br_component_find(&prefab, "Transform"), NULL);
        CHECK(br_world_save_file(&prefab, &b, 1, test_path("bullet.scene")));
    }
    br_world_destroy(&prefab);
    write_script("shooter.bs",
        "var fired = 0\n"
        "func update(dt) {\n"
        "    if key_pressed(\"space\") {\n"
        "        var b = spawn(\"bullet.scene\", self.position + vec2(10, 0))\n"
        "        fired += 1\n"
        "    }\n"
        "    if key_down(\"Left\") and len(find_all(\"Bullet\")) > 0 { destroy(find(\"Bullet\")) }\n"
        "}\n");
    shooter = scripted("Shooter", "shooter.bs", NULL, br_v2(50, 50));
    steps(1);
    g_engine.input.keys_pressed[BR_KEY_SPACE] = true;
    steps(1);
    g_engine.input.keys_pressed[BR_KEY_SPACE] = false;
    steps(1);
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    CHECK(num(shooter, "fired") == 1 && br_world_entity_count(&W) == 2);
    {
        br_entity bullet = W.pools[ID.name].entities[1];
        CHECK(((br_transform *)br_get(&W, bullet, ID.transform))->position.x == 60);
    }
    g_engine.input.keys[BR_KEY_LEFT] = true;
    steps(1);
    CHECK(br_world_entity_count(&W) == 1);                 /* destroyed (deferred to the end of the step) */
    teardown();
}

typedef struct counter_state { int starts, updates, hits; } counter_state;
static int g_destroyed;
static void cb_start(br_world *w, br_entity e, void *s) { BR_UNUSED(w); BR_UNUSED(e); ((counter_state *)s)->starts++; }
static void cb_update(br_world *w, br_entity e, void *s, float dt) { BR_UNUSED(w); BR_UNUSED(e); BR_UNUSED(dt); ((counter_state *)s)->updates++; }
static void cb_destroy(br_world *w, br_entity e, void *s) { BR_UNUSED(w); BR_UNUSED(e); g_destroyed += ((counter_state *)s)->updates; }
static const br_behaviour k_counter = { sizeof(counter_state), cb_start, cb_update, NULL, NULL, cb_destroy };

static void test_c_behaviour_and_reload(void) {
    br_entity c, s1, s2;
    setup();
    CHECK(br_script_register_behaviour("counter", &k_counter));
    c = scripted("c", "counter", NULL, br_v2(0, 0));
    g_destroyed = 0;
    steps(3);
    br_entity_destroy(&W, c);
    CHECK_EQ_INT(g_destroyed, 3);
    write_script("v.bs", "var version = 1\n");
    s1 = scripted("s1", "v.bs", NULL, br_v2(0, 0));
    steps(1);
    CHECK(num(s1, "version") == 1);
    write_script("v.bs", "var version = 2\n");
    br_script_reload(&W);                                  /* (file times have 1 s resolution) */
    s2 = scripted("s2", "v.bs", NULL, br_v2(0, 0));
    steps(1);
    CHECK(num(s1, "version") == 1 && num(s2, "version") == 2);   /* running ones keep their version */
    teardown();
}

static void test_garbage(void) {
    br_entity e;
    setup();
    write_script("churn.bs",
        "var keep = []\nvar n = 0\n"
        "func update(dt) {\n"
        "    var junk = []\n"
        "    for i in 200 { push(junk, \"text \" + i) }\n"
        "    n += 1\n"
        "    if n % 10 == 0 { push(keep, junk[5]) }\n"
        "}\n");
    e = scripted("churn", "churn.bs", NULL, br_v2(0, 0));
    steps(300);                                            /* ~60000 strings, mostly garbage */
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    CHECK(num(e, "n") == 300);
    {
        double kept = 0;
        CHECK(br_script_call(&W, e, "update", (const double[]){ 0 }, 1));
        CHECK(br_script_get_number(&W, e, "n", &kept) && kept == 301);
    }
    teardown();                                            /* the leak check covers the heap */
}

/* The example project plays: walk right, collect the first coin, jump onto a platform. */
static void test_platformer_example(void) {
    int n, i;
    char err[200];
    br_entity hud = BR_ENTITY_NULL, player = BR_ENTITY_NULL;
    double coins = -1;
    const br_transform *pt;
    setup();
    br_assets_set_root("examples/platformer/");
    n = br_world_load_file(&W, "examples/platformer/main.scene", NULL, 0, err, sizeof(err));
    CHECK(n > 10);
    for (i = 0; i < (int)W.pools[ID.name].count; ++i) {
        const char *name = ((const br_name *)(W.pools[ID.name].data + (size_t)i * W.pools[ID.name].stride))->text;
        if (strcmp(name, "HUD") == 0) hud = W.pools[ID.name].entities[i];
        if (strcmp(name, "Player") == 0) player = W.pools[ID.name].entities[i];
    }
    steps(30);                                             /* settle */
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    CHECK(br_script_get_number(&W, hud, "total", &coins) && coins == 5);
    g_engine.input.keys[BR_KEY_RIGHT] = true;              /* run right to the first coin (x 120) */
    steps(70);
    g_engine.input.keys[BR_KEY_RIGHT] = false;
    CHECK(br_script_get_number(&W, hud, "coins", &coins) && coins == 1);
    {
        char text[64];
        const br_label *l = (const br_label *)br_get(&W, hud, ID.label);
        CHECK(l && strcmp(l->text, "Coins: 1 / 5") == 0);
        BR_UNUSED(text);
    }
    pt = (const br_transform *)br_get(&W, player, ID.transform);
    CHECK(pt && pt->position.x > 120 && br_absf(pt->position.y - 155.5f) < 1.5f);   /* on the ground */
    g_engine.input.keys[BR_KEY_LEFT] = true;               /* back under platform 1, then jump onto it */
    steps(25);
    g_engine.input.keys[BR_KEY_LEFT] = false;
    g_engine.input.keys_pressed[BR_KEY_SPACE] = true;
    steps(1);
    g_engine.input.keys_pressed[BR_KEY_SPACE] = false;
    steps(4);
    pt = (const br_transform *)br_get(&W, player, ID.transform);
    CHECK(pt && pt->position.y < 145);                     /* in the air */
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    teardown();
}

static void test_tree_from_scripts(void) {
    br_entity boss, arm, hand;
    double v;
    setup();
    write_script("boss.bs",
        "var arms = 0\nvar hand_x = 0\nvar spawned = 0\n"
        "func start() {\n"
        "    var arm = child(self, \"Arm\")\n"
        "    arms = len(children(self))\n"
        "    hand_x = child(arm, \"Hand\").global_position.x\n"
        "    self.rotation = 90\n"
        "    var shot = spawn(\"part.scene\", vec2(5, 0), arm)\n"
        "    if shot.parent == arm and shot.global_position.y > 0 { spawned = 1 }\n"
        "    arm.parent = nil\n"
        "}\n");
    {
        br_world tmp;
        br_entity e;
        br_world_init(&tmp);
        br_builtin_register(&tmp, NULL);
        e = br_entity_create(&tmp);
        br_add(&tmp, e, br_component_find(&tmp, "Transform"), NULL);
        CHECK(br_world_save_file(&tmp, &e, 1, test_path("part.scene")));
        br_world_destroy(&tmp);
    }
    boss = scripted("Boss", "boss.bs", NULL, br_v2(100, 100));
    arm = br_entity_create(&W);
    snprintf(((br_name *)br_add(&W, arm, ID.name, NULL))->text, 32, "Arm");
    ((br_transform *)br_add(&W, arm, ID.transform, NULL))->position = br_v2(20, 0);
    br_set_parent(&W, arm, boss, false);
    hand = br_entity_create(&W);
    snprintf(((br_name *)br_add(&W, hand, ID.name, NULL))->text, 32, "Hand");
    ((br_transform *)br_add(&W, hand, ID.transform, NULL))->position = br_v2(10, 0);
    br_set_parent(&W, hand, arm, false);
    steps(1);
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    CHECK(br_script_get_number(&W, boss, "arms", &v) && v == 1);
    CHECK(br_script_get_number(&W, boss, "hand_x", &v) && v == 130);
    CHECK(br_script_get_number(&W, boss, "spawned", &v) && v == 1);
    CHECK(br_parent_of(&W, arm) == BR_ENTITY_NULL);                       /* detached, but didn't move */
    CHECK_NEAR(br_world_transform(&W, hand).position.y, 130, 1e-3);
    teardown();
}

/* The VS Code extension documents every built-in function the language has. */
static void test_extension_knows_builtins(void) {
    size_t size;
    char *json = (char *)br_file_read_all("tools/vscode/brontoscript/lib/builtins.json", &size);
    int i;
    CHECK(json != NULL);
    if (!json) return;
    CHECK(br_script_builtin_count() > 40 && br_script_builtin_name(br_script_builtin_count()) == NULL);
    for (i = 0; i < br_script_builtin_count(); ++i) {
        char key[64];
        snprintf(key, sizeof(key), "\"name\": \"%s\"", br_script_builtin_name(i));
        if (!strstr(json, key)) printf("      builtins.json is missing %s\n", br_script_builtin_name(i));
        CHECK(strstr(json, key) != NULL);
    }
    br_free(json);
}


/* Static typing: annotations are checked before a script runs. */
static void test_types_checked(void) {
    static const struct { const char *src, *err; } cases[] = {
        /* accepted */
        { "var speed: number = 90\nvar lives: int = 3\nvar name: string\nvar alive: bool = true\nvar at: vec2 = vec2(1, 2)\n"
          "var items: list[number] = [1, 2.5]\nvar target: entity\nvar anything: any = \"x\"\nvar count := 0\n"
          "func hit(amount: int, from: entity) -> bool {\n    lives -= amount\n    return lives <= 0\n}\n"
          "func update(dt: number) -> void {\n    speed += dt * 2\n    var d := distance(at, self.position)\n    count += floor(d)\n"
          "    for i: int in 3 { count += i }\n    for v in items { speed += v }\n    if hit(1, nil) { print(\"out\") }\n}\n", NULL },
        { "@strict\nvar a: int = 1\nfunc f(x: number) -> number {\n    var y := x * 2\n    return y\n}\n", NULL },
        { "func f() -> int {\n    if true { return 1 } else { return 2 }\n}\n", NULL },
        { "func start() -> void {\n    self.scale = 2\n    self.parent = nil\n    var e: entity = find(\"x\")\n}\n", NULL },
        /* rejected */
        { "var speed: number = \"fast\"\n", "t.bs:1: speed is a number, so it can't hold a string" },
        { "var lives: int = 2.5\n", "lives is an int, and this number may have a fraction" },
        { "var n := nil\n", "can't tell the type of n's value" },
        { "var c := 1\nfunc f() {\n    c = \"two\"\n}\n", "t.bs:3: c is an int, so it can't hold a string" },
        { "func f(a: vec2) {\n}\nfunc g() {\n    f(3)\n}\n", "t.bs:4: f()'s parameter 'a' is a vec2, so it can't hold an int" },
        { "func f() -> int {\n    return \"x\"\n}\n", "what f() returns is an int, so it can't hold a string" },
        { "func f() -> int {\n    if true { return 1 }\n}\n", "not every way through f() ends with a return" },
        { "func f() -> void {\n    return 3\n}\n", "returns nothing (-> void), so return can't have a value" },
        { "func f() {\n    var s: string = \"a\" - 1\n}\n", "can't use - on a string and an int" },
        { "func f() {\n    key_down(3)\n}\n", "key_down() wants a string for argument 1, not an int" },
        { "func f() {\n    destroy(nil)\n}\n", "destroy() wants an entity for argument 1, not a nil" },
        { "func f() {\n    var v := vec2(1, 2)\n    v.z = 1\n}\n", "a vec2 has x, y and length, not 'z'" },
        { "func f() {\n    var xs: list[int] = [1, 2]\n    push(xs, \"three\")\n}\n", "push(): the list holds int, not string" },
        { "func f() {\n    var n := 3\n    for x in \"abc\" { }\n}\n", "for ... in needs a list or a number, not a string" },
        { "func update(dt: string) {\n}\n", "the engine passes a number as update()'s parameter 'dt', not a string" },
        { "var x: flaot = 1\n", "unknown type 'flaot'" },
        { "func f() {\n    var s := print(\"x\")\n}\n", "returns nothing" },
        { "func f() {\n    self.id = 3\n}\n", "id can't be changed" },
        { "@strict\nvar a = 1\n", "t.bs:2: @strict: give 'a' a type" },
        { "@strict\nfunc f(x) -> void {\n}\n", "@strict: give the parameter 'x' a type" },
        { "@strict\nfunc f() {\n}\n", "@strict: say what f() returns" },
        { "@strict\nfunc f() -> void {\n    var y = 2\n}\n", "@strict: give 'y' a type" },
        { "var a = 1\n@strict\n", "@strict goes at the top of the file" },
        { "@tool\n", "unknown annotation '@tool'" },
    };
    size_t i;
    char err[256];
    for (i = 0; i < BR_ARRAY_COUNT(cases); ++i) {
        bool ok = br_script_check(cases[i].src, "t.bs", err, sizeof(err));
        if (cases[i].err == NULL) { CHECK(ok); if (!ok) fprintf(stderr, "      case %u: %s\n", (unsigned)i, err); }
        else {
            CHECK(!ok && strstr(err, cases[i].err));
            if (ok || !strstr(err, cases[i].err)) fprintf(stderr, "      case %u: got \"%s\"\n", (unsigned)i, ok ? "(accepted)" : err);
        }
    }
    br_script_set_strict(true);                              /* the project's setting: every script strict */
    CHECK(!br_script_check("var a = 1\n", "t.bs", err, sizeof(err)) && strstr(err, "@strict: give 'a' a type"));
    CHECK(br_script_check("var a: int = 1\n", "t.bs", err, sizeof(err)));
    br_script_set_strict(false);
    CHECK(br_script_check("var a = 1\n", "t.bs", err, sizeof(err)));
}

/* ... and again when it runs: values from untyped code, other scripts and C. */
static void test_types_at_run_time(void) {
    br_entity e, f;
    double v;
    char s[64];
    setup();
    write_script("typed.bs",
        "var hp: int\nvar label: string\nvar spot: vec2\nvar bag: list[int]\nvar who: entity\nvar loose\n"
        "var ok := false\n"
        "func start() -> void {\n"
        "    ok = hp == 0 and label == \"\" and spot.x == 0 and len(bag) == 0 and who == nil\n"
        "    push(bag, 1)\n"
        "}\n"
        "func set_hp(v) -> void { hp = v }\n"
        "func add(a: int, b: int) -> int { return a + b }\n"
        "func half(n) -> int { return n / 2 }\n"
        "func update(dt: number) -> void { }\n");
    write_script("poke.bs",
        "var which = 0\n"
        "func update(dt) {\n"
        "    var t = find(\"typed\")\n"
        "    if which == 1 { t.set_hp(2.5) }\n"
        "    if which == 2 { t.hp = \"lots\" }\n"
        "    if which == 3 { t.half(3) }\n"
        "    if which == 4 { var x: int = t.add(2, 3); t.hp = x }\n"
        "}\n");
    e = scripted("typed", "typed.bs", NULL, br_v2(0, 0));
    f = scripted("poke", "poke.bs", NULL, br_v2(0, 0));
    steps(1);
    CHECK(br_script_get_number(&W, e, "ok", &v) && v == 1);          /* the defaults */
    CHECK(br_script_get_string(&W, e, "label", s, sizeof(s)) && s[0] == 0);
    CHECK(!br_script_set_number(&W, e, "label", 3));                  /* a string stays a string */
    CHECK(!br_script_set_number(&W, e, "hp", 1.5) && br_script_set_number(&W, e, "hp", 4));
    br_script_set_number(&W, f, "which", 4);
    steps(1);
    CHECK(br_script_get_number(&W, e, "hp", &v) && v == 5);
    br_script_set_number(&W, f, "which", 1);
    QUIET(steps(1));
    CHECK(strstr(br_script_last_error(&W), "typed.bs") && strstr(br_script_last_error(&W), "hp is an int, and 2.5 isn't a whole number"));
    teardown();

    setup();
    write_script("typed.bs", "var hp: int\nfunc update(dt) { }\n");
    write_script("poke.bs", "func update(dt) {\n    find(\"typed\").hp = \"lots\"\n}\n");
    scripted("typed", "typed.bs", NULL, br_v2(0, 0));
    scripted("poke", "poke.bs", NULL, br_v2(0, 0));
    QUIET(steps(2));
    CHECK(strstr(br_script_last_error(&W), "poke.bs:2: hp is an int, so it can't hold a string"));
    teardown();

    setup();
    write_script("ret.bs", "func half(n) -> int { return n / 2 }\nfunc start() { var r = half(3) }\n");
    scripted("r", "ret.bs", NULL, br_v2(0, 0));
    QUIET(steps(1));
    CHECK(strstr(br_script_last_error(&W), "what half() returns is an int, and 1.5 isn't a whole number"));
    teardown();

    setup();                                                   /* Script.vars must fit too */
    write_script("vars.bs", "var speed: number = 1\nfunc update(dt) { }\n");
    scripted("v", "vars.bs", "speed = \"fast\"", br_v2(0, 0));
    QUIET(steps(1));
    CHECK(strstr(br_script_last_error(&W), "Script.vars sets 'speed', but it is a number"));
    teardown();
}

void suite_script(void) {
    RUN(test_extension_knows_builtins);
    RUN(test_tree_from_scripts);
    RUN(test_platformer_example);
    RUN(test_check_and_errors);
    RUN(test_types_checked);
    RUN(test_types_at_run_time);
    RUN(test_language);
    RUN(test_entities_and_vars);
    RUN(test_runtime_errors);
    RUN(test_physics_callbacks_and_messages);
    RUN(test_spawn_destroy_input);
    RUN(test_c_behaviour_and_reload);
    RUN(test_garbage);
}
