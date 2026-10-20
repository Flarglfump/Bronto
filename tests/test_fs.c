/* Tests: the file system module (fs.h) and path helpers. */
#include "test.h"
#include <string.h>
#include <time.h>

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
        CHECK(br_fs_config_dir("bronto-test", abs, sizeof(abs)) && br_fs_is_dir(abs) && strcmp(br_path_name(abs), "bronto-test") == 0);
        br_fs_remove(abs);
    }
}


/* stdout / stderr capture (the editor's console) */
static void test_capture(void) {
    char got[256], buf[64];
    size_t n = 0, k;
    br_stream st;
    bool saw_err = false;
    clock_t start;
#if defined(__EMSCRIPTEN__)
    CHECK(!br_capture_start(false));
    return;
#endif
    CHECK(!br_capture_active());
    if (!br_capture_start(false)) { CHECK(br_capture_active()); return; }
    printf("captured line\n");
    fprintf(stderr, "to stderr\n");
    start = clock();
    while ((n < 14 || !saw_err) && (double)(clock() - start) / CLOCKS_PER_SEC < 2.0) {   /* the reader thread is quick, but async */
        while ((k = br_capture_read(&st, buf, 5)) > 0) {              /* small reads split records */
            if (st == BR_STREAM_STDERR) saw_err = true;
            if (st == BR_STREAM_STDOUT && n + k < sizeof(got)) { memcpy(got + n, buf, k); n += k; }
        }
    }
    got[n] = 0;
    br_capture_stop();
    CHECK(!br_capture_active());
    CHECK(strcmp(got, "captured line\n") == 0);
    CHECK(saw_err);
    CHECK(br_capture_dropped() == 0);
    CHECK(br_capture_read(&st, buf, sizeof(buf)) == 0);          /* stopped */
}


static bool skip_folder(const char *rel, bool is_dir, void *user) { BR_UNUSED(user); return !(is_dir && strcmp(rel, "skip") == 0); }

/* Packs: a folder in one file, or added to the end of another (an executable). */
static void test_pack(void) {
    char root[BR_PATH_MAX], sub[BR_PATH_MAX], skip[BR_PATH_MAX], f[BR_PATH_MAX], pak[BR_PATH_MAX], exe[BR_PATH_MAX], err[200];
    unsigned char bin[300];
    br_pack_stats st;
    size_t size, i;
    char *text;
    br_path_join(root, sizeof(root), g_out_dir, "pack_src");
    br_path_join(sub, sizeof(sub), root, "art");
    br_path_join(skip, sizeof(skip), root, "skip");
    br_fs_mkdir(root); br_fs_mkdir(sub); br_fs_mkdir(skip);
    br_path_join(f, sizeof(f), root, "main.scene"); CHECK(br_file_write_all(f, "bronto_scene 1\n", 15));
    for (i = 0; i < sizeof(bin); ++i) bin[i] = (unsigned char)(i * 7);
    br_path_join(f, sizeof(f), sub, "hero.bin"); CHECK(br_file_write_all(f, bin, sizeof(bin)));
    br_path_join(f, sizeof(f), sub, "empty.txt"); CHECK(br_file_write_all(f, "", 0));
    br_path_join(f, sizeof(f), skip, "nope.txt"); CHECK(br_file_write_all(f, "x", 1));
    br_path_join(f, sizeof(f), root, ".hidden"); CHECK(br_file_write_all(f, "x", 1));
    br_path_join(pak, sizeof(pak), g_out_dir, "test.brpak");
    CHECK(br_pack_create(root, pak, false, skip_folder, NULL, &st, err, sizeof(err)));
    CHECK_EQ_INT(st.files, 3);
    CHECK(st.bytes == 15 + sizeof(bin));
    CHECK(!br_pack_contains("main.scene") && !br_pack_mounted());
    CHECK(br_pack_mount(pak) && br_pack_mounted() && br_pack_file_count() == 3);
    CHECK(br_pack_contains("art/hero.bin") && br_pack_contains("./art\\hero.bin") && !br_pack_contains("skip/nope.txt") && !br_pack_contains(".hidden"));
    text = (char *)br_pack_read("art/hero.bin", &size);
    CHECK(text && size == sizeof(bin) && memcmp(text, bin, sizeof(bin)) == 0);
    br_free(text);
    text = (char *)br_file_read_all("main.scene", &size);                 /* relative paths: the pack first */
    CHECK(text && size == 15 && strcmp(text, "bronto_scene 1\n") == 0);
    br_free(text);
    text = (char *)br_file_read_all("art/empty.txt", &size);
    CHECK(text && size == 0 && text[0] == 0);
    br_free(text);
    br_pack_unmount();
    CHECK(br_file_read_all("art/empty.txt", &size) == NULL);

    /* added to the end of a program */
    br_path_join(exe, sizeof(exe), g_out_dir, "fake_player.exe");
    CHECK(br_file_write_all(exe, "MZ this is the program", 22));
    CHECK(!br_pack_mount(exe));                                             /* nothing added yet */
    CHECK(br_pack_create(root, exe, true, skip_folder, NULL, NULL, err, sizeof(err)));
    CHECK(br_pack_mount(exe) && br_pack_file_count() == 3);
    text = (char *)br_pack_read("art/hero.bin", &size);
    CHECK(text && size == sizeof(bin) && text[299] == (char)(299 * 7));
    br_free(text);
    text = (char *)br_file_read_all(exe, &size);                            /* absolute paths: the disk */
    CHECK(text && memcmp(text, "MZ this is the program", 22) == 0);
    br_free(text);
    br_pack_unmount();
    CHECK(!br_pack_create("no/such/folder", pak, false, NULL, NULL, NULL, err, sizeof(err)) && err[0]);
}

static void test_project_settings(void) {
    br_project p, q;
    char dir[BR_PATH_MAX];
    br_path_join(dir, sizeof(dir), g_out_dir, "proj");
    br_fs_mkdir(dir);
    br_project_defaults(&p, "Thing");
    CHECK(strcmp(p.main_scene, "main.scene") == 0 && p.width == 320 && p.height == 180 && p.window_scale == 3 && !p.strict_typing);
    CHECK(br_project_parse("bronto_project 1\n# old file\n", &p) && strcmp(p.name, "Thing") == 0);   /* the editor's old ones */
    CHECK(br_project_parse("bronto_project 1\nname = Pop Defense\n  width=640 \nstrict_typing = true\nbuild_dir =\nbogus = 1\n", &p));
    CHECK(strcmp(p.name, "Pop Defense") == 0 && p.width == 640 && p.strict_typing && strcmp(p.build_dir, "builds") == 0);
    CHECK(!br_project_parse("not a project\n", &p));
    snprintf(p.main_scene, sizeof(p.main_scene), "levels/one.scene");
    snprintf(p.icon, sizeof(p.icon), "art/icon.bmp");
    CHECK(br_project_save(dir, &p));
    CHECK(br_project_load(dir, &q));
    CHECK(strcmp(q.name, "Pop Defense") == 0 && strcmp(q.main_scene, "levels/one.scene") == 0 && q.width == 640 &&
          q.height == 180 && q.strict_typing && strcmp(q.icon, "art/icon.bmp") == 0);
    br_path_join(dir, sizeof(dir), g_out_dir, "no_project_here");
    br_fs_mkdir(dir);
    CHECK(!br_project_load(dir, &q) && strcmp(q.name, "no_project_here") == 0);       /* defaults, named after the folder */
}

void suite_fs(void) {
    RUN(test_paths);
    RUN(test_fs_ops);
    RUN(test_capture);
    RUN(test_pack);
    RUN(test_project_settings);
}
