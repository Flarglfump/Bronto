/* Minimal dependency-free test harness. */
#ifndef BRONTO_TEST_H
#define BRONTO_TEST_H

#include <stdio.h>
#include <math.h>
#include "bronto/bronto.h"

extern int g_checks, g_failures;
extern const char *g_current_test;

#define CHECK(cond) do { \
        g_checks++; \
        if (!(cond)) { \
            g_failures++; \
            fprintf(stderr, "    FAIL [%s] %s:%d: %s\n", g_current_test, __FILE__, __LINE__, #cond); \
        } \
    } while (0)

#define CHECK_EQ_INT(a, b) do { \
        long long va_ = (long long)(a), vb_ = (long long)(b); \
        g_checks++; \
        if (va_ != vb_) { \
            g_failures++; \
            fprintf(stderr, "    FAIL [%s] %s:%d: %s == %s (%lld vs %lld)\n", g_current_test, \
                    __FILE__, __LINE__, #a, #b, va_, vb_); \
        } \
    } while (0)

#define CHECK_NEAR(a, b, eps) do { \
        double va_ = (double)(a), vb_ = (double)(b); \
        g_checks++; \
        if (fabs(va_ - vb_) > (eps)) { \
            g_failures++; \
            fprintf(stderr, "    FAIL [%s] %s:%d: %s ~= %s (%g vs %g)\n", g_current_test, \
                    __FILE__, __LINE__, #a, #b, va_, vb_); \
        } \
    } while (0)

/* Run a statement with logging silenced (for paths that log expected errors). */
#define QUIET(stmt) do { \
        br_log_level lvl_ = br_log_get_level(); \
        br_log_set_level(BR_LOG_NONE); \
        stmt; \
        br_log_set_level(lvl_); \
    } while (0)

typedef void (*test_fn)(void);
void run_test(const char *name, test_fn fn);
#define RUN(fn) run_test(#fn, fn)

/* Suites */
void suite_memory(void);
void suite_math(void);
void suite_events(void);
void suite_input(void);
void suite_ecs(void);
void suite_scene(void);
void suite_render(void);
void suite_audio(void);
void suite_engine(void);
void suite_window(void);
void suite_snake(void);
void suite_audio_ring(void);
void suite_gamepad(void);
void suite_towerdefense(void);
void suite_serialize(void);
void suite_components(void);
void suite_ui(void);
void suite_editor_doc(void);
void suite_fs(void);
void suite_physics(void);
void suite_script(void);

/* Directory for test artefacts (images, wav). */
extern const char *g_out_dir;
const char *test_path(const char *file);

#endif
