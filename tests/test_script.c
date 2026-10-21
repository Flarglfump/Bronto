/* Tests: BrontoScript and the Script component (script.h). */
#include "test.h"
#include <math.h>
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
    br_save_set_folder(test_path("saves"));      /* (saved data stays out of the real place) */
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

/* Tilemaps from scripts, substr(), and spawning a big prefab (a room: a
 * root with more than 64 children). */
static void test_tiles_substr_big_spawn(void) {
    br_entity runner, map;
    br_tilemap *m;
    setup();
    {   /* the prefab: a root, 100 children (written by hand, as the generator would) */
        char *text = (char *)br_alloc(20000);
        size_t at = 0;
        int i;
        at += (size_t)snprintf(text + at, 20000 - at, "bronto_scene 1\nentity\n  Name\n    text = \"Room\"\n  Transform\n    position = 0 0\n");
        for (i = 0; i < 100; ++i)
            at += (size_t)snprintf(text + at, 20000 - at, "entity\n  Name\n    text = \"Bit\"\n  Transform\n    position = %d 0\n  Parent\n    entity = 0\n", i);
        CHECK(br_file_write_all(test_path("room_test.scene"), text, at));
        br_free(text);
    }
    map = br_entity_create(&W);
    snprintf(((br_name *)br_add(&W, map, ID.name, NULL))->text, 32, "Map");
    ((br_transform *)br_add(&W, map, ID.transform, NULL))->position = br_v2(16, 8);
    m = (br_tilemap *)br_add(&W, map, ID.tilemap, NULL);      /* 2 x 1 cells of 16: x 0..32, y 0..16 */
    m->columns = 2;
    m->rows = 1;
    strcpy(m->tiles, ".#");
    strcpy(m->solid, "#");
    write_script("tiles.bs",
        "var a = \"\"\nvar solid_left = true\nvar solid_right = false\nvar c := vec2(0, 0)\nvar part = \"\"\nvar kids = 0\n"
        "func start() {\n"
        "    var m = find(\"Map\")\n"
        "    a = tile_at(m, vec2(4, 4)) + tile_at(m, vec2(20, 4)) + tile_at(m, vec2(40, 4)) + \"|\"\n"
        "    solid_left = tile_solid(m, vec2(4, 4))\n"
        "    solid_right = tile_solid(m, vec2(20, 4))\n"
        "    c = tile_center(m, vec2(20, 4))\n"
        "    set_tile(m, vec2(4, 4), \"#\")\n"
        "    part = substr(\"recipe\", 2, 3) + substr(\"recipe\", -2) + substr(\"ab\", 5)\n"
        "    var room = spawn(\"room_test.scene\", vec2(10, 20), self)\n"
        "    kids = len(children(room))\n"
        "}\n");
    runner = scripted("Runner", "tiles.bs", NULL, br_v2(0, 0));
    steps(1);
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    {
        char buf[64];
        CHECK(br_script_get_string(&W, runner, "a", buf, sizeof(buf)) && strcmp(buf, ".#|") == 0);
        CHECK(br_script_get_string(&W, runner, "part", buf, sizeof(buf)) && strcmp(buf, "cippe") == 0);
    }
    CHECK(num(runner, "solid_left") == 0 && num(runner, "solid_right") == 1);
    CHECK(strcmp(((br_tilemap *)br_get(&W, map, ID.tilemap))->tiles, "##") == 0);
    CHECK(num(runner, "kids") == 100);
    CHECK_EQ_INT(br_world_entity_count(&W), 103);            /* map, runner, room + 100: all parented */
    teardown();
}

/* A spawned entity's script is ready at once: the spawner can set its
 * variables and call it before its start() runs (next step). */
static void test_spawn_then_set(void) {
    br_entity maker;
    setup();
    write_script("ball.bs", "var speed = 1\nvar seen = 0\nvar told = \"\"\nfunc start() {\n    seen = speed\n}\nfunc tell(s) {\n    told = s\n}\n");
    {
        const char text[] = "bronto_scene 1\nentity\n  Name\n    text = \"Ball\"\n  Transform\n    position = 0 0\n  Script\n    file = \"ball.bs\"\n    vars = \"speed = 2\"\n";
        CHECK(br_file_write_all(test_path("ball.scene"), text, sizeof(text) - 1));
    }
    write_script("maker.bs", "var before = 0\nfunc start() {\n    var b = spawn(\"ball.scene\", vec2(5, 5))\n    before = b.speed\n    b.speed = 7\n    b.tell(\"hi\")\n}\n");
    maker = scripted("Maker", "maker.bs", NULL, br_v2(0, 0));
    steps(3);
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    CHECK(num(maker, "before") == 2);                                  /* the prefab's vars were applied */
    {
        br_entity ball = W.pools[ID.name].entities[1];
        char buf[16];
        CHECK(num(ball, "seen") == 7);                                  /* start() saw what the maker set */
        CHECK(br_script_get_string(&W, ball, "told", buf, sizeof(buf)) && strcmp(buf, "hi") == 0);
    }
    teardown();
}

/* Saved data from scripts: save_data / load_data and friends, and lists in
 * Script.vars (both read values the same way). */
static void test_saved_data(void) {
    char folder[BR_PATH_MAX];
    br_entity e, v;
    int errors;
    setup();
    br_path_join(folder, sizeof(folder), g_out_dir, "script_saves");
    br_save_set_folder(folder);
    br_save_delete("progress.txt");
    br_save_delete("damaged.txt");
    write_script("saver.bs",
                 "var pass = 0\nvar missing = 0\nvar names = 0\nvar after = 0\n"
                 "func start() {\n"
                 "    var v = [3, \"ow_7_7\", vec2(-128.5, 152), true, nil, [1, [2, []]], \"say \\\"hi\\\" \\\\ \\n done\", 0.1, -7]\n"
                 "    if not save_data(\"progress\", v) { return }\n"
                 "    var got = load_data(\"progress\")\n"
                 "    if str(got) == str(v) and got[2].x == -128.5 and got[6] == v[6] and got[7] == 0.1 and len(got[5][1][1]) == 0 { pass = 1 }\n"
                 "    missing = load_data(\"nothing\", 42)\n"
                 "    if load_data(\"nothing\") == nil and has_data(\"progress\") and not has_data(\"nothing\") { missing += 1 }\n"
                 "    names = len(data_names())\n"
                 "    if data_names()[0] == \"damaged\" and data_names()[1] == \"progress\" { names += 10 }\n"
                 "    if load_data(\"damaged\", \"fallback\") == \"fallback\" { names += 100 }\n"
                 "    delete_data(\"progress\")\n"
                 "    if not has_data(\"progress\") and load_data(\"progress\", 5) == 5 { after = 1 }\n"
                 "}\n");
    CHECK(br_save_write("damaged.txt", "[1, 2", 5));
    e = scripted("Saver", "saver.bs", NULL, br_v2(0, 0));
    steps(2);
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    CHECK(num(e, "pass") == 1);
    CHECK(num(e, "missing") == 43);
    CHECK(num(e, "names") == 112);                       /* damaged and progress, in order; a damaged file gives the default */
    CHECK(num(e, "after") == 1);
    {
        size_t size;
        char *text;
        write_script("saver2.bs", "func start() { save_data(\"plain\", [1, \"a\", vec2(2, 3)]) }\n");
        scripted("Saver2", "saver2.bs", NULL, br_v2(0, 0));
        steps(2);
        text = (char *)br_save_read("plain.txt", &size);      /* readable text, like code */
        CHECK(text && strcmp(text, "[1, \"a\", vec2(2, 3)]\n") == 0);
        br_free(text);
        br_save_delete("plain.txt");
    }
    /* what can't be saved, and names that aren't allowed: errors */
    errors = br_script_error_count(&W);
    write_script("bad_save.bs", "func start() { save_data(\"me\", [self]) }\n");
    scripted("BadSave", "bad_save.bs", NULL, br_v2(0, 0));
    steps(2);
    CHECK_EQ_INT(br_script_error_count(&W), errors + 1);
    CHECK(strstr(br_script_last_error(&W), "can't save an entity") != NULL);
    write_script("bad_name.bs", "func start() { load_data(\"../up\") }\n");
    scripted("BadName", "bad_name.bs", NULL, br_v2(0, 0));
    steps(2);
    CHECK_EQ_INT(br_script_error_count(&W), errors + 2);
    CHECK(strstr(br_script_last_error(&W), "can't name saved data") != NULL);
    /* lists (and negative vec2s) in Script.vars */
    write_script("holder.bs", "var items: list = []\nvar spot := vec2(0, 0)\nvar ok = 0\n"
                              "func start() { if len(items) == 3 and items[1] == \"two\" and items[2][0] == -1 and spot.y == -4 { ok = 1 } }\n");
    v = scripted("Holder", "holder.bs", "items = [\"one\", \"two\", [-1]], spot = vec2(3, -4)", br_v2(0, 0));
    steps(2);
    CHECK(num(v, "ok") == 1);
    CHECK_EQ_INT(br_script_error_count(&W), errors + 2);
    br_save_delete("damaged.txt");
    br_fs_remove(folder);
    br_save_set_folder(NULL);
    teardown();
}

/* Volume settings and choosing keys from scripts: set_volume / volume on the
 * mixer's buses (a Sound component plays on its bus), and last_key(). */
static void test_volume_and_keys(void) {
    br_entity e;
    double m = 0, mus = 0, master = 0;
    char key[16];
    setup();
    write_script("volume.bs", "var m = 0\nvar music = 0\nvar master = 0\nvar key = \"\"\n"
                              "func start() { set_volume(\"music\", 0.25); set_volume(\"master\", 0.5); m = volume(\"music\"); music = volume(\"sfx\"); master = volume(\"master\") }\n"
                              "func update(dt) { var k = last_key(); if k != \"\" { key = k } }\n");
    e = scripted("Volume", "volume.bs", NULL, br_v2(0, 0));
    steps(2);
    CHECK(br_script_get_number(&W, e, "m", &m) && fabs(m - 0.25) < 1e-6);
    CHECK(br_script_get_number(&W, e, "music", &mus) && mus == 1);
    CHECK(br_script_get_number(&W, e, "master", &master) && fabs(master - 0.5) < 1e-6);
    CHECK(fabsf(g_engine.mixer.bus_volume[BR_BUS_MUSIC] - 0.25f) < 1e-6f && fabsf(g_engine.mixer.master_volume - 0.5f) < 1e-6f);
    br_input_begin_frame(&g_engine.input);
    {
        br_event k = br_event_key(BR_EVENT_KEY_DOWN, BR_KEY_Q, 0, false);
        br_input_handle_event(&g_engine.input, &k);
    }
    steps(1);
    CHECK(br_script_get_string(&W, e, "key", key, sizeof(key)) && strcmp(key, "q") == 0);
    write_script("badbus.bs", "func start() { set_volume(\"loud\", 1) }\n");
    scripted("BadBus", "badbus.bs", NULL, br_v2(0, 0));
    steps(2);
    CHECK(strstr(br_script_last_error(&W), "there is no \"loud\"") != NULL);
    {   /* a Sound component's bus is saved by name */
        const br_component_desc *d = br_component_get_desc(&W, ID.sound);
        int i, found = 0;
        for (i = 0; d && i < d->field_count; ++i) if (strcmp(d->fields[i].name, "bus") == 0) found = 1;
        CHECK(found);
    }
    g_engine.mixer.master_volume = 1;
    g_engine.mixer.bus_volume[BR_BUS_MUSIC] = 1;
    teardown();
}

/* Pantry Quest (examples/pantryquest, a project made of scenes and scripts):
 * Pip walks into the first cave, takes the spoon, and walks back out. */
static br_entity named(const char *name) {
    uint32_t i;
    for (i = 0; i < W.pools[ID.name].count; ++i)
        if (strcmp(((const br_name *)(W.pools[ID.name].data + (size_t)i * W.pools[ID.name].stride))->text, name) == 0)
            return W.pools[ID.name].entities[i];
    return BR_ENTITY_NULL;
}
static void hold_key(br_key k, int n) {
    g_engine.input.keys[k] = true;
    steps(n);
    g_engine.input.keys[k] = false;
}
static void test_pantryquest_example(void) {
    char err[200], room[32];
    br_entity game, hero;
    double weapon = -1;
    const br_transform *t;
    setup();
    br_assets_set_root("examples/pantryquest/");
    CHECK(br_world_load_file(&W, "examples/pantryquest/main.scene", NULL, 0, err, sizeof(err)) > 20);
    game = named("Game");
    hero = named("Hero");
    steps(40);                                             /* the first room fades in */
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    CHECK(br_script_get_string(&W, game, "room_name", room, sizeof(room)) && strcmp(room, "ow_7_7") == 0);
    CHECK(named("Map") != BR_ENTITY_NULL);
    hold_key(BR_KEY_LEFT, 35);                             /* to the cave mouth's column (x 72)... */
    t = (const br_transform *)br_get(&W, hero, ID.transform);
    CHECK(t && t->position.x > 69 && t->position.x < 75);
    hold_key(BR_KEY_UP, 70);                               /* ...and up into it */
    steps(40);
    CHECK(br_script_get_string(&W, game, "room_name", room, sizeof(room)) && strcmp(room, "cave_chef") == 0);
    hold_key(BR_KEY_UP, 45);                               /* the spoon */
    CHECK(br_script_get_number(&W, game, "weapon", &weapon) && weapon == 1);
    steps(110);                                            /* holds it up, then can move again */
    CHECK(br_script_get_string(&W, game, "state", room, sizeof(room)) && strcmp(room, "play") == 0);
    hold_key(BR_KEY_DOWN, 90);                             /* and out again */
    steps(40);
    CHECK(br_script_get_string(&W, game, "room_name", room, sizeof(room)) && strcmp(room, "ow_7_7") == 0);
    t = (const br_transform *)br_get(&W, hero, ID.transform);
    CHECK(t && br_absf(t->position.x - 72) < 2 && t->position.y > 100);
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    teardown();
}

/* Pantry Quest: sliding into a cellar room whose shutter closes behind Pip
 * leaves him clear of the doorway, free to walk (he used to be shut in). */
static void test_pantryquest_shutter(void) {
    char err[200], room[32];
    br_entity game, hero;
    br_script *sc;
    const br_transform *t;
    float x, y;
    setup();
    br_assets_set_root("examples/pantryquest/");
    CHECK(br_world_load_file(&W, "examples/pantryquest/main.scene", NULL, 0, err, sizeof(err)) > 20);
    game = named("Game");
    hero = named("Hero");
    sc = (br_script *)br_get(&W, game, SCRIPT);
    CHECK(sc != NULL);
    if (!sc) { teardown(); return; }
    snprintf(sc->vars, sizeof(sc->vars), "start_room = \"d1_2_2\", start_pos = vec2(40, 152), weapon = 1");
    steps(40);
    CHECK(br_script_get_string(&W, game, "room_name", room, sizeof(room)) && strcmp(room, "d1_2_2") == 0);
    hold_key(BR_KEY_LEFT, 40);                             /* out through the west doorway... */
    steps(80);                                             /* ...into the cutter room, whose east shutter closes */
    CHECK(br_script_get_string(&W, game, "room_name", room, sizeof(room)) && strcmp(room, "d1_1_2") == 0);
    CHECK(br_script_get_string(&W, game, "state", room, sizeof(room)) && strcmp(room, "play") == 0);
    t = (const br_transform *)br_get(&W, hero, ID.transform);
    CHECK(t && t->position.x <= 216);                      /* past the doorway's two tiles */
    if (!t) { teardown(); return; }
    x = t->position.x;
    y = t->position.y;
    hold_key(BR_KEY_UP, 10);                               /* and he can walk */
    t = (const br_transform *)br_get(&W, hero, ID.transform);
    CHECK(t && t->position.y < y - 4);
    hold_key(BR_KEY_LEFT, 10);
    t = (const br_transform *)br_get(&W, hero, ID.transform);
    CHECK(t && t->position.x < x - 4);
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    teardown();
}

/* Pantry Quest's overworld (examples/pantryquest/rooms, made by its tools):
 * 16 x 8 screens whose edges meet, sealed all round, with all nine cellar
 * doors. */
static bool pq_field(const char *text, const char *key, char *out, size_t size) {
    const char *p = strstr(text, key), *q;
    if (!p || !(p = strchr(p, '"')) || !(q = strchr(p + 1, '"')) || (size_t)(q - p - 1) >= size) return false;
    memcpy(out, p + 1, (size_t)(q - p - 1));
    out[q - p - 1] = 0;
    return true;
}
static void test_pantryquest_world(void) {
    static char tiles[16][8][16 * 11 + 1];
    char solid[64] = "", path[96];
    int x, y, i, k, cellars = 0;
    bool ok = true;
    for (y = 0; y < 8; ++y) {
        for (x = 0; x < 16; ++x) {
            size_t size;
            char *text;
            snprintf(path, sizeof(path), "examples/pantryquest/rooms/ow_%d_%d.scene", x, y);
            text = (char *)br_file_read_all(path, &size);
            CHECK(text != NULL);
            if (!text) return;
            CHECK(pq_field(text, "    tiles = ", tiles[x][y], sizeof(tiles[x][y])) && strlen(tiles[x][y]) == 16 * 11);
            pq_field(text, "    solid = ", solid, sizeof(solid));
            for (k = 1; k <= 9; ++k) {
                char door[32];
                snprintf(door, sizeof(door), "to = \\\"d%d_2_4\\\"", k);
                if (strstr(text, door)) cellars |= 1 << k;
            }
            br_free(text);
        }
    }
    CHECK(solid[0] != 0);
#define PQ_OPEN(xx, yy, c, r) (strchr(solid, tiles[xx][yy][(r) * 16 + (c)]) == NULL)
    for (y = 0; y < 8; ++y) {
        for (x = 0; x < 16; ++x) {
            for (i = 0; i < 16; ++i) {                          /* top and bottom edges */
                if (y == 0 && PQ_OPEN(x, y, i, 0)) ok = false;
                if (y == 7 && PQ_OPEN(x, y, i, 10)) ok = false;
                if (y < 7 && PQ_OPEN(x, y, i, 10) != PQ_OPEN(x, y + 1, i, 0)) ok = false;
            }
            for (i = 0; i < 11; ++i) {                          /* left and right */
                if (x == 0 && PQ_OPEN(x, y, 0, i)) ok = false;
                if (x == 15 && PQ_OPEN(x, y, 15, i)) ok = false;
                if (x < 15 && PQ_OPEN(x, y, 15, i) != PQ_OPEN(x + 1, y, 0, i)) ok = false;
            }
            if (!ok) { printf("      ow_%d_%d's edges don't meet its neighbours'\n", x, y); CHECK(ok); return; }
        }
    }
#undef PQ_OPEN
    CHECK_EQ_INT(cellars, 0x3fe);                               /* cellars 1 to 9 */
}

/* Pantry Quest's cellar guardians, fought from here: Pip stays put (his
 * health kept full) and pokes at the guardian every few steps. The Big Dill
 * only feels it while dizzy from charging into a wall; the Jammerwock only
 * while it is up out of the jam; the Spice Wraith only while it is solid.
 * Each must go down, and the cellar's music comes back. `only` lists the
 * modes a hit may land in (NULL: any). */
static void pq_fight(const char *room, int max_hp, const char *only) {
    char err[200], mode[32], music[32];
    br_entity game, boss = BR_ENTITY_NULL;
    br_script *sc;
    double hp = 0, last_hp, life = 32;
    int i, landed = 0, shrugged = 0;
    static const double poke[3] = { 1, 0, 0 };
    setup();
    br_assets_set_root("examples/pantryquest/");
    CHECK(br_world_load_file(&W, "examples/pantryquest/main.scene", NULL, 0, err, sizeof(err)) > 20);
    game = named("Game");
    sc = (br_script *)br_get(&W, game, SCRIPT);
    snprintf(sc->vars, sizeof(sc->vars), "start_room = \"%s\", start_pos = vec2(128, 200), weapon = 1, hearts_max = 16", room);
    steps(40);
    boss = named("Enemy");
    CHECK(boss != BR_ENTITY_NULL);
    if (boss == BR_ENTITY_NULL) { teardown(); return; }
    last_hp = max_hp;
    for (i = 0; i < 60 * 60 && br_entity_alive(&W, boss); ++i) {
        br_script_set_number(&W, game, "hp", life);
        steps(1);
        if (i % 7 || !br_entity_alive(&W, boss)) continue;
        mode[0] = 0;
        br_script_get_string(&W, boss, "mode", mode, sizeof(mode));
        br_script_call(&W, boss, "hit", poke, 3);
        if (!br_entity_alive(&W, boss)) { landed++; break; }
        br_script_get_number(&W, boss, "hp", &hp);
        if (hp < last_hp) {
            landed++;
            if (only) CHECK(mode[0] && strstr(only, mode) != NULL);
        } else if (only && !(mode[0] && strstr(only, mode))) {
            shrugged++;
        }
        last_hp = hp;
    }
    CHECK(!br_entity_alive(&W, boss));
    CHECK_EQ_INT(landed, max_hp);
    if (only) CHECK(shrugged > 0);
    steps(2);
    CHECK(br_script_get_string(&W, game, "music_name", music, sizeof(music)) && strcmp(music, "cellar") == 0);
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    teardown();
}
/* The Gumdrop King: four hits and he bursts into two gumdrops, two hits
 * each and those burst into two little ones; one hit pops each of those.
 * Twelve in all, poking at every gumdrop in the room, and his bar counts
 * them down. */
static void pq_fight_gumdrops(void) {
    char err[200], music[32];
    br_entity game, king, all[32];
    br_script *sc;
    double hp = 0, last_hp = 12, life = 32;
    int i, j, n, landed = 0, most = 0;
    static const double poke[3] = { 1, 0, 0 };
    setup();
    br_assets_set_root("examples/pantryquest/");
    CHECK(br_world_load_file(&W, "examples/pantryquest/main.scene", NULL, 0, err, sizeof(err)) > 20);
    game = named("Game");
    sc = (br_script *)br_get(&W, game, SCRIPT);
    snprintf(sc->vars, sizeof(sc->vars), "start_room = \"d5_2_0\", start_pos = vec2(128, 200), weapon = 1, hearts_max = 16");
    steps(40);
    king = named("Enemy");
    CHECK(king != BR_ENTITY_NULL);
    if (king == BR_ENTITY_NULL) { teardown(); return; }
    steps(60 * 4);                                     /* (his entrance) */
    CHECK(br_script_get_number(&W, king, "hp", &hp) && hp == 12);
    for (i = 0; i < 60 * 60 && br_entity_alive(&W, king); ++i) {
        br_script_set_number(&W, game, "hp", life);
        steps(1);
        if (i % 9) continue;
        n = 0;
        for (j = 0; j < (int)W.pools[ID.name].count && n < 32; ++j)
            if (strcmp(((const br_name *)(W.pools[ID.name].data + (size_t)j * W.pools[ID.name].stride))->text, "Enemy") == 0)
                all[n++] = W.pools[ID.name].entities[j];
        if (n - 1 > most) most = n - 1;                /* (the king stays, unseen, to keep count) */
        for (j = 0; j < n; ++j) br_script_call(&W, all[j], "hit", poke, 3);
        steps(1);
        if (!br_entity_alive(&W, king)) { landed += (int)last_hp; break; }
        br_script_get_number(&W, king, "hp", &hp);
        if (hp < last_hp) landed += (int)(last_hp - hp);
        last_hp = hp;
    }
    CHECK(!br_entity_alive(&W, king));
    CHECK_EQ_INT(landed, 12);
    CHECK_EQ_INT(most, 4);
    steps(2);
    CHECK(br_script_get_string(&W, game, "music_name", music, sizeof(music)) && strcmp(music, "cellar") == 0);
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    teardown();
}
static void test_pantryquest_bosses(void) {
    pq_fight("d2_2_1", 6, "dizzy");             /* the Big Dill */
    pq_fight("d3_1_0", 8, NULL);                /* the Spice Wraith */
    pq_fight("d4_2_0", 8, "up rise");           /* the Jammerwock */
    pq_fight_gumdrops();                        /* the Gumdrop King */
}

/* Pantry Quest: Pip walks in eight directions - a diagonal as fast as
 * straight - and is eased round a corner he only just touches, so he slips
 * into a cave mouth without lining up first. */
static void test_pantryquest_eight_ways(void) {
    char err[200], room[32];
    br_entity game, hero;
    br_script *sc;
    const br_transform *t;
    float x0, y0;
    setup();
    br_assets_set_root("examples/pantryquest/");
    CHECK(br_world_load_file(&W, "examples/pantryquest/main.scene", NULL, 0, err, sizeof(err)) > 20);
    game = named("Game");
    hero = named("Hero");
    sc = (br_script *)br_get(&W, game, SCRIPT);
    snprintf(sc->vars, sizeof(sc->vars), "start_room = \"ow_7_7\", start_pos = vec2(120, 168)");
    steps(40);
    t = (const br_transform *)br_get(&W, hero, ID.transform);
    if (!t) { teardown(); return; }
    x0 = t->position.x;
    y0 = t->position.y;
    g_engine.input.keys[BR_KEY_UP] = g_engine.input.keys[BR_KEY_LEFT] = true;     /* up and to the left */
    steps(30);
    g_engine.input.keys[BR_KEY_UP] = g_engine.input.keys[BR_KEY_LEFT] = false;
    t = (const br_transform *)br_get(&W, hero, ID.transform);
    CHECK(t && x0 - t->position.x > 26 && x0 - t->position.x < 33);   /* 84 px/s for 0.5 s, shared between the axes */
    CHECK(t && y0 - t->position.y > 26 && y0 - t->position.y < 33);
    CHECK(t && fabsf((x0 - t->position.x) - (y0 - t->position.y)) < 1);
    /* four pixels right of the first cave's mouth (column 4: x 64 to 80), walking straight up */
    teardown();
    setup();
    br_assets_set_root("examples/pantryquest/");
    CHECK(br_world_load_file(&W, "examples/pantryquest/main.scene", NULL, 0, err, sizeof(err)) > 20);
    game = named("Game");
    sc = (br_script *)br_get(&W, game, SCRIPT);
    snprintf(sc->vars, sizeof(sc->vars), "start_room = \"ow_7_7\", start_pos = vec2(76, 120)");
    steps(40);
    hold_key(BR_KEY_UP, 60);
    steps(40);
    CHECK(br_script_get_string(&W, game, "room_name", room, sizeof(room)) && strcmp(room, "cave_chef") == 0);
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    teardown();
}

/* Pantry Quest's title, files and settings, played with key presses: start
 * a new game in file 1, save it from the pause menu, quit to the title and
 * find the file there; then choose a new key and a softer volume. */
static void pq_press(br_key k) {
    g_engine.input.keys_pressed[k] = g_engine.input.keys[k] = true;
    steps(1);
    g_engine.input.keys_pressed[k] = g_engine.input.keys[k] = false;
    steps(2);
}
static bool pq_load(const char *scene) {
    char err[200], path[96];
    setup();
    br_assets_set_root("examples/pantryquest/");
    snprintf(path, sizeof(path), "examples/pantryquest/%s", scene);
    return br_world_load_file(&W, path, NULL, 0, err, sizeof(err)) > 5;
}
/* Pantry Quest's wafer plank and whisk. With the plank Pip walks over jam
 * one tile wide (the Jam Jar's moat room), and not without it; the whisk,
 * out on the land, whirls him to the door of a cellar whose page he has. */
static float pq_walk_west(const char *treasures) {
    br_script *sc;
    const br_transform *t;
    float x;
    if (!pq_load("main.scene")) { teardown(); return -1; }
    sc = (br_script *)br_get(&W, named("Game"), SCRIPT);
    snprintf(sc->vars, sizeof(sc->vars), "start_room = \"d4_0_2\", start_pos = vec2(200, 152), weapon = 1, treasures = [%s]", treasures);
    steps(40);
    hold_key(BR_KEY_LEFT, 90);
    t = (const br_transform *)br_get(&W, named("Hero"), ID.transform);
    x = t ? t->position.x : -1;
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    teardown();
    return x;
}
static void test_pantryquest_plank_and_whisk(void) {
    char room[32];
    br_entity game;
    br_script *sc;
    float x;
    x = pq_walk_west("");
    CHECK(x > 128);                                    /* stopped at the jam (x 112 to 128) */
    x = pq_walk_west("\"plank\"");
    CHECK(x > 0 && x < 100);                           /* over it on the plank */
    CHECK(pq_load("main.scene"));
    game = named("Game");
    sc = (br_script *)br_get(&W, game, SCRIPT);
    snprintf(sc->vars, sizeof(sc->vars), "start_room = \"ow_7_7\", start_pos = vec2(120, 168), start_items = \"whisk\", "
             "flags = [\"page:d1\", \"page:d3\"]");
    steps(40);
    pq_press(BR_KEY_Z);
    steps(40);
    CHECK(br_script_get_string(&W, game, "room_name", room, sizeof(room)) && strcmp(room, "ow_7_5") == 0);   /* the Cold Cellar's door */
    pq_press(BR_KEY_Z);
    steps(40);
    CHECK(br_script_get_string(&W, game, "room_name", room, sizeof(room)) && strcmp(room, "ow_1_6") == 0);   /* then the Spice Rack's */
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    teardown();
}

static void test_pantryquest_files_and_settings(void) {
    char folder[BR_PATH_MAX], text[64];
    size_t size;
    char *saved;
    double slot = 0;
    br_path_join(folder, sizeof(folder), g_out_dir, "pq_saves");
    br_save_set_folder(folder);
    br_save_delete("slot1.txt"); br_save_delete("session.txt"); br_save_delete("settings.txt");
    /* the title: PLAY, then file 1 (empty: a new game) */
    CHECK(pq_load("title.scene"));
    steps(4);
    pq_press(BR_KEY_ENTER);
    CHECK(named("FilesMenu") != BR_ENTITY_NULL);
    pq_press(BR_KEY_ENTER);
    CHECK(br_script_scene_request(&W, text, sizeof(text)) && strcmp(text, "main.scene") == 0);
    CHECK(br_save_exists("session.txt"));
    teardown();
    /* the game: file 1 is saved at once; pause, save, then save and quit */
    CHECK(pq_load("main.scene"));
    steps(60);
    CHECK(br_script_get_number(&W, named("Game"), "slot", &slot) && slot == 1);
    CHECK(br_save_exists("slot1.txt") && !br_save_exists("session.txt"));
    br_script_set_number(&W, named("Game"), "beans", 42);          /* something to find again */
    pq_press(BR_KEY_ESCAPE);
    CHECK(named("PauseMenu") != BR_ENTITY_NULL);
    pq_press(BR_KEY_DOWN);
    pq_press(BR_KEY_ENTER);                                       /* SAVE GAME */
    CHECK(br_script_get_string(&W, named("Game"), "state", text, sizeof(text)) && strcmp(text, "pause") == 0);
    {
        const br_label *note = (const br_label *)br_get(&W, named("Note"), ID.label);
        CHECK(note && strcmp(note->text, "GAME SAVED!") == 0);
    }
    pq_press(BR_KEY_DOWN);
    pq_press(BR_KEY_DOWN);
    pq_press(BR_KEY_ENTER);                                       /* QUIT TO TITLE: save first? */
    pq_press(BR_KEY_ENTER);                                       /* SAVE AND QUIT */
    CHECK(br_script_scene_request(&W, text, sizeof(text)) && strcmp(text, "title.scene") == 0);
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    teardown();
    saved = (char *)br_save_read("slot1.txt", &size);
    CHECK(saved && strstr(saved, "\"room\", \"ow_7_7\"") && strstr(saved, "\"place\", \"PANTRY PLAINS\""));
    br_free(saved);
    /* the title again: file 1 shows where it is; then the settings */
    CHECK(pq_load("title.scene"));
    steps(4);
    pq_press(BR_KEY_ENTER);
    {
        const br_label *place = (const br_label *)br_get(&W, named("Place0"), ID.label);
        CHECK(place && strcmp(place->text, "PANTRY PLAINS") == 0);
    }
    pq_press(BR_KEY_ENTER);                                       /* carry on with file 1 */
    CHECK(br_script_scene_request(&W, text, sizeof(text)) && strcmp(text, "main.scene") == 0);
    teardown();
    {
        double beans = 0;
        CHECK(pq_load("main.scene"));
        steps(60);
        CHECK(br_script_get_number(&W, named("Game"), "beans", &beans) && beans == 42);
        CHECK(br_script_get_string(&W, named("Game"), "room_name", text, sizeof(text)) && strcmp(text, "ow_7_7") == 0);
        teardown();
    }
    CHECK(pq_load("title.scene"));
    steps(4);
    pq_press(BR_KEY_DOWN);
    pq_press(BR_KEY_ENTER);                                       /* SETTINGS */
    CHECK(named("SettingsMenu") != BR_ENTITY_NULL);
    pq_press(BR_KEY_LEFT);                                        /* the music, one step softer */
    CHECK(fabsf(g_engine.mixer.bus_volume[BR_BUS_MUSIC] - 0.7f) < 1e-4f);
    pq_press(BR_KEY_DOWN);
    pq_press(BR_KEY_DOWN);                                        /* UP */
    pq_press(BR_KEY_ENTER);
    steps(10);
    pq_press(BR_KEY_I);                                           /* is now I */
    pq_press(BR_KEY_ESCAPE);                                      /* leave: saved */
    CHECK_EQ_INT(br_script_error_count(&W), 0);
    if (br_script_error_count(&W)) printf("      %s\n", br_script_last_error(&W));
    teardown();
    saved = (char *)br_save_read("settings.txt", &size);
    CHECK(saved && strncmp(saved, "[[[\"i\", \"w\"]", 12) == 0 && strstr(saved, "], 7, 10]"));
    CHECK(saved && strstr(saved, "[\"enter\", \"\"]"));          /* I was the Pocket Pantry's: a key does one thing */
    br_free(saved);
    g_engine.mixer.bus_volume[BR_BUS_MUSIC] = g_engine.mixer.bus_volume[BR_BUS_SFX] = g_engine.mixer.bus_volume[BR_BUS_UI] = 1;
    br_save_delete("slot1.txt"); br_save_delete("settings.txt");
    br_fs_remove(folder);
    br_save_set_folder(NULL);
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
    RUN(test_saved_data);
    RUN(test_volume_and_keys);
    RUN(test_pantryquest_example);
    RUN(test_pantryquest_shutter);
    RUN(test_pantryquest_world);
    RUN(test_pantryquest_bosses);
    RUN(test_pantryquest_plank_and_whisk);
    RUN(test_pantryquest_eight_ways);
    RUN(test_pantryquest_files_and_settings);
    RUN(test_check_and_errors);
    RUN(test_types_checked);
    RUN(test_types_at_run_time);
    RUN(test_language);
    RUN(test_entities_and_vars);
    RUN(test_runtime_errors);
    RUN(test_physics_callbacks_and_messages);
    RUN(test_spawn_destroy_input);
    RUN(test_tiles_substr_big_spawn);
    RUN(test_spawn_then_set);
    RUN(test_c_behaviour_and_reload);
    RUN(test_garbage);
}
