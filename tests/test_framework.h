/*
 * test_framework.h - Simple unit test framework for keyhunt
 *
 * A lightweight test framework that requires no external dependencies.
 * Provides assertion macros and test organization utilities.
 *
 * Usage:
 *   TEST(test_name) {
 *       ASSERT_TRUE(condition);
 *       ASSERT_EQ(expected, actual);
 *       // ...
 *   }
 *
 *   int main(int argc, char *argv[]) {
 *       TEST_INIT();
 *       RUN_TEST(test_name);
 *       return TEST_RESULTS();
 *   }
 */

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

/* Color codes for terminal output */
#define CLR_RED     "\033[31m"
#define CLR_GREEN   "\033[32m"
#define CLR_YELLOW  "\033[33m"
#define CLR_CYAN    "\033[36m"
#define CLR_BOLD    "\033[1m"
#define CLR_RESET   "\033[0m"

/* Global test state */
static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;
static int g_current_test_failed = 0;
static const char *g_current_test_name = NULL;

/* Initialize test framework */
#define TEST_INIT() do { \
    g_tests_run = 0; \
    g_tests_passed = 0; \
    g_tests_failed = 0; \
    printf(CLR_CYAN CLR_BOLD "\n=== KEYHUNT UNIT TESTS ===" CLR_RESET "\n\n"); \
} while(0)

/* Define a test function */
#define TEST(name) \
    void test_##name(void)

/* Run a single test */
#define RUN_TEST(name) do { \
    g_current_test_name = #name; \
    g_current_test_failed = 0; \
    g_tests_run++; \
    printf("  Running: " CLR_BOLD "%s" CLR_RESET " ... ", #name); \
    fflush(stdout); \
    test_##name(); \
    if (g_current_test_failed) { \
        g_tests_failed++; \
        printf(CLR_RED "FAILED" CLR_RESET "\n"); \
    } else { \
        g_tests_passed++; \
        printf(CLR_GREEN "PASSED" CLR_RESET "\n"); \
    } \
} while(0)

/* Print test results and return exit code */
#define TEST_RESULTS() ( \
    printf("\n" CLR_CYAN "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" CLR_RESET "\n"), \
    printf("  Total:  %d\n", g_tests_run), \
    printf("  " CLR_GREEN "Passed: %d" CLR_RESET "\n", g_tests_passed), \
    (g_tests_failed > 0 ? printf("  " CLR_RED "Failed: %d" CLR_RESET "\n", g_tests_failed) : 0), \
    printf(CLR_CYAN "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" CLR_RESET "\n\n"), \
    (g_tests_failed > 0 ? 1 : 0) \
)

/* Assertion macros */

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("\n    " CLR_RED "ASSERT_TRUE failed at %s:%d" CLR_RESET "\n", __FILE__, __LINE__); \
        printf("    Condition: %s\n", #cond); \
        g_current_test_failed = 1; \
        return; \
    } \
} while(0)

#define ASSERT_FALSE(cond) do { \
    if ((cond)) { \
        printf("\n    " CLR_RED "ASSERT_FALSE failed at %s:%d" CLR_RESET "\n", __FILE__, __LINE__); \
        printf("    Condition: %s\n", #cond); \
        g_current_test_failed = 1; \
        return; \
    } \
} while(0)

#define ASSERT_EQ(expected, actual) do { \
    if ((expected) != (actual)) { \
        printf("\n    " CLR_RED "ASSERT_EQ failed at %s:%d" CLR_RESET "\n", __FILE__, __LINE__); \
        printf("    Expected: %lld\n", (long long)(expected)); \
        printf("    Actual:   %lld\n", (long long)(actual)); \
        g_current_test_failed = 1; \
        return; \
    } \
} while(0)

#define ASSERT_NEQ(not_expected, actual) do { \
    if ((not_expected) == (actual)) { \
        printf("\n    " CLR_RED "ASSERT_NEQ failed at %s:%d" CLR_RESET "\n", __FILE__, __LINE__); \
        printf("    Expected not: %lld\n", (long long)(not_expected)); \
        printf("    Actual:       %lld\n", (long long)(actual)); \
        g_current_test_failed = 1; \
        return; \
    } \
} while(0)

#define ASSERT_STR_EQ(expected, actual) do { \
    if (strcmp((expected), (actual)) != 0) { \
        printf("\n    " CLR_RED "ASSERT_STR_EQ failed at %s:%d" CLR_RESET "\n", __FILE__, __LINE__); \
        printf("    Expected: \"%s\"\n", (expected)); \
        printf("    Actual:   \"%s\"\n", (actual)); \
        g_current_test_failed = 1; \
        return; \
    } \
} while(0)

#define ASSERT_MEM_EQ(expected, actual, len) do { \
    if (memcmp((expected), (actual), (len)) != 0) { \
        printf("\n    " CLR_RED "ASSERT_MEM_EQ failed at %s:%d" CLR_RESET "\n", __FILE__, __LINE__); \
        printf("    Memory comparison failed for %zu bytes\n", (size_t)(len)); \
        g_current_test_failed = 1; \
        return; \
    } \
} while(0)

#define ASSERT_NULL(ptr) do { \
    if ((ptr) != NULL) { \
        printf("\n    " CLR_RED "ASSERT_NULL failed at %s:%d" CLR_RESET "\n", __FILE__, __LINE__); \
        printf("    Pointer is not NULL\n"); \
        g_current_test_failed = 1; \
        return; \
    } \
} while(0)

#define ASSERT_NOT_NULL(ptr) do { \
    if ((ptr) == NULL) { \
        printf("\n    " CLR_RED "ASSERT_NOT_NULL failed at %s:%d" CLR_RESET "\n", __FILE__, __LINE__); \
        printf("    Pointer is NULL\n"); \
        g_current_test_failed = 1; \
        return; \
    } \
} while(0)

#define ASSERT_DOUBLE_EQ(expected, actual, epsilon) do { \
    double diff = fabs((double)(expected) - (double)(actual)); \
    if (diff > (epsilon)) { \
        printf("\n    " CLR_RED "ASSERT_DOUBLE_EQ failed at %s:%d" CLR_RESET "\n", __FILE__, __LINE__); \
        printf("    Expected: %f\n", (double)(expected)); \
        printf("    Actual:   %f\n", (double)(actual)); \
        printf("    Diff:     %f (max: %f)\n", diff, (double)(epsilon)); \
        g_current_test_failed = 1; \
        return; \
    } \
} while(0)

/* Test section markers */
#define TEST_SECTION(name) do { \
    printf("\n" CLR_YELLOW "[%s]" CLR_RESET "\n", name); \
} while(0)

#endif /* TEST_FRAMEWORK_H */
