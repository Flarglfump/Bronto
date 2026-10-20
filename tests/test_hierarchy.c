/* Tests: the scene tree (hierarchy.h) and entity references in scene files. */
#include "test.h"
#include <string.h>

static br_world W;
static br_builtin_ids ID;
static br_component PARENT;

static void setup(void) {
    br_world_init(&W);
    br_builtin_register(&W, &ID);
    PARENT = br_component_find(&W, "Parent");
}

static br_entity node(const char *name, float x, float y) {
    br_entity e = br_entity_create(&W);
    snprintf(((br_name *)br_add(&W, e, ID.name, NULL))->text, 32, "%s", name);
    ((br_transform *)br_add(&W, e, ID.transform, NULL))->position = br_v2(x, y);
    return e;
}

static br_transform *xf(br_entity e) { return (br_transform *)br_get(&W, e, ID.transform); }

static void test_parenting(void) {
    br_entity a, b, c, kids[4];
    setup();
    a = node("A", 100, 50);
    b = node("B", 10, 0);
    c = node("C", 5, 5);
    CHECK(br_parent_of(&W, b) == BR_ENTITY_NULL);
    CHECK(br_set_parent(&W, b, a, false) && br_set_parent(&W, c, b, false));
    CHECK(br_parent_of(&W, c) == b && br_depth(&W, c) == 2 && br_is_ancestor(&W, a, c));
    CHECK(!br_set_parent(&W, a, c, false) && !br_set_parent(&W, a, a, false));   /* no loops */
    CHECK(br_children(&W, a, kids, 4) == 1 && kids[0] == b);
    CHECK(br_child_named(&W, b, "C") == c && br_child_named(&W, b, "X") == BR_ENTITY_NULL);
    CHECK(br_subtree(&W, a, kids, 4) == 3 && kids[0] == a && kids[1] == b && kids[2] == c);
    {   /* world = parent then local: rotate A by 90 degrees, scale it 2x */
        br_transform t;
        xf(a)->rotation = 90;
        xf(a)->scale = br_v2(2, 2);
        t = br_world_transform(&W, b);                     /* (10,0) * 2, turned 90 deg clockwise -> (0,20) */
        CHECK_NEAR(t.position.x, 100, 1e-3);
        CHECK_NEAR(t.position.y, 70, 1e-3);
        CHECK(t.rotation == 90 && t.scale.x == 2);
        t = br_world_transform(&W, c);                     /* B at (100,70); C's (5,5)*2 turned -> (-10,10) */
        CHECK_NEAR(t.position.x, 90, 1e-3);
        CHECK_NEAR(t.position.y, 80, 1e-3);
        br_set_world_position(&W, c, br_v2(200, 100));    /* and back again */
        t = br_world_transform(&W, c);
        CHECK_NEAR(t.position.x, 200, 1e-3);
        CHECK_NEAR(t.position.y, 100, 1e-3);
        br_set_world_rotation(&W, c, 30);
        CHECK_NEAR(xf(c)->rotation, -60, 1e-4);
        CHECK_NEAR(br_world_to_parent_vector(&W, c, br_v2(0, 4)).x, 2, 1e-4);   /* world down = B's +x, halved */
    }
    {   /* keep_world: unparenting C leaves it where it is */
        br_transform before = br_world_transform(&W, c), after;
        CHECK(br_set_parent(&W, c, BR_ENTITY_NULL, true));
        after = br_world_transform(&W, c);
        CHECK(br_parent_of(&W, c) == BR_ENTITY_NULL && !br_has(&W, c, PARENT));
        CHECK_NEAR(after.position.x, before.position.x, 1e-3);
        CHECK_NEAR(after.position.y, before.position.y, 1e-3);
        CHECK_NEAR(after.rotation, before.rotation, 1e-3);
        CHECK(br_set_parent(&W, c, a, true));
        after = br_world_transform(&W, c);
        CHECK_NEAR(after.position.x, before.position.x, 1e-3);
        CHECK_NEAR(after.scale.x, before.scale.x, 1e-4);
    }
    br_world_destroy(&W);
}

static void test_destroy_cascades(void) {
    br_entity a, b, c, d;
    setup();
    a = node("A", 0, 0);
    b = node("B", 0, 0);
    c = node("C", 0, 0);
    d = node("D", 0, 0);
    br_set_parent(&W, b, a, false);
    br_set_parent(&W, c, b, false);
    br_entity_destroy(&W, a);
    CHECK(!br_entity_alive(&W, a) && !br_entity_alive(&W, b) && !br_entity_alive(&W, c) && br_entity_alive(&W, d));
    a = node("A", 0, 0);
    br_set_parent(&W, d, a, false);
    br_entity_destroy_deferred(&W, a);                     /* the way scripts destroy */
    br_world_flush(&W);
    CHECK(!br_entity_alive(&W, d) && br_world_entity_count(&W) == 0);
    br_world_destroy(&W);
}

static void test_tree_in_scene_files(void) {
    br_entity a, b, c, made[8], order[3];
    char *text;
    int n;
    setup();
    a = node("Root", 10, 10);
    b = node("Child", 1, 2);
    c = node("Grandchild", 3, 4);
    br_set_parent(&W, b, a, false);
    br_set_parent(&W, c, b, false);
    order[0] = a; order[1] = b; order[2] = c;
    text = br_world_to_text(&W, order, 3, NULL);
    CHECK(text && strstr(text, "Parent\n    entity = 0\n") && strstr(text, "Parent\n    entity = 1\n"));
    br_world_clear(&W);
    n = br_world_from_text(&W, text, made, 8, NULL, 0);
    CHECK(n == 3 && br_parent_of(&W, made[1]) == made[0] && br_parent_of(&W, made[2]) == made[1]);
    CHECK_NEAR(br_world_transform(&W, made[2]).position.x, 14, 1e-4);
    n = br_world_from_text(&W, text, made + 3, 5, NULL, 0);  /* a second copy links to itself, not the first */
    CHECK(n == 3 && br_parent_of(&W, made[4]) == made[3] && br_parent_of(&W, made[5]) == made[4]);
    br_free(text);
    order[0] = made[1];                                     /* saving a child alone drops the link */
    text = br_world_to_text(&W, order, 1, NULL);
    CHECK(text && strstr(text, "entity = none"));
    br_free(text);
    CHECK(br_world_from_text(&W, "bronto_scene 1\nentity\n  Parent\n    entity = oops\n", NULL, 0, NULL, 0) == -1);
    n = br_world_from_text(&W, "bronto_scene 1\nentity\n  Parent\n    entity = 7\n", made, 1, NULL, 0);   /* out of range: none */
    CHECK(n == 1 && br_parent_of(&W, made[0]) == BR_ENTITY_NULL);
    {   /* a clone keeps the same parent */
        br_entity k = br_entity_clone(&W, made[4]);
        CHECK(br_parent_of(&W, k) == made[3]);
    }
    br_world_destroy(&W);
}

void suite_hierarchy(void) {
    RUN(test_parenting);
    RUN(test_destroy_cascades);
    RUN(test_tree_in_scene_files);
}
