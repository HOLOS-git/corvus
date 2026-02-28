/**
 * test_main.c — Minimal test runner
 *
 * No external test framework — just assert-style macros.
 * Returns 0 on all pass, 1 on any failure.
 *
 * SIMULATION DISCLAIMER: Firmware architecture demo, not production code.
 */

#include <stdio.h>
#include <stdlib.h>

/* ── Test infrastructure ───────────────────────────────────────────── */

int g_tests_run = 0;
int g_tests_passed = 0;
int g_tests_failed = 0;

#define TEST_ASSERT(expr) do { \
    g_tests_run++; \
    if (expr) { g_tests_passed++; } \
    else { g_tests_failed++; \
        fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #expr); } \
} while (0)

#define TEST_ASSERT_EQ(a, b) do { \
    g_tests_run++; \
    if ((a) == (b)) { g_tests_passed++; } \
    else { g_tests_failed++; \
        fprintf(stderr, "  FAIL: %s:%d: %s == %s (%ld != %ld)\n", \
                __FILE__, __LINE__, #a, #b, (long)(a), (long)(b)); } \
} while (0)

#define TEST_ASSERT_NE(a, b) do { \
    g_tests_run++; \
    if ((a) != (b)) { g_tests_passed++; } \
    else { g_tests_failed++; \
        fprintf(stderr, "  FAIL: %s:%d: %s != %s (both %ld)\n", \
                __FILE__, __LINE__, #a, #b, (long)(a)); } \
} while (0)

#define TEST_ASSERT_GT(a, b) do { \
    g_tests_run++; \
    if ((a) > (b)) { g_tests_passed++; } \
    else { g_tests_failed++; \
        fprintf(stderr, "  FAIL: %s:%d: %s > %s (%ld <= %ld)\n", \
                __FILE__, __LINE__, #a, #b, (long)(a), (long)(b)); } \
} while (0)

#define TEST_ASSERT_LT(a, b) do { \
    g_tests_run++; \
    if ((a) < (b)) { g_tests_passed++; } \
    else { g_tests_failed++; \
        fprintf(stderr, "  FAIL: %s:%d: %s < %s (%ld >= %ld)\n", \
                __FILE__, __LINE__, #a, #b, (long)(a), (long)(b)); } \
} while (0)

#define RUN_TEST(fn) do { \
    fprintf(stderr, "  [TEST] %s\n", #fn); \
    fn(); \
} while (0)

/* ── External test suites ──────────────────────────────────────────── */
extern void test_bq76952_suite(void);
extern void test_protection_suite(void);
extern void test_contactor_suite(void);
extern void test_can_suite(void);
extern void test_state_suite(void);

/* ── Main ──────────────────────────────────────────────────────────── */

int main(void)
{
    fprintf(stderr, "\n=== Corvus Orca ESS BMS Firmware Tests ===\n\n");

    fprintf(stderr, "[SUITE] BQ76952 Driver\n");
    test_bq76952_suite();

    fprintf(stderr, "\n[SUITE] Protection\n");
    test_protection_suite();

    fprintf(stderr, "\n[SUITE] Contactor\n");
    test_contactor_suite();

    fprintf(stderr, "\n[SUITE] CAN\n");
    test_can_suite();

    fprintf(stderr, "\n[SUITE] State Machine\n");
    test_state_suite();

    fprintf(stderr, "\n=== Results: %d/%d passed, %d failed ===\n\n",
            g_tests_passed, g_tests_run, g_tests_failed);

    return (g_tests_failed > 0) ? 1 : 0;
}
