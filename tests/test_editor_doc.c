/* Tests: the editor's document model (tools/editor/doc.c) and console lines (console.c). */
#include "test.h"
#include "editor.h"
#include <string.h>

static ed_doc D;

static br_transform *xf(br_entity e) { return (br_transform *)br_get(&D.world, e, D.ids.transform); }

static void test_doc_create_order_delete(void) {
    br_entity a, b, c, d;
    char name[32];
    ed_doc_init(&D);
    CHECK(D.count == 0 && !ed_doc_dirty(&D));
    a = ed_doc_create(&D, "A", br_v2(10, 10));
    b = ed_doc_create(&D, "B", br_v2(50, 10));
    c = ed_doc_create(&D, "C", br_v2(90, 10));
    CHECK_EQ_INT(D.count, 3);
    CHECK(D.selected == c && ed_doc_index(&D, b) == 1);
    CHECK(strcmp(ed_doc_name(&D, a, name, sizeof(name)), "A") == 0);
    d = ed_doc_duplicate(&D, a);                          /* goes right after A */
    CHECK_EQ_INT(ed_doc_index(&D, d), 1);
    CHECK(strcmp(ed_doc_name(&D, d, name, sizeof(name)), "A copy") == 0);
    CHECK(xf(d)->position.x == 18 && xf(a)->position.x == 10);
    ed_doc_reorder(&D, c, -1);
    CHECK(ed_doc_index(&D, c) == 2 && ed_doc_index(&D, b) == 3);
    ed_doc_reorder(&D, a, -1);                            /* already first: no change */
    {
        int before[3], k;
        for (k = 0; k < 3; ++k) before[k] = (int)D.order[k];
        ed_doc_move(&D, D.order[0], 3);                   /* first to the end */
        CHECK((int)D.order[2] == before[0] && (int)D.order[0] == before[1]);
        ed_doc_move(&D, D.order[2], 0);                   /* and back to the front */
        for (k = 0; k < 3; ++k) CHECK((int)D.order[k] == before[k]);
        ed_doc_move(&D, D.order[1], 2);                   /* just below itself: no change */
        for (k = 0; k < 3; ++k) CHECK((int)D.order[k] == before[k]);
    }
    CHECK_EQ_INT(ed_doc_index(&D, a), 0);
    D.selected = c;
    ed_doc_delete(&D, c);
    CHECK(D.count == 3 && !br_entity_alive(&D.world, c));
    CHECK(D.selected == b);                               /* the next one */
    ed_doc_destroy(&D);
}

static void test_doc_pick(void) {
    br_entity low, high;
    ed_doc_init(&D);
    low = ed_doc_create(&D, "low", br_v2(100, 100));
    high = ed_doc_create(&D, "high", br_v2(105, 100));
    ((br_shape *)br_add(&D.world, low, D.ids.shape, NULL))->layer = 5;
    br_add(&D.world, high, D.ids.shape, NULL);
    CHECK(ed_doc_pick(&D, br_v2(103, 100)) == low);       /* higher layer wins */
    CHECK(ed_doc_pick(&D, br_v2(112, 100)) == high);
    CHECK(ed_doc_pick(&D, br_v2(300, 300)) == BR_ENTITY_NULL);
    ed_doc_destroy(&D);
}

static void test_doc_undo_redo(void) {
    br_entity e;
    ed_doc_init(&D);
    e = ed_doc_create(&D, "Box", br_v2(0, 0));
    ed_doc_checkpoint(&D);
    CHECK(D.undo_count == 1 && ed_doc_dirty(&D));
    xf(e)->position = br_v2(40, 0);
    ed_doc_checkpoint(&D);
    ed_doc_checkpoint(&D);                                /* nothing changed: no new step */
    CHECK_EQ_INT(D.undo_count, 2);
    CHECK(ed_doc_undo(&D));
    CHECK_EQ_INT(D.count, 1);
    CHECK(xf(D.order[0])->position.x == 0);
    CHECK(D.selected == D.order[0]);                      /* selection survives */
    CHECK(ed_doc_undo(&D));
    CHECK(D.count == 0 && !ed_doc_dirty(&D));             /* back to the start */
    CHECK(!ed_doc_undo(&D));
    CHECK(ed_doc_redo(&D) && ed_doc_redo(&D));
    CHECK(D.count == 1 && xf(D.order[0])->position.x == 40);
    CHECK(!ed_doc_redo(&D));
    ed_doc_undo(&D);
    ed_doc_create(&D, "New", br_v2(0, 0));                /* a new edit drops the redo steps */
    ed_doc_checkpoint(&D);
    CHECK_EQ_INT(D.redo_count, 0);
    ed_doc_destroy(&D);
}

static void test_doc_play_stop(void) {
    br_entity e;
    ed_doc_init(&D);
    e = ed_doc_create(&D, "Ball", br_v2(10, 20));
    ((br_motion *)br_add(&D.world, e, D.ids.motion, NULL))->velocity = br_v2(60, 0);
    ed_doc_checkpoint(&D);
    ed_doc_play(&D);
    CHECK(D.playing);
    ed_doc_step(&D, 0.5f);
    CHECK_NEAR(xf(D.order[0])->position.x, 40, 1e-4);
    ed_doc_checkpoint(&D);                                /* no undo steps while playing */
    CHECK(!ed_doc_undo(&D));
    ed_doc_stop(&D);
    CHECK(!D.playing && D.count == 1);
    CHECK_NEAR(xf(D.order[0])->position.x, 10, 1e-6);     /* exactly as before */
    CHECK(D.selected == D.order[0]);
    ed_doc_step(&D, 0.5f);                                /* stopped: nothing moves */
    CHECK_NEAR(xf(D.order[0])->position.x, 10, 1e-6);
    ed_doc_destroy(&D);
}

static void test_doc_save_load(void) {
    const char *path = test_path("editor_doc_test.scene");
    br_entity e;
    ed_doc_init(&D);
    e = ed_doc_create(&D, "Hero", br_v2(3, 4));
    br_add(&D.world, e, D.ids.shape, NULL);
    ed_doc_create(&D, "Sign", br_v2(5, 6));
    CHECK(ed_doc_save(&D, path));
    CHECK(!ed_doc_dirty(&D) && strcmp(D.path, path) == 0);
    xf(e)->position.x = 99;
    ed_doc_checkpoint(&D);
    CHECK(ed_doc_dirty(&D));
    CHECK(ed_doc_load(&D, path));
    CHECK(D.count == 2 && !ed_doc_dirty(&D) && D.undo_count == 0);
    CHECK(xf(D.order[0])->position.x == 3);
    CHECK(br_has(&D.world, D.order[0], D.ids.shape) && !br_has(&D.world, D.order[1], D.ids.shape));
    /* a broken file is refused and the open scene is kept */
    {
        const char *broken = "bronto_scene 1\nentity\n  Transform\n    position = oops\n";
        CHECK(br_file_write_all(test_path("broken.scene"), broken, strlen(broken)));
    }
    CHECK(!ed_doc_load(&D, test_path("broken.scene")));
    CHECK(strstr(D.error, "Transform.position") != NULL);
    CHECK(D.count == 2 && strcmp(D.path, path) == 0);
    CHECK(!ed_doc_load(&D, test_path("missing.scene")));
    ed_doc_new(&D);
    CHECK(D.count == 0 && D.path[0] == 0);
    br_assets_set_root("");
    ed_doc_destroy(&D);
}

static void test_doc_prefabs(void) {
    const char *prefab = test_path("prefab_pair.scene");
    br_entity a, b;
    int n;
    ed_doc_init(&D);
    a = ed_doc_create(&D, "Left", br_v2(10, 20));
    br_add(&D.world, a, D.ids.shape, NULL);
    CHECK(ed_doc_save_entity(&D, a, prefab));
    CHECK_EQ_INT(ed_doc_count_entities(prefab), 1);
    CHECK_EQ_INT(ed_doc_count_entities(test_path("missing.scene")), -1);
    CHECK(!ed_doc_save_entity(&D, BR_ENTITY_NULL, prefab));
    b = ed_doc_create(&D, "Right", br_v2(30, 40));
    {
        br_entity both[2];
        both[0] = a; both[1] = b;
        CHECK(br_world_save_file(&D.world, both, 2, prefab));   /* a two-entity prefab */
    }
    ed_doc_checkpoint(&D);
    n = ed_doc_instantiate(&D, prefab, br_v2(100, 100));
    CHECK_EQ_INT(n, 2);
    CHECK(D.count == 4 && D.selected == D.order[2]);
    CHECK(xf(D.order[2])->position.x == 90 && xf(D.order[2])->position.y == 90);    /* average at 100,100 */
    CHECK(xf(D.order[3])->position.x == 110 && xf(D.order[3])->position.y == 110);
    CHECK(br_has(&D.world, D.order[2], D.ids.shape));
    ed_doc_checkpoint(&D);
    CHECK(ed_doc_undo(&D) && D.count == 2);                      /* one undo step */
    CHECK_EQ_INT(ed_doc_instantiate(&D, test_path("missing.scene"), br_v2(0, 0)), -1);
    CHECK(D.count == 2 && D.error[0]);
    ed_doc_destroy(&D);
}

static void test_doc_project_root(void) {
    char dir[BR_PATH_MAX], sub[BR_PATH_MAX], scene[BR_PATH_MAX], marker[BR_PATH_MAX], found[BR_PATH_MAX];
    br_path_join(dir, sizeof(dir), g_out_dir, "proj_test");
    br_path_join(sub, sizeof(sub), dir, "levels");
    br_path_join(scene, sizeof(scene), sub, "one.scene");
    br_path_join(marker, sizeof(marker), dir, BR_PROJECT_FILE);
    br_fs_remove(marker);
    CHECK(br_fs_mkdir(dir) && br_fs_mkdir(sub));
    br_assets_find_root(scene, found, sizeof(found));
    CHECK(strcmp(found, sub) == 0);                              /* no project file: the scene's folder */
    CHECK(br_file_write_all(marker, "bronto_project 1\n", 17));
    br_assets_find_root(scene, found, sizeof(found));
    CHECK(strcmp(found, dir) == 0);                              /* the marked folder above */
    br_assets_find_root("plain.scene", found, sizeof(found));
    CHECK(found[0] == 0);
    ed_doc_init(&D);
    ed_doc_set_root(&D, dir);
    CHECK(ed_doc_save(&D, scene));
    CHECK(strcmp(br_assets_root(), dir) != 0 && strncmp(br_assets_root(), dir, strlen(dir)) == 0);  /* "dir/" */
    ed_doc_destroy(&D);
    br_fs_remove(scene);
    br_fs_remove(marker);
    br_assets_set_root("");
}

static void test_doc_tree(void) {
    br_entity a, b, c, kid, copy;
    const char *prefab = test_path("tree_prefab.scene");
    char name[64];
    ed_doc_init(&D);
    a = ed_doc_create(&D, "A", br_v2(100, 0));
    b = ed_doc_create(&D, "B", br_v2(0, 0));
    c = ed_doc_create(&D, "C", br_v2(50, 50));
    CHECK(ed_doc_place(&D, c, a, BR_ENTITY_NULL));          /* C under A, stays put on screen */
    CHECK(D.order[0] == a && D.order[1] == c && D.order[2] == b);
    CHECK(xf(c)->position.x == -50 && br_world_transform(&D.world, c).position.x == 50);
    kid = ed_doc_create_child(&D, c, "Kid", br_v2(1, 2));   /* local to C */
    CHECK(D.order[2] == kid && br_world_transform(&D.world, kid).position.x == 51);
    CHECK(!ed_doc_place(&D, a, kid, BR_ENTITY_NULL));       /* no loops */
    CHECK(ed_doc_place(&D, b, a, c));                       /* B under A, before C */
    CHECK(D.order[1] == b && D.order[2] == c && D.order[3] == kid);
    ed_doc_reorder(&D, b, 1);                               /* swap with C (and C's kid) */
    CHECK(D.order[1] == c && D.order[2] == kid && D.order[3] == b);
    copy = ed_doc_duplicate(&D, c);                         /* C and Kid, after C's subtree */
    CHECK(D.count == 6 && D.order[3] == copy && br_parent_of(&D.world, copy) == a);
    CHECK(br_parent_of(&D.world, D.order[4]) == copy && strcmp(ed_doc_name(&D, D.order[4], name, sizeof(name)), "Kid") == 0);
    ed_doc_checkpoint(&D);
    ed_doc_delete(&D, a);                                   /* the whole tree goes */
    CHECK(D.count == 0 && br_world_entity_count(&D.world) == 0);
    ed_doc_checkpoint(&D);
    CHECK(ed_doc_undo(&D) && D.count == 6);                 /* and comes back, still a tree */
    a = D.order[0];
    CHECK(br_parent_of(&D.world, D.order[1]) == a && br_parent_of(&D.world, D.order[2]) == D.order[1]);
    c = D.order[1];
    CHECK(ed_doc_save_entity(&D, c, prefab));               /* a subtree prefab, saved as a root where it is */
    CHECK(ed_doc_count_entities(prefab) == 2);
    CHECK(br_parent_of(&D.world, c) == a && xf(c)->position.x == -50);   /* untouched */
    CHECK(ed_doc_instantiate(&D, prefab, br_v2(300, 300)) == 2);
    CHECK(D.count == 8 && br_parent_of(&D.world, D.order[7]) == D.order[6]);
    CHECK(br_world_transform(&D.world, D.order[6]).position.x == 300);
    CHECK(br_world_transform(&D.world, D.order[7]).position.x == 301);
    ed_doc_destroy(&D);
}


/* Copy and paste: a subtree as text, back at a point or under a parent. */
static void test_doc_copy_paste(void) {
    br_entity a, kid, b;
    char *text, name[64];
    int n;
    ed_doc_init(&D);
    a = ed_doc_create(&D, "A", br_v2(100, 50));
    kid = ed_doc_create_child(&D, a, "Kid", br_v2(10, 0));
    b = ed_doc_create(&D, "B", br_v2(0, 0));
    text = ed_doc_copy(&D, kid);                              /* a child: copied as a root, where it is */
    CHECK(text && strstr(text, "Kid") && !strstr(text, "\"A\""));
    n = ed_doc_paste(&D, text, NULL, BR_ENTITY_NULL);
    CHECK_EQ_INT(n, 1);
    CHECK(br_parent_of(&D.world, D.selected) == BR_ENTITY_NULL);
    CHECK_NEAR(br_world_transform(&D.world, D.selected).position.x, 110, 1e-3);
    br_free(text);
    text = ed_doc_copy(&D, a);                                /* with its subtree */
    n = ed_doc_paste(&D, text, &(br_vec2){ 0, 200 }, b);      /* at a point, under B */
    CHECK_EQ_INT(n, 2);
    CHECK(br_parent_of(&D.world, D.selected) == b && strcmp(ed_doc_name(&D, D.selected, name, sizeof(name)), "A") == 0);
    CHECK_NEAR(br_world_transform(&D.world, D.selected).position.y, 200, 1e-3);
    CHECK_EQ_INT(D.count, 6);
    CHECK(br_world_transform(&D.world, D.order[ed_doc_index(&D, D.selected) + 1]).position.x - br_world_transform(&D.world, D.selected).position.x > 9.9f);
    CHECK(ed_doc_paste(&D, "this is not a scene", NULL, BR_ENTITY_NULL) < 0 && D.error[0]);
    br_free(text);
    ed_doc_destroy(&D);
}

/* The console's lines (console.c). */
static ed_console CON;

static void test_console_lines(void) {
    const ed_con_line *l;
    char file[64];
    int line = 0, i;
    memset(&CON, 0, sizeof(CON));
    ed_console_feed(&CON, BR_STREAM_STDOUT, "hel", 3, 0);
    CHECK_EQ_INT(CON.count, 0);                               /* not a whole line yet */
    ed_console_feed(&CON, BR_STREAM_STDOUT, "lo\nwor", 6, 0.1);
    CHECK_EQ_INT(CON.count, 1);
    CHECK(strcmp(ed_console_line(&CON, 0)->text, "hello") == 0 && ed_console_line(&CON, 0)->kind == ED_CON_OUT);
    ed_console_flush(&CON, 0.2, 0.25);
    CHECK_EQ_INT(CON.count, 1);                               /* "wor" waits a little... */
    ed_console_flush(&CON, 0.5, 0.25);
    CHECK(CON.count == 2 && strcmp(ed_console_line(&CON, 1)->text, "wor") == 0);   /* ...then shows */

    ed_console_feed(&CON, BR_STREAM_STDERR, "[ERROR] script.c:960: script: scripts/player.bs:12: oops\r\n", 58, 1);
    l = ed_console_line(&CON, 2);
    CHECK(l && l->kind == ED_CON_ERROR && l->level == ED_LEVEL_ERROR && strcmp(l->text, "scripts/player.bs:12: oops") == 0);
    CHECK(l && ed_console_location(l->text, file, sizeof(file), &line) && strcmp(file, "scripts/player.bs") == 0 && line == 12);
    CHECK(ed_console_location("error in (enemy.bs:3) here", file, sizeof(file), &line) && strcmp(file, "enemy.bs") == 0 && line == 3);
    CHECK(!ed_console_location("no.bsx:3 or a.bs: here", file, sizeof(file), &line));
    ed_console_feed(&CON, BR_STREAM_STDERR, "[INFO ] engine.c:1: ready\n", 26, 1);
    CHECK(ed_console_line(&CON, 3)->kind == ED_CON_INFO && strcmp(ed_console_line(&CON, 3)->text, "ready") == 0);
    ed_console_add(&CON, ED_CON_ERR, "the device failed to open", 1);
    ed_console_add(&CON, ED_CON_ERR, "Warning: slow disk\nplain stderr", 1);
    CHECK(ed_console_line(&CON, 4)->level == ED_LEVEL_ERROR && ed_console_line(&CON, 5)->level == ED_LEVEL_WARN &&
          ed_console_line(&CON, 6)->level == ED_LEVEL_LOG);
    for (i = 0; i < 3; ++i) ed_console_add(&CON, ED_CON_PRINT, "tick", 2);   /* the same again: counted */
    CHECK(CON.count == 8 && ed_console_line(&CON, 7)->repeat == 3);
    CHECK_EQ_INT(CON.level_count[ED_LEVEL_ERROR], 2);
    CHECK_EQ_INT(CON.level_count[ED_LEVEL_LOG], 2 + 1 + 1 + 3);    /* hello, wor, ready, plain, tick x3 */
    for (i = 0; i < ED_CONSOLE_LINES + 5; ++i) {             /* full: the oldest go */
        char t[32];
        snprintf(t, sizeof(t), "line %d", i);
        ed_console_add(&CON, ED_CON_OUT, t, 3);
    }
    CHECK_EQ_INT(CON.count, ED_CONSOLE_LINES);
    CHECK(strcmp(ed_console_line(&CON, 0)->text, "line 5") == 0);
    CHECK_EQ_INT(CON.level_count[ED_LEVEL_ERROR] + CON.level_count[ED_LEVEL_WARN] + CON.level_count[ED_LEVEL_LOG], ED_CONSOLE_LINES);
    {
        char longer[600];                                     /* a very long line is broken up */
        for (i = 0; i < (int)sizeof(longer) - 1; ++i) longer[i] = (char)('a' + i % 26);
        longer[sizeof(longer) - 1] = '\n';
        ed_console_clear(&CON);
        ed_console_feed(&CON, BR_STREAM_STDOUT, longer, sizeof(longer), 4);
        CHECK(CON.count == 4 && strlen(ed_console_line(&CON, 0)->text) == ED_CONSOLE_TEXT - 1);
    }
    ed_console_clear(&CON);
    CHECK(CON.count == 0 && CON.level_count[0] == 0 && ed_console_line(&CON, 0) == NULL);
}

void suite_editor_doc(void) {
    RUN(test_doc_tree);
    RUN(test_doc_copy_paste);
    RUN(test_console_lines);
    RUN(test_doc_prefabs);
    RUN(test_doc_project_root);
    RUN(test_doc_create_order_delete);
    RUN(test_doc_pick);
    RUN(test_doc_undo_redo);
    RUN(test_doc_play_stop);
    RUN(test_doc_save_load);
}
