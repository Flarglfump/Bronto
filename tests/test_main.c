#include "test.h"

#include <string.h>
#include <time.h>

int g_checks = 0, g_failures = 0;
const char *g_current_test = "";
const char *g_out_dir = "build/test_out";
static int g_tests = 0, g_failed_tests = 0, g_leaky_tests = 0;
static const char *g_filter = NULL;

const char *test_path(const char *file) {
    static char buf[4][512];
    static int slot = 0;
    char *b = buf[slot++ & 3];
    snprintf(b, 512, "%s/%s", g_out_dir, file);
    return b;
}

void run_test(const char *name, test_fn fn) {
    br_mem_stats before, after;
    int failures_before = g_failures;
    clock_t t0;
    double ms;
    if (g_filter && !strstr(name, g_filter)) return;
    g_current_test = name;
    before = br_mem_get_stats();
    t0 = clock();
    fn();
    ms = (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
    after = br_mem_get_stats();
    g_tests++;
    if (after.live_allocations != before.live_allocations) {
        fprintf(stderr, "    LEAK [%s] %ld allocation(s), %ld bytes\n", name,
                (long)(after.live_allocations - before.live_allocations),
                (long)(after.live_bytes - before.live_bytes));
        g_leaky_tests++;
        g_failures++;
    }
    if (g_failures != failures_before) g_failed_tests++;
    printf("  %-4s %-44s %7.2f ms\n", g_failures == failures_before ? "ok" : "FAIL", name, ms);
}

int main(int argc, char **argv) {
    int i;
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) g_out_dir = argv[++i];
        else g_filter = argv[i];
    }
    br_log_set_level(BR_LOG_WARN);
    printf("Bronto test suite (out dir: %s)\n", g_out_dir);

    printf("[memory]\n"); suite_memory(); suite_fs();
    printf("[math]\n");   suite_math();
    printf("[events]\n"); suite_events();
    printf("[input]\n");  suite_input(); suite_gamepad();
    printf("[ecs]\n");    suite_ecs(); suite_serialize(); suite_components();
    printf("[physics]\n"); suite_physics();
    printf("[script]\n");  suite_script();
    printf("[scene]\n");  suite_scene();
    printf("[render]\n"); suite_render(); suite_ui();
    printf("[audio]\n");  suite_audio(); suite_audio_ring();
    printf("[engine]\n"); suite_engine();
    printf("[window]\n"); suite_window();
    printf("[snake]\n");  suite_snake();
    printf("[towerdefense]\n"); suite_towerdefense();
    printf("[editor]\n"); suite_editor_doc();

    printf("\n%d tests, %d checks, %d failed test(s), %d leak(s)\n", g_tests, g_checks,
           g_failed_tests, g_leaky_tests);
    if (g_failures) { printf("RESULT: FAIL\n"); return 1; }
    printf("RESULT: PASS\n");
    return 0;
}
