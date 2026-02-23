/*
 * test_bsgs_sort.cpp - Unit tests for BSGS sorting and search algorithms
 *
 * Tests the optimized sorting algorithms used in Baby Step Giant Step:
 * - Introsort (hybrid quicksort/heapsort/insertion sort)
 * - Binary search for X-point lookup
 * - Individual sorting components
 */

#include "test_framework.h"
#include "bsgs/bsgs_sort.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

/* ============================================================================
 * Test Utilities
 * ============================================================================ */

/* Helper: Check if array is sorted in ascending order */
static int is_sorted(struct bsgs_xvalue *arr, int64_t n) {
    for (int64_t i = 1; i < n; i++) {
        if (arr[i].value < arr[i-1].value) {
            return 0;
        }
    }
    return 1;
}

/* Helper: Create test array with specific pattern */
static struct bsgs_xvalue* create_test_array(int64_t n) {
    struct bsgs_xvalue *arr = (struct bsgs_xvalue*)malloc(n * sizeof(struct bsgs_xvalue));
    for (int64_t i = 0; i < n; i++) {
        arr[i].value = i;
        arr[i].index = i;
    }
    return arr;
}

/* Helper: Create reverse-sorted array */
static struct bsgs_xvalue* create_reverse_array(int64_t n) {
    struct bsgs_xvalue *arr = (struct bsgs_xvalue*)malloc(n * sizeof(struct bsgs_xvalue));
    for (int64_t i = 0; i < n; i++) {
        arr[i].value = n - i - 1;
        arr[i].index = i;
    }
    return arr;
}

/* Helper: Create random array (deterministic for testing) */
static struct bsgs_xvalue* create_random_array(int64_t n, uint32_t seed) {
    struct bsgs_xvalue *arr = (struct bsgs_xvalue*)malloc(n * sizeof(struct bsgs_xvalue));
    srand(seed);
    for (int64_t i = 0; i < n; i++) {
        arr[i].value = ((uint64_t)rand() << 32) | rand();
        arr[i].index = i;
    }
    return arr;
}

/* Helper: Create array with duplicates */
static struct bsgs_xvalue* create_duplicate_array(int64_t n) {
    struct bsgs_xvalue *arr = (struct bsgs_xvalue*)malloc(n * sizeof(struct bsgs_xvalue));
    for (int64_t i = 0; i < n; i++) {
        arr[i].value = i / 2;  /* Each value appears twice */
        arr[i].index = i;
    }
    return arr;
}

/* ============================================================================
 * Swap Tests
 * ============================================================================ */

TEST(bsgs_swap_basic) {
    struct bsgs_xvalue a = {100, 1};
    struct bsgs_xvalue b = {200, 2};

    bsgs_swap(&a, &b);

    ASSERT_EQ(200, a.value);
    ASSERT_EQ(2, a.index);   /* index also swapped */
    ASSERT_EQ(100, b.value);
    ASSERT_EQ(1, b.index);   /* index also swapped */
}

TEST(bsgs_swap_same_element) {
    struct bsgs_xvalue a = {100, 1};

    bsgs_swap(&a, &a);

    ASSERT_EQ(100, a.value);
    ASSERT_EQ(1, a.index);
}

/* ============================================================================
 * Insertion Sort Tests (for small arrays < 16 elements)
 * ============================================================================ */

TEST(bsgs_insertionsort_empty) {
    struct bsgs_xvalue *arr = create_test_array(0);
    bsgs_insertionsort(arr, 0);
    free(arr);
}

TEST(bsgs_insertionsort_single) {
    struct bsgs_xvalue *arr = create_test_array(1);
    arr[0].value = 42;

    bsgs_insertionsort(arr, 1);

    ASSERT_EQ(42, arr[0].value);
    free(arr);
}

TEST(bsgs_insertionsort_two_sorted) {
    struct bsgs_xvalue arr[2] = {{10, 0}, {20, 1}};

    bsgs_insertionsort(arr, 2);

    ASSERT_EQ(10, arr[0].value);
    ASSERT_EQ(20, arr[1].value);
}

TEST(bsgs_insertionsort_two_reverse) {
    struct bsgs_xvalue arr[2] = {{20, 0}, {10, 1}};

    bsgs_insertionsort(arr, 2);

    ASSERT_EQ(10, arr[0].value);
    ASSERT_EQ(20, arr[1].value);
}

TEST(bsgs_insertionsort_small_array) {
    struct bsgs_xvalue *arr = create_reverse_array(8);

    bsgs_insertionsort(arr, 8);

    ASSERT_TRUE(is_sorted(arr, 8));
    ASSERT_EQ(0, arr[0].value);
    ASSERT_EQ(7, arr[7].value);
    free(arr);
}

TEST(bsgs_insertionsort_with_duplicates) {
    struct bsgs_xvalue arr[5] = {{5, 0}, {2, 1}, {5, 2}, {1, 3}, {2, 4}};

    bsgs_insertionsort(arr, 5);

    ASSERT_TRUE(is_sorted(arr, 5));
    ASSERT_EQ(1, arr[0].value);
    ASSERT_EQ(5, arr[4].value);
}

TEST(bsgs_insertionsort_already_sorted) {
    struct bsgs_xvalue *arr = create_test_array(10);

    bsgs_insertionsort(arr, 10);

    ASSERT_TRUE(is_sorted(arr, 10));
    free(arr);
}

/* ============================================================================
 * Partition Tests
 * ============================================================================ */

TEST(bsgs_partition_three_elements) {
    struct bsgs_xvalue arr[3] = {{30, 0}, {10, 1}, {20, 2}};

    int64_t pivot = bsgs_partition(arr, 3);

    /* Pivot should be in correct position */
    ASSERT_TRUE(pivot >= 0 && pivot < 3);
    /* Elements before pivot should be <= pivot */
    for (int64_t i = 0; i < pivot; i++) {
        ASSERT_TRUE(arr[i].value <= arr[pivot].value);
    }
    /* Elements after pivot should be > pivot */
    for (int64_t i = pivot + 1; i < 3; i++) {
        ASSERT_TRUE(arr[i].value > arr[pivot].value);
    }
}

TEST(bsgs_partition_all_same) {
    struct bsgs_xvalue arr[5] = {{42, 0}, {42, 1}, {42, 2}, {42, 3}, {42, 4}};

    int64_t pivot = bsgs_partition(arr, 5);

    ASSERT_TRUE(pivot >= 0 && pivot < 5);
    for (int64_t i = 0; i < 5; i++) {
        ASSERT_EQ(42, arr[i].value);
    }
}

/* ============================================================================
 * Heapify Tests
 * ============================================================================ */

TEST(bsgs_heapify_basic) {
    struct bsgs_xvalue arr[7] = {{1, 0}, {2, 1}, {3, 2}, {4, 3}, {5, 4}, {6, 5}, {7, 6}};

    /* Build max heap */
    for (int64_t i = 6/2; i >= 0; i--) {
        bsgs_heapify(arr, 7, i);
    }

    /* Root should be maximum */
    uint64_t max = arr[0].value;
    for (int64_t i = 1; i < 7; i++) {
        ASSERT_TRUE(arr[i].value <= max);
    }
}

/* ============================================================================
 * Heap Sort Tests
 * ============================================================================ */

TEST(bsgs_heapsort_small) {
    struct bsgs_xvalue *arr = create_reverse_array(10);

    bsgs_myheapsort(arr, 10);

    ASSERT_TRUE(is_sorted(arr, 10));
    free(arr);
}

TEST(bsgs_heapsort_medium) {
    struct bsgs_xvalue *arr = create_random_array(100, 12345);

    bsgs_myheapsort(arr, 100);

    ASSERT_TRUE(is_sorted(arr, 100));
    free(arr);
}

TEST(bsgs_heapsort_with_duplicates) {
    struct bsgs_xvalue *arr = create_duplicate_array(20);

    bsgs_myheapsort(arr, 20);

    ASSERT_TRUE(is_sorted(arr, 20));
    free(arr);
}

/* ============================================================================
 * Main Sort Tests (bsgs_sort using introsort)
 * ============================================================================ */

TEST(bsgs_sort_empty) {
    struct bsgs_xvalue *arr = create_test_array(0);
    bsgs_sort(arr, 0);
    free(arr);
}

TEST(bsgs_sort_single) {
    struct bsgs_xvalue arr[1] = {{42, 0}};

    bsgs_sort(arr, 1);

    ASSERT_EQ(42, arr[0].value);
}

TEST(bsgs_sort_small_array) {
    struct bsgs_xvalue *arr = create_reverse_array(10);

    bsgs_sort(arr, 10);

    ASSERT_TRUE(is_sorted(arr, 10));
    ASSERT_EQ(0, arr[0].value);
    ASSERT_EQ(9, arr[9].value);
    free(arr);
}

TEST(bsgs_sort_medium_array) {
    struct bsgs_xvalue *arr = create_random_array(100, 54321);

    bsgs_sort(arr, 100);

    ASSERT_TRUE(is_sorted(arr, 100));
    free(arr);
}

TEST(bsgs_sort_large_array) {
    struct bsgs_xvalue *arr = create_random_array(10000, 98765);

    bsgs_sort(arr, 10000);

    ASSERT_TRUE(is_sorted(arr, 10000));
    free(arr);
}

TEST(bsgs_sort_already_sorted) {
    struct bsgs_xvalue *arr = create_test_array(1000);

    bsgs_sort(arr, 1000);

    ASSERT_TRUE(is_sorted(arr, 1000));
    free(arr);
}

TEST(bsgs_sort_reverse_sorted) {
    struct bsgs_xvalue *arr = create_reverse_array(1000);

    bsgs_sort(arr, 1000);

    ASSERT_TRUE(is_sorted(arr, 1000));
    free(arr);
}

TEST(bsgs_sort_with_duplicates) {
    struct bsgs_xvalue *arr = create_duplicate_array(1000);

    bsgs_sort(arr, 1000);

    ASSERT_TRUE(is_sorted(arr, 1000));
    free(arr);
}

TEST(bsgs_sort_boundary_16_elements) {
    /* Test boundary where insertion sort is used (n <= 16) */
    struct bsgs_xvalue *arr = create_random_array(16, 11111);

    bsgs_sort(arr, 16);

    ASSERT_TRUE(is_sorted(arr, 16));
    free(arr);
}

TEST(bsgs_sort_boundary_17_elements) {
    /* Test boundary where introsort dispatches to quicksort/heapsort */
    struct bsgs_xvalue *arr = create_random_array(17, 22222);

    bsgs_sort(arr, 17);

    ASSERT_TRUE(is_sorted(arr, 17));
    free(arr);
}

/* ============================================================================
 * Binary Search Tests
 * ============================================================================ */

TEST(bsgs_searchbinary_empty_array) {
    struct bsgs_xvalue *arr = create_test_array(0);
    char data[32] = {0};
    uint64_t r_value = 0;

    int found = bsgs_searchbinary(arr, data, 0, &r_value);

    ASSERT_EQ(0, found);
    free(arr);
}

TEST(bsgs_searchbinary_single_found) {
    struct bsgs_xvalue arr[1];
    arr[0].value = 0x123456789ABCDEF0ULL;
    arr[0].index = 42;

    /* Create data with matching bytes 16-23 */
    char data[32] = {0};
    uint64_t search_val = 0x123456789ABCDEF0ULL;
    memcpy(data + 16, &search_val, 8);

    uint64_t r_value = 0;
    int found = bsgs_searchbinary(arr, data, 1, &r_value);

    ASSERT_EQ(1, found);
    ASSERT_EQ(42, r_value);
}

TEST(bsgs_searchbinary_single_not_found) {
    struct bsgs_xvalue arr[1];
    arr[0].value = 0x123456789ABCDEF0ULL;
    arr[0].index = 42;

    /* Create data with non-matching bytes */
    char data[32] = {0};
    uint64_t search_val = 0xFFFFFFFFFFFFFFFFULL;
    memcpy(data + 16, &search_val, 8);

    uint64_t r_value = 0;
    int found = bsgs_searchbinary(arr, data, 1, &r_value);

    ASSERT_EQ(0, found);
}

TEST(bsgs_searchbinary_multiple_found_first) {
    struct bsgs_xvalue arr[5];
    for (int i = 0; i < 5; i++) {
        arr[i].value = i * 100;
        arr[i].index = i + 10;
    }

    /* Search for first element */
    char data[32] = {0};
    uint64_t search_val = 0;
    memcpy(data + 16, &search_val, 8);

    uint64_t r_value = 0;
    int found = bsgs_searchbinary(arr, data, 5, &r_value);

    ASSERT_EQ(1, found);
    ASSERT_EQ(10, r_value);
}

TEST(bsgs_searchbinary_multiple_found_last) {
    struct bsgs_xvalue arr[5];
    for (int i = 0; i < 5; i++) {
        arr[i].value = i * 100;
        arr[i].index = i + 10;
    }

    /* Search for last element */
    char data[32] = {0};
    uint64_t search_val = 400;
    memcpy(data + 16, &search_val, 8);

    uint64_t r_value = 0;
    int found = bsgs_searchbinary(arr, data, 5, &r_value);

    ASSERT_EQ(1, found);
    ASSERT_EQ(14, r_value);
}

TEST(bsgs_searchbinary_multiple_found_middle) {
    struct bsgs_xvalue arr[5];
    for (int i = 0; i < 5; i++) {
        arr[i].value = i * 100;
        arr[i].index = i + 10;
    }

    /* Search for middle element */
    char data[32] = {0};
    uint64_t search_val = 200;
    memcpy(data + 16, &search_val, 8);

    uint64_t r_value = 0;
    int found = bsgs_searchbinary(arr, data, 5, &r_value);

    ASSERT_EQ(1, found);
    ASSERT_EQ(12, r_value);
}

TEST(bsgs_searchbinary_large_array) {
    /* Create large sorted array */
    int64_t n = 10000;
    struct bsgs_xvalue *arr = (struct bsgs_xvalue*)malloc(n * sizeof(struct bsgs_xvalue));
    for (int64_t i = 0; i < n; i++) {
        arr[i].value = i * 1000;
        arr[i].index = i + 5000;
    }

    /* Search for element in middle */
    char data[32] = {0};
    uint64_t search_val = 5000 * 1000;  /* Element at index 5000 */
    memcpy(data + 16, &search_val, 8);

    uint64_t r_value = 0;
    int found = bsgs_searchbinary(arr, data, n, &r_value);

    ASSERT_EQ(1, found);
    ASSERT_EQ(10000, r_value);

    free(arr);
}

TEST(bsgs_searchbinary_not_found_between) {
    struct bsgs_xvalue arr[3];
    arr[0].value = 100;
    arr[0].index = 1;
    arr[1].value = 300;
    arr[1].index = 2;
    arr[2].value = 500;
    arr[2].index = 3;

    /* Search for value between elements */
    char data[32] = {0};
    uint64_t search_val = 200;
    memcpy(data + 16, &search_val, 8);

    uint64_t r_value = 0;
    int found = bsgs_searchbinary(arr, data, 3, &r_value);

    ASSERT_EQ(0, found);
}

/* ============================================================================
 * Integration Tests: Sort + Search
 * ============================================================================ */

TEST(bsgs_sort_and_search_integration) {
    /* Create unsorted array */
    struct bsgs_xvalue arr[10];
    uint64_t values[] = {500, 100, 900, 300, 700, 200, 800, 400, 600, 1000};
    for (int i = 0; i < 10; i++) {
        arr[i].value = values[i];
        arr[i].index = i + 100;
    }

    /* Sort the array */
    bsgs_sort(arr, 10);

    /* Verify sorted */
    ASSERT_TRUE(is_sorted(arr, 10));

    /* Search for existing value */
    char data[32] = {0};
    uint64_t search_val = 700;
    memcpy(data + 16, &search_val, 8);

    uint64_t r_value = 0;
    int found = bsgs_searchbinary(arr, data, 10, &r_value);

    ASSERT_EQ(1, found);
    ASSERT_EQ(104, r_value);  /* Original index was 4, so index = 104 */
}

TEST(bsgs_large_sort_and_multiple_searches) {
    /* Create large random array */
    int64_t n = 1000;
    struct bsgs_xvalue *arr = create_random_array(n, 77777);

    /* Sort it */
    bsgs_sort(arr, n);

    /* Verify sorted */
    ASSERT_TRUE(is_sorted(arr, n));

    /* Perform multiple searches */
    for (int i = 0; i < 10; i++) {
        char data[32] = {0};
        memcpy(data + 16, &arr[i * 100].value, 8);

        uint64_t r_value = 0;
        int found = bsgs_searchbinary(arr, data, n, &r_value);

        ASSERT_EQ(1, found);
    }

    free(arr);
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

/* Exported function for test runner */
int run_bsgs_sort_tests(void) {
    TEST_INIT();

    TEST_SECTION("Swap Operations");
    RUN_TEST(bsgs_swap_basic);
    RUN_TEST(bsgs_swap_same_element);

    TEST_SECTION("Insertion Sort");
    RUN_TEST(bsgs_insertionsort_empty);
    RUN_TEST(bsgs_insertionsort_single);
    RUN_TEST(bsgs_insertionsort_two_sorted);
    RUN_TEST(bsgs_insertionsort_two_reverse);
    RUN_TEST(bsgs_insertionsort_small_array);
    RUN_TEST(bsgs_insertionsort_with_duplicates);
    RUN_TEST(bsgs_insertionsort_already_sorted);

    TEST_SECTION("Partition");
    RUN_TEST(bsgs_partition_three_elements);
    RUN_TEST(bsgs_partition_all_same);

    TEST_SECTION("Heapify");
    RUN_TEST(bsgs_heapify_basic);

    TEST_SECTION("Heap Sort");
    RUN_TEST(bsgs_heapsort_small);
    RUN_TEST(bsgs_heapsort_medium);
    RUN_TEST(bsgs_heapsort_with_duplicates);

    TEST_SECTION("Main Sort (Introsort)");
    RUN_TEST(bsgs_sort_empty);
    RUN_TEST(bsgs_sort_single);
    RUN_TEST(bsgs_sort_small_array);
    RUN_TEST(bsgs_sort_medium_array);
    RUN_TEST(bsgs_sort_large_array);
    RUN_TEST(bsgs_sort_already_sorted);
    RUN_TEST(bsgs_sort_reverse_sorted);
    RUN_TEST(bsgs_sort_with_duplicates);
    RUN_TEST(bsgs_sort_boundary_16_elements);
    RUN_TEST(bsgs_sort_boundary_17_elements);

    TEST_SECTION("Binary Search");
    RUN_TEST(bsgs_searchbinary_empty_array);
    RUN_TEST(bsgs_searchbinary_single_found);
    RUN_TEST(bsgs_searchbinary_single_not_found);
    RUN_TEST(bsgs_searchbinary_multiple_found_first);
    RUN_TEST(bsgs_searchbinary_multiple_found_last);
    RUN_TEST(bsgs_searchbinary_multiple_found_middle);
    RUN_TEST(bsgs_searchbinary_large_array);
    RUN_TEST(bsgs_searchbinary_not_found_between);

    TEST_SECTION("Integration Tests");
    RUN_TEST(bsgs_sort_and_search_integration);
    RUN_TEST(bsgs_large_sort_and_multiple_searches);

    return TEST_RESULTS();
}

/* Standalone main for individual testing */
#ifdef TEST_STANDALONE
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    return run_bsgs_sort_tests();
}
#endif
