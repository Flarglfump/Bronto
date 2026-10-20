/* Tests: the file system module (fs.h) and path helpers. */
#include "test.h"
#include <string.h>

static void test_paths(void) {
    char buf[64];
    br_path_join(buf, sizeof(buf), "a/b", "c.bmp");   CHECK(strcmp(buf, "a/b/c.bmp") == 0);
    br_path_join(buf, sizeof(buf), "a/", "c");        CHECK(strcmp(buf, "a/c") == 0);
    br_path_join(buf, sizeof(buf), "", "c");          CHECK(strcmp(buf, "c") == 0);
    br_path_join(buf, sizeof(buf), "a", "/abs");      CHECK(strcmp(buf, "/abs") == 0);
    br_path_parent(buf, sizeof(buf), "a/b/c.bmp");    CHECK(strcmp(buf, "a/b") == 0);
    br_path_parent(buf, sizeof(buf), "c.bmp");        CHECK(strcmp(buf, "") == 0);
    br_path_parent(buf, sizeof(buf), "/c");           CHECK(strcmp(buf, "/") == 0);
    br_path_parent(buf, sizeof(buf), "a\\b");         CHECK(strcmp(buf, "a") == 0);
    CHECK(strcmp(br_path_name("a/b/hero.bmp"), "hero.bmp") == 0);
    CHECK(strcmp(br_path_ext("a/b/hero.BMP"), ".BMP") == 0);
    CHECK(strcmp(br_path_ext("a.b/noext"), "") == 0);
    CHECK(strcmp(br_path_ext(".hidden"), "") == 0);
    CHECK(br_path_has_ext("x/hero.BMP", ".bmp") && !br_path_has_ext("hero.bmpx", ".bmp"));
    br_path_stem(buf, sizeof(buf), "art/hero.tga");   CHECK(strcmp(buf, "hero") == 0);
}

static void test_fs_ops(void) {
    char root[BR_PATH_MAX], sub[BR_PATH_MAX], f1[BR_PATH_MAX], f2[BR_PATH_MAX], moved[BR_PATH_MAX];
    br_dir_list l;
    br_dir_entry e;
    br_path_join(root, sizeof(root), g_out_dir, "fs_test");
    br_path_join(sub, sizeof(sub), root, "Folder");
    br_path_join(f1, sizeof(f1), root, "b.txt");
    br_path_join(f2, sizeof(f2), root, "A.txt");
    br_path_join(moved, sizeof(moved), sub, "a2.txt");
    br_fs_remove(moved); br_fs_remove(f1); br_fs_remove(f2); br_fs_remove(sub);   /* a previous run */
    CHECK(br_fs_mkdir(root));
    CHECK(br_fs_mkdir(root));                                  /* already there: fine */
    CHECK(br_fs_mkdir(sub) && br_fs_is_dir(sub));
    CHECK(br_file_write_all(f1, "hello", 5));
    CHECK(br_file_write_all(f2, "x", 1));
    {
        char hidden[BR_PATH_MAX];
        br_path_join(hidden, sizeof(hidden), root, ".secret");
        CHECK(br_file_write_all(hidden, "", 0));
    }
    CHECK(br_fs_list(root, false, &l));
    CHECK_EQ_INT(l.count, 3);                                  /* .secret is hidden */
    if (l.count == 3) {
        CHECK(l.entries[0].is_dir && strcmp(l.entries[0].name, "Folder") == 0);   /* folders first */
        CHECK(strcmp(l.entries[1].name, "A.txt") == 0 && strcmp(l.entries[2].name, "b.txt") == 0);
        CHECK(l.entries[2].size == 5 && l.entries[2].modified > 1500000000);
    }
    br_dir_list_free(&l);
    CHECK(br_fs_list(root, true, &l) && l.count == 4);
    br_dir_list_free(&l);
    CHECK(br_fs_stat(f1, &e) && !e.is_dir && e.size == 5 && strcmp(e.name, "b.txt") == 0);
    CHECK(!br_fs_rename(f2, f1));                              /* never overwrites */
    CHECK(br_fs_rename(f2, moved) && !br_fs_exists(f2) && br_fs_exists(moved));
    CHECK(!br_fs_remove(sub));                                 /* not empty */
    CHECK(br_fs_remove(moved) && br_fs_remove(sub) && !br_fs_exists(sub));
    CHECK(br_fs_remove(f1));
    CHECK(!br_fs_list("no/such/folder", false, &l) && l.count == 0);
    CHECK(!br_fs_exists("no/such/file") && !br_fs_is_dir(f1));
    {
        char abs[BR_PATH_MAX], again[BR_PATH_MAX];
        CHECK(br_fs_absolute(root, abs, sizeof(abs)) && (abs[0] == '/' || abs[1] == ':'));
        CHECK(strcmp(br_path_name(abs), "fs_test") == 0);
        CHECK(br_fs_absolute(abs, again, sizeof(again)) && strcmp(abs, again) == 0);
        CHECK(br_fs_absolute("", abs, sizeof(abs)) && br_fs_is_dir(abs));
        CHECK(!br_fs_absolute("no/such/folder", abs, sizeof(abs)));
    }
}

void suite_fs(void) {
    RUN(test_paths);
    RUN(test_fs_ops);
}
