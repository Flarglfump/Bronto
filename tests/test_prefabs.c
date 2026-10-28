/* Tests: prefab instances that follow their prefab (serialize.h). */
#include "test.h"
#include <string.h>

static br_world W;
static br_builtin_ids ID;

static void setup(void) {
    br_world_init(&W);
    br_builtin_register(&W, &ID);
    br_assets_set_root(g_out_dir);
}
static void teardown(void) {
    br_world_destroy(&W);
    br_assets_set_root("");
}

static void write(const char *name, const char *text) { CHECK(br_file_write_all(test_path(name), text, strlen(text))); }

static br_entity named(const char *name) {
    uint32_t i;
    for (i = 0; i < W.high_water; ++i) {
        br_entity e = (br_entity)(((uint32_t)W.generations[i] << BR_ENTITY_INDEX_BITS) | i);
        const br_name *n;
        if (!W.alive[i]) continue;
        n = (const br_name *)br_get(&W, e, ID.name);
        if (n && strcmp(n->text, name) == 0) return e;
    }
    return BR_ENTITY_NULL;
}
static int count_named(const char *name) {
    uint32_t i;
    int n = 0;
    for (i = 0; i < W.high_water; ++i) {
        br_entity e = (br_entity)(((uint32_t)W.generations[i] << BR_ENTITY_INDEX_BITS) | i);
        const br_name *nm;
        if (!W.alive[i]) continue;
        nm = (const br_name *)br_get(&W, e, ID.name);
        if (nm && strcmp(nm->text, name) == 0) ++n;
    }
    return n;
}
static br_color shape_color(br_entity e) { const br_shape *s = (const br_shape *)br_get(&W, e, ID.shape); return s ? s->color : br_rgba(0, 0, 0, 0); }

/* the prefab: a tree with a trunk and two lots of leaves */
static const char *k_tree_v1 =
    "bronto_scene 1\n"
    "entity\n  Name\n    text = \"Tree\"\n  Transform\n    position = 0 0\n  Shape\n    color = #00ff00ff\n"
    "entity\n  Name\n    text = \"Trunk\"\n  Transform\n    position = 0 10\n  Parent\n    entity = 0\n  Shape\n    color = #8b4513ff\n"
    "entity\n  Name\n    text = \"Leaves\"\n  Transform\n    position = -5 -5\n  Parent\n    entity = 0\n  Shape\n    color = #228822ff\n"
    "entity\n  Name\n    text = \"Leaves\"\n  Transform\n    position = 5 -5\n  Parent\n    entity = 0\n  Shape\n    color = #228822ff\n";

/* ... changed: leaves blue, a new apple */
static const char *k_tree_v2 =
    "bronto_scene 1\n"
    "entity\n  Name\n    text = \"Tree\"\n  Transform\n    position = 0 0\n  Shape\n    color = #00ff00ff\n"
    "entity\n  Name\n    text = \"Trunk\"\n  Transform\n    position = 0 10\n  Parent\n    entity = 0\n  Shape\n    color = #8b4513ff\n"
    "entity\n  Name\n    text = \"Leaves\"\n  Transform\n    position = -5 -5\n  Parent\n    entity = 0\n  Shape\n    color = #0000ffff\n"
    "entity\n  Name\n    text = \"Leaves\"\n  Transform\n    position = 5 -5\n  Parent\n    entity = 0\n  Shape\n    color = #0000ffff\n"
    "entity\n  Name\n    text = \"Apple\"\n  Transform\n    position = 0 -8\n  Parent\n    entity = 0\n";

static void test_prefab_instances(void) {
    br_entity *made = NULL, tree, leaves2, kids[8], bird, cam;
    char *text, *again;
    int n;
    setup();
    write("tree.scene", k_tree_v1);
    n = br_world_from_text_list(&W, "bronto_scene 1\nentity\n  Prefab\n    file = \"tree.scene\"\n  Transform\n    position = 100 50\n", &made, NULL, 0);
    CHECK_EQ_INT(n, 4);                                                   /* the instance: the prefab's four */
    tree = named("Tree");
    CHECK(tree != BR_ENTITY_NULL && made && made[0] == tree);
    CHECK(((br_transform *)br_get(&W, tree, ID.transform))->position.x == 100);   /* the scene's own position */
    CHECK(shape_color(tree).g == 255);                                    /* the prefab's colour */
    CHECK(br_children(&W, tree, kids, 8) == 3 && count_named("Leaves") == 2);
    br_free(made);
    /* change it here: the second leaves red, no trunk, a bird added, the tree's Shape taken off */
    {
        br_entity found[8];
        uint32_t k, c = br_children(&W, tree, found, 8);
        int seen = 0;
        leaves2 = BR_ENTITY_NULL;
        for (k = 0; k < c; ++k) {
            const br_name *nm = (const br_name *)br_get(&W, found[k], ID.name);
            if (strcmp(nm->text, "Leaves") == 0 && ++seen == 2) leaves2 = found[k];
        }
    }
    ((br_shape *)br_get(&W, leaves2, ID.shape))->color = br_rgb(255, 0, 0);
    br_entity_destroy(&W, named("Trunk"));
    bird = br_entity_create(&W);
    snprintf(((br_name *)br_add(&W, bird, ID.name, NULL))->text, 32, "Bird");
    br_add(&W, bird, ID.transform, NULL);
    br_set_parent(&W, bird, tree, false);
    br_remove(&W, tree, ID.shape);
    cam = br_entity_create(&W);                                           /* something pointing into the instance */
    ((br_camera *)br_add(&W, cam, ID.camera, NULL))->follow = leaves2;
    text = br_world_to_text(&W, NULL, 0, NULL);
    CHECK(text != NULL);
    if (text) {
        CHECK(strstr(text, "Prefab\n    file = \"tree.scene\"") != NULL);
        CHECK(strstr(text, "remove Shape") != NULL);                      /* the tree's Shape */
        CHECK(strstr(text, "path = \"Trunk\"\n  remove entity") != NULL);
        CHECK(strstr(text, "path = \"Leaves#2\"") != NULL && strstr(text, "#ff0000ff") != NULL);
        CHECK(strstr(text, "#228822ff") == NULL && strstr(text, "#8b4513ff") == NULL);   /* the prefab's own values aren't repeated */
        CHECK(strstr(text, "\"Bird\"") != NULL);
    }
    /* the prefab changes: every instance follows, except where it says otherwise */
    write("tree.scene", k_tree_v2);
    br_world_clear(&W);
    n = br_world_from_text_list(&W, text, &made, NULL, 0);
    CHECK(n == 6);                                                        /* tree, 2 leaves, apple, bird, camera */
    tree = named("Tree");
    CHECK(tree != BR_ENTITY_NULL && ((br_transform *)br_get(&W, tree, ID.transform))->position.x == 100);
    CHECK(!br_has(&W, tree, ID.shape));
    CHECK(named("Trunk") == BR_ENTITY_NULL && named("Apple") != BR_ENTITY_NULL && named("Bird") != BR_ENTITY_NULL);
    CHECK(br_parent_of(&W, named("Apple")) == tree && br_parent_of(&W, named("Bird")) == tree);
    {
        int blue = 0, red = 0;
        uint32_t k, c = br_children(&W, tree, kids, 8);
        for (k = 0; k < c; ++k) {
            br_color col = shape_color(kids[k]);
            if (col.b == 255 && col.r == 0) ++blue;
            if (col.r == 255 && col.b == 0) { ++red; leaves2 = kids[k]; }
        }
        CHECK(blue == 1 && red == 1);                                     /* the new blue, but this one still red */
    }
    cam = named("") ;
    {
        uint32_t i;
        br_entity follow = BR_ENTITY_NULL;
        for (i = 0; i < (uint32_t)n; ++i) if (br_has(&W, made[i], ID.camera)) follow = ((br_camera *)br_get(&W, made[i], ID.camera))->follow;
        CHECK(follow == leaves2);                                         /* the reference found the same part */
    }
    again = br_world_to_text(&W, made, (uint32_t)n, NULL);                /* and saved again: the same changes */
    CHECK(again && strstr(again, "path = \"Leaves#2\"") && strstr(again, "remove entity") && !strstr(again, "#0000ffff"));
    br_free(again);
    br_free(made);
    br_free(text);
    teardown();
}

static void test_nested_prefabs(void) {
    br_entity *made = NULL;
    int n;
    setup();
    write("tree.scene", k_tree_v1);
    write("forest.scene",                                                 /* a prefab of two trees (instances) */
        "bronto_scene 1\n"
        "entity\n  Name\n    text = \"Forest\"\n  Transform\n    position = 0 0\n"
        "entity\n  Prefab\n    file = \"tree.scene\"\n  Transform\n    position = -20 0\n  Parent\n    entity = 0\n"
        "entity\n  Prefab\n    file = \"tree.scene\"\n  Transform\n    position = 20 0\n  Parent\n    entity = 0\n");
    n = br_world_from_text_list(&W, "bronto_scene 1\nentity\n  Prefab\n    file = \"forest.scene\"\n", &made, NULL, 0);
    CHECK_EQ_INT(n, 9);                                                   /* forest + 2 x (tree, trunk, 2 leaves) */
    CHECK(count_named("Tree") == 2 && count_named("Leaves") == 4 && count_named("Forest") == 1);
    br_free(made);
    write("tree.scene", k_tree_v2);                                       /* the tree changes: both, inside the forest */
    br_world_clear(&W);
    n = br_world_from_text_list(&W, "bronto_scene 1\nentity\n  Prefab\n    file = \"forest.scene\"\n", &made, NULL, 0);
    CHECK(n == 11 && count_named("Apple") == 2);
    {   /* an instance of the forest, saved: nothing but the instance */
        char *text = br_world_to_text(&W, made, (uint32_t)n, NULL);
        CHECK(text && strstr(text, "forest.scene") && !strstr(text, "Leaves") && !strstr(text, "PrefabPart"));
        br_free(text);
    }
    br_free(made);
    /* a prefab inside itself: a warning, not a hang */
    write("loop.scene", "bronto_scene 1\nentity\n  Name\n    text = \"Loop\"\nentity\n  Prefab\n    file = \"loop.scene\"\n  Parent\n    entity = 0\n");
    br_world_clear(&W);
    n = br_world_from_text_list(&W, "bronto_scene 1\nentity\n  Prefab\n    file = \"loop.scene\"\n", &made, NULL, 0);
    CHECK(n > 0 && n < 40);
    br_free(made);
    /* a missing prefab: the instance keeps what the scene says */
    br_world_clear(&W);
    n = br_world_from_text_list(&W, "bronto_scene 1\nentity\n  Prefab\n    file = \"gone.scene\"\n  Name\n    text = \"Lost\"\n", &made, NULL, 0);
    CHECK(n == 1 && named("Lost") != BR_ENTITY_NULL);
    br_free(made);
    teardown();
}

void suite_prefabs(void) {
    RUN(test_prefab_instances);
    RUN(test_nested_prefabs);
}
