/*
 * test_threading.cpp - Multi-threaded tests for TSan validation
 *
 * These tests exercise the concurrent patterns present in keyhunt to
 * validate that the atomic conversions from plans 02-01/02-02 are
 * correct and TSan-visible.
 *
 * Test A: Atomic counter with multiple writers (THREADOUTPUT/counter pattern)
 * Test B: Per-element atomic array (bsgs_found pattern)
 * Test C: Mutex-protected shared data (stats_lock pattern)
 */

#include "test_framework.h"
#include "platform/platform.h"

#include <atomic>
#include <time.h>
#include <string.h>

/* ============================================================================
 * Test A: Atomic counter with multiple writers
 *
 * Validates the pattern used by THREADOUTPUT and steps[] counters:
 * multiple worker threads increment a shared atomic counter while a
 * stop flag controls termination.
 * ============================================================================ */

struct counter_args {
    std::atomic<int> *counter;
    std::atomic<int> *stop_flag;
};

static void* counter_worker(void* arg) {
    struct counter_args *a = (struct counter_args *)arg;
    while (a->stop_flag->load(std::memory_order_acquire) == 0) {
        a->counter->fetch_add(1, std::memory_order_relaxed);
    }
    return NULL;
}

TEST(threading_atomic_counter_no_race) {
    const int NUM_THREADS = 4;
    std::atomic<int> counter(0);
    std::atomic<int> stop_flag(0);

    struct counter_args args;
    args.counter = &counter;
    args.stop_flag = &stop_flag;

    platform_thread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        int rc = platform_thread_create(&threads[i], counter_worker, &args);
        ASSERT_EQ(0, rc);
    }

    /* Let threads run for 10ms */
    struct timespec ts = {0, 10000000}; /* 10ms */
    nanosleep(&ts, NULL);

    /* Signal stop */
    stop_flag.store(1, std::memory_order_release);

    /* Join all threads */
    for (int i = 0; i < NUM_THREADS; i++) {
        int rc = platform_thread_join(threads[i], NULL);
        ASSERT_EQ(0, rc);
    }

    /* Counter must have been incremented */
    int final_count = counter.load(std::memory_order_relaxed);
    ASSERT_TRUE(final_count > 0);
}

/* ============================================================================
 * Test B: Per-element atomic array
 *
 * Validates the bsgs_found pattern where each thread writes to a
 * separate index in an atomic array, and the main thread reads all
 * elements afterward.
 * ============================================================================ */

struct array_args {
    std::atomic<int> *arr;
    int id;
};

static void* array_worker(void* arg) {
    struct array_args *a = (struct array_args *)arg;
    a->arr[a->id].store(1, std::memory_order_release);
    return NULL;
}

TEST(threading_per_element_atomic_array) {
    const int N = 4;
    std::atomic<int> arr[N];
    for (int i = 0; i < N; i++) {
        arr[i].store(0, std::memory_order_relaxed);
    }

    struct array_args args[N];
    platform_thread_t threads[N];

    for (int i = 0; i < N; i++) {
        args[i].arr = arr;
        args[i].id = i;
        int rc = platform_thread_create(&threads[i], array_worker, &args[i]);
        ASSERT_EQ(0, rc);
    }

    /* Join all threads */
    for (int i = 0; i < N; i++) {
        int rc = platform_thread_join(threads[i], NULL);
        ASSERT_EQ(0, rc);
    }

    /* Verify all elements were set */
    for (int i = 0; i < N; i++) {
        int val = arr[i].load(std::memory_order_acquire);
        ASSERT_EQ(1, val);
    }
}

/* ============================================================================
 * Test C: Mutex-protected shared data
 *
 * Validates the stats_lock pattern where threads lock a platform_mutex_t,
 * modify shared data, and unlock. Verifies consistency after all threads
 * complete.
 * ============================================================================ */

struct mutex_args {
    platform_mutex_t *mutex;
    int *shared_data;
    int increments;
};

static void* mutex_worker(void* arg) {
    struct mutex_args *a = (struct mutex_args *)arg;
    for (int i = 0; i < a->increments; i++) {
        platform_mutex_lock(a->mutex);
        (*a->shared_data)++;
        platform_mutex_unlock(a->mutex);
    }
    return NULL;
}

TEST(threading_mutex_shared_data) {
    const int NUM_THREADS = 2;
    const int INCREMENTS = 1000;

    platform_mutex_t mutex;
    int rc = platform_mutex_init(&mutex);
    ASSERT_EQ(0, rc);

    int shared_data = 0;

    struct mutex_args args[NUM_THREADS];
    platform_thread_t threads[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].mutex = &mutex;
        args[i].shared_data = &shared_data;
        args[i].increments = INCREMENTS;
        rc = platform_thread_create(&threads[i], mutex_worker, &args[i]);
        ASSERT_EQ(0, rc);
    }

    /* Join all threads */
    for (int i = 0; i < NUM_THREADS; i++) {
        rc = platform_thread_join(threads[i], NULL);
        ASSERT_EQ(0, rc);
    }

    /* Verify consistent result: each thread incremented INCREMENTS times */
    ASSERT_EQ(NUM_THREADS * INCREMENTS, shared_data);

    platform_mutex_destroy(&mutex);
}

/* ============================================================================
 * Registration function - called from run_tests.cpp
 * ============================================================================ */

int run_threading_tests(void) {
    TEST_INIT();
    RUN_TEST(threading_atomic_counter_no_race);
    RUN_TEST(threading_per_element_atomic_array);
    RUN_TEST(threading_mutex_shared_data);
    return TEST_RESULTS();
}
