/* Tests: component descriptions and scene files (serialize.h). */
#include "test.h"
#include "bronto/serialize.h"
#include <string.h>

typedef struct Thing {
    int hp;
    float speed;
    bool shiny;
    br_vec2 pos;
    br_color tint;
    char label[16];
    int mood;
} Thing;

static const char *const k_moods[] = { "calm", "happy", "grumpy" };
static const br_field k_thing_fields[] = {
    BR_FIELD(Thing, hp, BR_FIELD_INT, 0, 100),
    BR_FIELD(Thing, speed, BR_FIELD_FLOAT, 0, 0),
    BR_FIELD(Thing, shiny, BR_FIELD_BOOL, 0, 0),
    BR_FIELD(Thing, pos, BR_FIELD_VEC2, 0, 0),
    BR_FIELD(Thing, tint, BR_FIELD_COLOR, 0, 0),
    BR_FIELD(Thing, label, BR_FIELD_STRING, 0, 0),
    BR_FIELD_ENUM_OF(Thing, mood, k_moods),
};
static const Thing k_thing_default = { 10, 1.5f, false, { 0, 0 }, { 255, 255, 255, 255 }, "thing", 1 };
static const br_component_desc k_thing = BR_COMPONENT_DESC(Thing, k_thing_fields, &k_thing_default);

typedef struct Marker { int unused; } Marker;      /* registered without a description */

static void make_world(br_world *w, br_component *thing, br_component *marker) {
    br_world_init(w);
    *thing = br_component_register_desc(w, &k_thing);
    *marker = BR_COMPONENT(w, Marker);
}

static void test_desc_and_defaults(void) {
    br_world w;
    br_component T, M;
    br_entity e;
    Thing *t;
    make_world(&w, &T, &M);
    CHECK(br_component_get_desc(&w, T) == &k_thing);
    CHECK(br_component_get_desc(&w, M) == NULL);
    CHECK(strcmp(br_component_name(&w, T), "Thing") == 0);
    CHECK_EQ_INT(k_thing.field_count, 7);
    CHECK_EQ_INT(k_thing_fields[6].enum_count, 3);
    e = br_entity_create(&w);
    t = (Thing *)br_add(&w, e, T, NULL);                 /* NULL init: the defaults */
    CHECK(t->hp == 10 && t->mood == 1 && strcmp(t->label, "thing") == 0 && t->tint.a == 255);
    br_world_destroy(&w);
}

static void test_field_format_parse(void) {
    Thing t = k_thing_default;
    char buf[128];
    br_field_format(&k_thing_fields[4], &t, buf, sizeof(buf));
    CHECK(strcmp(buf, "#ffffffff") == 0);
    CHECK(br_field_parse(&k_thing_fields[4], &t, "#10203040"));
    CHECK(t.tint.r == 0x10 && t.tint.g == 0x20 && t.tint.b == 0x30 && t.tint.a == 0x40);
    CHECK(br_field_parse(&k_thing_fields[4], &t, "#abcdef") && t.tint.a == 255);
    CHECK(br_field_parse(&k_thing_fields[4], &t, "1 2 3 4") && t.tint.r == 1 && t.tint.a == 4);
    CHECK(!br_field_parse(&k_thing_fields[4], &t, "#12345"));
    CHECK(!br_field_parse(&k_thing_fields[4], &t, "red"));

    CHECK(br_field_parse(&k_thing_fields[5], &t, "\"say \\\"hi\\\"\""));
    CHECK(strcmp(t.label, "say \"hi\"") == 0);
    br_field_format(&k_thing_fields[5], &t, buf, sizeof(buf));
    CHECK(strcmp(buf, "\"say \\\"hi\\\"\"") == 0);
    CHECK(br_field_parse(&k_thing_fields[5], &t, "\"this is much longer than sixteen\""));
    CHECK(strlen(t.label) == 15);                        /* truncated to fit */
    CHECK(!br_field_parse(&k_thing_fields[5], &t, "no quotes"));

    CHECK(br_field_parse(&k_thing_fields[6], &t, "grumpy") && t.mood == 2);
    CHECK(br_field_parse(&k_thing_fields[6], &t, "0") && t.mood == 0);
    CHECK(!br_field_parse(&k_thing_fields[6], &t, "sleepy"));
    CHECK(br_field_parse(&k_thing_fields[2], &t, "true") && t.shiny);
    CHECK(!br_field_parse(&k_thing_fields[2], &t, "yes"));
    CHECK(br_field_parse(&k_thing_fields[3], &t, " -1.5 2e3 ") && t.pos.x == -1.5f && t.pos.y == 2000.0f);
    CHECK(!br_field_parse(&k_thing_fields[3], &t, "1"));
    CHECK(!br_field_parse(&k_thing_fields[0], &t, "12abc"));
    CHECK(!br_field_parse(&k_thing_fields[1], &t, "nan"));
    t.speed = 0.1f;                                      /* floats round-trip exactly */
    br_field_format(&k_thing_fields[1], &t, buf, sizeof(buf));
    t.speed = 0;
    CHECK(br_field_parse(&k_thing_fields[1], &t, buf) && t.speed == 0.1f);
}

static void test_round_trip(void) {
    br_world w, w2;
    br_component T, M, T2, M2;
    br_entity e[3], loaded[8];
    char *text, *text2, err[128];
    int n;
    make_world(&w, &T, &M);
    e[0] = br_entity_create(&w);
    e[1] = br_entity_create(&w);
    e[2] = br_entity_create(&w);
    {
        Thing *t = (Thing *)br_add(&w, e[0], T, NULL);
        t->hp = 42; t->speed = 3.25f; t->shiny = true; t->pos = br_v2(1.5f, -7);
        t->tint = br_rgba(1, 2, 3, 4); strcpy(t->label, "a \"b\"\\c"); t->mood = 2;
    }
    br_add(&w, e[0], M, NULL);                            /* not described: not saved */
    br_add(&w, e[2], T, NULL);
    /* entity 1 has nothing: still saved, as an empty entity */
    {
        br_entity order[3];
        order[0] = e[2]; order[1] = e[0]; order[2] = e[1];
        text = br_world_to_text(&w, order, 3, NULL);
    }
    CHECK(text != NULL && strstr(text, "bronto_scene 1") != NULL);
    CHECK(strstr(text, "Marker") == NULL);
    CHECK(strstr(text, "mood = grumpy") != NULL);

    make_world(&w2, &T2, &M2);
    n = br_world_from_text(&w2, text, loaded, 8, err, sizeof(err));
    CHECK_EQ_INT(n, 3);
    if (n == 3) {
        const Thing *a = (const Thing *)br_get(&w2, loaded[1], T2);   /* file order kept */
        CHECK(br_get(&w2, loaded[0], T2) != NULL && br_get(&w2, loaded[2], T2) == NULL);
        CHECK(a && a->hp == 42 && a->speed == 3.25f && a->shiny && a->pos.y == -7 && a->tint.b == 3);
        CHECK(a && strcmp(a->label, "a \"b\"\\c") == 0 && a->mood == 2);
        CHECK(!br_has(&w2, loaded[1], M2));
        text2 = br_world_to_text(&w2, loaded, 3, NULL);
        CHECK(text2 && strcmp(text, text2) == 0);         /* save -> load -> save is stable */
        br_free(text2);
    }
    br_free(text);
    br_world_destroy(&w);
    br_world_destroy(&w2);
}

static void test_forgiving_and_errors(void) {
    br_world w;
    br_component T, M;
    br_entity out[4];
    char err[160];
    int n;
    make_world(&w, &T, &M);
    QUIET(n = br_world_from_text(&w,
        "# made by hand\n"
        "bronto_scene 1\n"
        "entity\n"
        "  Unknown\n"
        "    whatever = 12\n"
        "  Thing\n"
        "    hp = 7\n"
        "    colour = #ff0000ff\n"        /* unknown field: ignored */
        "entity\n", out, 4, err, sizeof(err)));
    CHECK_EQ_INT(n, 2);
    if (n == 2) {
        const Thing *t = (const Thing *)br_get(&w, out[0], T);
        CHECK(t && t->hp == 7 && t->speed == 1.5f);       /* missing fields keep their defaults */
    }
    CHECK_EQ_INT(br_world_entity_count(&w), 2);

    n = br_world_from_text(&w, "bronto_scene 1\nentity\n  Thing\n    hp = lots\nentity\n", out, 4, err, sizeof(err));
    CHECK_EQ_INT(n, -1);
    CHECK(strstr(err, "line 4") != NULL && strstr(err, "Thing.hp") != NULL);
    CHECK_EQ_INT(br_world_entity_count(&w), 2);           /* nothing was added */
    CHECK_EQ_INT(br_world_from_text(&w, "entity\n", out, 4, err, sizeof(err)), -1);
    CHECK(strstr(err, "not a Bronto scene") != NULL);
    CHECK_EQ_INT(br_world_from_text(&w, "bronto_scene 99\n", out, 4, err, sizeof(err)), -1);
    CHECK_EQ_INT(br_world_from_text(&w, "bronto_scene 1\nThing\n", out, 4, err, sizeof(err)), -1);
    CHECK_EQ_INT(br_world_from_text(&w, "bronto_scene 1\nentity\n  hp = 3\n", out, 4, err, sizeof(err)), -1);
    CHECK_EQ_INT(br_world_from_text(&w, "bronto_scene 1\n", out, 4, err, sizeof(err)), 0);
    br_world_destroy(&w);
}

static void test_files_and_clone(void) {
    br_world w;
    br_component T, M;
    br_entity e, c, out[4];
    const char *path = test_path("scene_test.scene");
    char err[128];
    make_world(&w, &T, &M);
    e = br_entity_create(&w);
    ((Thing *)br_add(&w, e, T, NULL))->hp = 99;
    br_add(&w, e, M, NULL);
    c = br_entity_clone(&w, e);
    CHECK(c != BR_ENTITY_NULL && c != e);
    CHECK(br_has(&w, c, M) && ((Thing *)br_get(&w, c, T))->hp == 99);
    ((Thing *)br_get(&w, c, T))->hp = 1;
    CHECK(((Thing *)br_get(&w, e, T))->hp == 99);         /* a real copy */
    CHECK(br_world_save_file(&w, NULL, 0, path));
    br_world_clear(&w);
    CHECK_EQ_INT(br_world_load_file(&w, path, out, 4, err, sizeof(err)), 2);
    CHECK(((Thing *)br_get(&w, out[0], T))->hp == 99 && ((Thing *)br_get(&w, out[1], T))->hp == 1);
    CHECK_EQ_INT(br_world_load_file(&w, test_path("no_such.scene"), out, 4, err, sizeof(err)), -1);
    br_world_destroy(&w);
}

void suite_serialize(void) {
    RUN(test_desc_and_defaults);
    RUN(test_field_format_parse);
    RUN(test_round_trip);
    RUN(test_forgiving_and_errors);
    RUN(test_files_and_clone);
}
