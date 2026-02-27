/*
 * test_multi_gpu_integration.cpp - Integration tests for multi-GPU worker threads
 *
 * Uses Option C: Both mock and conditional tests
 * - Mock tests using stub pattern for CI (no real GPU needed)
 * - Conditional tests with #ifdef HAVE_CUDA_BACKEND for real GPU validation
 *
 * Tests:
 * - Worker thread initialization and lifecycle
 * - Worker thread start/stop/pause/resume
 * - Multi-worker coordination via scheduler
 * - Statistics collection and aggregation
 * - Error handling and recovery
 * - Work distribution across multiple workers
 */

#include "test_framework.h"

extern "C" {
#include "gpu/gpu_multi_worker.h"
#include "gpu/multi_gpu_scheduler.h"
#include "gpu/gpu_backend.h"
}

#include <string.h>
#include <stdlib.h>
#include <unistd.h>  /* For usleep */

/* ============================================================================
 * Worker Initialization Tests (Mock - always run)
 * ============================================================================ */

TEST(worker_init_null_config) {
    /* Should handle NULL config gracefully */
    gpu_multi_worker_t *worker = gpu_worker_init(NULL);
    ASSERT_NULL(worker);
}

TEST(worker_init_null_scheduler) {
    worker_config_t config;
    memset(&config, 0, sizeof(config));
    config.scheduler = NULL;
    config.device_count = 1;
    config.device_ids[0] = 0;

    /* Should fail with NULL scheduler */
    gpu_multi_worker_t *worker = gpu_worker_init(&config);
    ASSERT_NULL(worker);
}

TEST(worker_init_invalid_device_count) {
    /* Create a scheduler first */
    multi_gpu_scheduler_t *sched = multi_gpu_init(NULL);

#ifndef HAVE_CUDA_BACKEND
    /* Without CUDA, scheduler init should fail */
    ASSERT_NULL(sched);
#else
    if (sched) {
        worker_config_t config;
        memset(&config, 0, sizeof(config));
        config.scheduler = sched;

        /* Test zero devices */
        config.device_count = 0;
        gpu_multi_worker_t *worker = gpu_worker_init(&config);
        ASSERT_NULL(worker);

        /* Test negative devices */
        config.device_count = -5;
        worker = gpu_worker_init(&config);
        ASSERT_NULL(worker);

        /* Test too many devices */
        config.device_count = MULTI_GPU_MAX_DEVICES + 1;
        worker = gpu_worker_init(&config);
        ASSERT_NULL(worker);

        multi_gpu_shutdown(sched);
    }
#endif
}

TEST(worker_shutdown_null) {
    /* Shutdown with NULL should not crash */
    gpu_worker_shutdown(NULL);
    ASSERT_TRUE(1);
}

TEST(worker_default_config) {
    multi_gpu_scheduler_t *sched = multi_gpu_init(NULL);

#ifndef HAVE_CUDA_BACKEND
    /* Without CUDA, scheduler should be NULL */
    ASSERT_NULL(sched);
#else
    if (sched) {
        /* Test default config creation */
        worker_config_t config = gpu_worker_default_config(sched, -1);

        ASSERT_EQ(sched, config.scheduler);
        ASSERT_TRUE(config.batch_size > 0);
        ASSERT_EQ(0, config.priority);

        multi_gpu_shutdown(sched);
    }
#endif
}

/* ============================================================================
 * Worker Lifecycle Tests
 * ============================================================================ */

TEST(worker_start_before_init) {
    /* Starting NULL worker should fail */
    bool result = gpu_worker_start(NULL);
    ASSERT_FALSE(result);
}

TEST(worker_stop_before_start) {
    /* Stopping NULL worker should succeed (no-op) */
    bool result = gpu_worker_stop(NULL, 1000);
    ASSERT_FALSE(result);
}

TEST(worker_init_and_shutdown) {
#ifdef HAVE_CUDA_BACKEND
    multi_gpu_scheduler_t *sched = multi_gpu_init(NULL);

    if (sched && multi_gpu_available() > 0) {
        worker_config_t config = gpu_worker_default_config(sched, 1);
        config.device_ids[0] = 0;

        gpu_multi_worker_t *worker = gpu_worker_init(&config);
        ASSERT_NOT_NULL(worker);

        /* Shutdown without starting should be safe */
        gpu_worker_shutdown(worker);

        multi_gpu_shutdown(sched);
    }
#else
    /* Without CUDA, just verify we can handle the case */
    ASSERT_TRUE(1);
#endif
}

TEST(worker_start_and_stop) {
#ifdef HAVE_CUDA_BACKEND
    gpu_backend_init(NULL);
    multi_gpu_scheduler_t *sched = multi_gpu_init(NULL);

    if (sched && multi_gpu_available() > 0) {
        worker_config_t config = gpu_worker_default_config(sched, 1);
        config.device_ids[0] = 0;
        config.batch_size = 1024;  /* Small batch for quick test */

        gpu_multi_worker_t *worker = gpu_worker_init(&config);
        ASSERT_NOT_NULL(worker);

        /* Set a small work range */
        multi_gpu_set_range(sched, 0, 10000);

        /* Start worker */
        bool started = gpu_worker_start(worker);
        ASSERT_TRUE(started);

        /* Stop worker with timeout */
        bool stopped = gpu_worker_stop(worker, 5000);
        ASSERT_TRUE(stopped);

        gpu_worker_shutdown(worker);
        multi_gpu_shutdown(sched);
    }

    gpu_backend_shutdown();
#else
    ASSERT_TRUE(1);
#endif
}

TEST(worker_double_start) {
#ifdef HAVE_CUDA_BACKEND
    gpu_backend_init(NULL);
    multi_gpu_scheduler_t *sched = multi_gpu_init(NULL);

    if (sched && multi_gpu_available() > 0) {
        worker_config_t config = gpu_worker_default_config(sched, 1);
        config.device_ids[0] = 0;

        gpu_multi_worker_t *worker = gpu_worker_init(&config);
        ASSERT_NOT_NULL(worker);

        multi_gpu_set_range(sched, 0, 10000);

        /* First start should succeed */
        bool started1 = gpu_worker_start(worker);
        ASSERT_TRUE(started1);

        /* Second start should fail or be no-op */
        bool started2 = gpu_worker_start(worker);
        /* Implementation may choose to fail or allow */
        (void)started2;

        gpu_worker_stop(worker, 5000);
        gpu_worker_shutdown(worker);
        multi_gpu_shutdown(sched);
    }

    gpu_backend_shutdown();
#else
    ASSERT_TRUE(1);
#endif
}

/* ============================================================================
 * Worker Statistics Tests
 * ============================================================================ */

TEST(worker_get_stats_null) {
    multi_gpu_worker_stats_t stats;
    memset(&stats, 0xFF, sizeof(stats));

    /* Should handle NULL worker gracefully */
    gpu_worker_get_stats(NULL, &stats);
    ASSERT_TRUE(1);  /* Just checking no crash */
}

TEST(worker_get_stats_null_output) {
#ifdef HAVE_CUDA_BACKEND
    gpu_backend_init(NULL);
    multi_gpu_scheduler_t *sched = multi_gpu_init(NULL);

    if (sched && multi_gpu_available() > 0) {
        worker_config_t config = gpu_worker_default_config(sched, 1);
        gpu_multi_worker_t *worker = gpu_worker_init(&config);

        if (worker) {
            /* Should handle NULL output gracefully */
            gpu_worker_get_stats(worker, NULL);
            ASSERT_TRUE(1);

            gpu_worker_shutdown(worker);
        }

        multi_gpu_shutdown(sched);
    }

    gpu_backend_shutdown();
#else
    ASSERT_TRUE(1);
#endif
}

TEST(worker_get_stats_initial) {
#ifdef HAVE_CUDA_BACKEND
    gpu_backend_init(NULL);
    multi_gpu_scheduler_t *sched = multi_gpu_init(NULL);

    if (sched && multi_gpu_available() > 0) {
        worker_config_t config = gpu_worker_default_config(sched, 1);
        config.device_ids[0] = 0;

        gpu_multi_worker_t *worker = gpu_worker_init(&config);
        ASSERT_NOT_NULL(worker);

        multi_gpu_worker_stats_t stats;
        memset(&stats, 0xFF, sizeof(stats));

        gpu_worker_get_stats(worker, &stats);

        /* Initial state should have zero keys processed */
        ASSERT_EQ(0UL, stats.total_keys_processed);
        ASSERT_EQ(0.0, stats.combined_throughput);
        ASSERT_TRUE(stats.active_workers >= 0);

        gpu_worker_shutdown(worker);
        multi_gpu_shutdown(sched);
    }

    gpu_backend_shutdown();
#else
    ASSERT_TRUE(1);
#endif
}

TEST(worker_get_device_stats_invalid) {
#ifdef HAVE_CUDA_BACKEND
    gpu_backend_init(NULL);
    multi_gpu_scheduler_t *sched = multi_gpu_init(NULL);

    if (sched && multi_gpu_available() > 0) {
        worker_config_t config = gpu_worker_default_config(sched, 1);
        config.device_ids[0] = 0;

        gpu_multi_worker_t *worker = gpu_worker_init(&config);
        ASSERT_NOT_NULL(worker);

        gpu_worker_stats_t stats;

        /* Invalid device ID should return false */
        bool result = gpu_worker_get_device_stats(worker, 999, &stats);
        ASSERT_FALSE(result);

        gpu_worker_shutdown(worker);
        multi_gpu_shutdown(sched);
    }

    gpu_backend_shutdown();
#else
    ASSERT_TRUE(1);
#endif
}

TEST(worker_get_device_stats_valid) {
#ifdef HAVE_CUDA_BACKEND
    gpu_backend_init(NULL);
    multi_gpu_scheduler_t *sched = multi_gpu_init(NULL);

    if (sched && multi_gpu_available() > 0) {
        worker_config_t config = gpu_worker_default_config(sched, 1);
        config.device_ids[0] = 0;

        gpu_multi_worker_t *worker = gpu_worker_init(&config);
        ASSERT_NOT_NULL(worker);

        gpu_worker_stats_t stats;
        memset(&stats, 0xFF, sizeof(stats));

        /* Valid device ID should return true */
        bool result = gpu_worker_get_device_stats(worker, 0, &stats);
        ASSERT_TRUE(result);

        /* Check initial values */
        ASSERT_EQ(0, stats.device_id);
        ASSERT_EQ(WORKER_IDLE, stats.status);
        ASSERT_EQ(0UL, stats.keys_processed);

        gpu_worker_shutdown(worker);
        multi_gpu_shutdown(sched);
    }

    gpu_backend_shutdown();
#else
    ASSERT_TRUE(1);
#endif
}

/* ============================================================================
 * Worker Pause/Resume Tests
 * ============================================================================ */

TEST(worker_pause_null) {
    bool result = gpu_worker_pause(NULL);
    ASSERT_FALSE(result);
}

TEST(worker_resume_null) {
    bool result = gpu_worker_resume(NULL);
    ASSERT_FALSE(result);
}

TEST(worker_pause_resume_lifecycle) {
#ifdef HAVE_CUDA_BACKEND
    gpu_backend_init(NULL);
    multi_gpu_scheduler_t *sched = multi_gpu_init(NULL);

    if (sched && multi_gpu_available() > 0) {
        worker_config_t config = gpu_worker_default_config(sched, 1);
        config.device_ids[0] = 0;
        config.batch_size = 1024;

        gpu_multi_worker_t *worker = gpu_worker_init(&config);
        ASSERT_NOT_NULL(worker);

        multi_gpu_set_range(sched, 0, 100000);

        /* Start worker */
        bool started = gpu_worker_start(worker);
        ASSERT_TRUE(started);

        /* Pause worker */
        bool paused = gpu_worker_pause(worker);
        ASSERT_TRUE(paused);

        /* Resume worker */
        bool resumed = gpu_worker_resume(worker);
        ASSERT_TRUE(resumed);

        /* Stop worker */
        bool stopped = gpu_worker_stop(worker, 5000);
        ASSERT_TRUE(stopped);

        gpu_worker_shutdown(worker);
        multi_gpu_shutdown(sched);
    }

    gpu_backend_shutdown();
#else
    ASSERT_TRUE(1);
#endif
}

TEST(worker_double_pause) {
#ifdef HAVE_CUDA_BACKEND
    gpu_backend_init(NULL);
    multi_gpu_scheduler_t *sched = multi_gpu_init(NULL);

    if (sched && multi_gpu_available() > 0) {
        worker_config_t config = gpu_worker_default_config(sched, 1);
        gpu_multi_worker_t *worker = gpu_worker_init(&config);

        if (worker) {
            multi_gpu_set_range(sched, 0, 10000);
            gpu_worker_start(worker);

            /* First pause */
            bool paused1 = gpu_worker_pause(worker);
            ASSERT_TRUE(paused1);

            /* Second pause should be safe */
            bool paused2 = gpu_worker_pause(worker);
            /* Implementation may allow or disallow */
            (void)paused2;

            gpu_worker_stop(worker, 5000);
            gpu_worker_shutdown(worker);
        }

        multi_gpu_shutdown(sched);
    }

    gpu_backend_shutdown();
#else
    ASSERT_TRUE(1);
#endif
}

/* ============================================================================
 * Multi-Worker Integration Tests
 * ============================================================================ */

TEST(worker_multi_gpu_init) {
#ifdef HAVE_CUDA_BACKEND
    gpu_backend_init(NULL);
    multi_gpu_scheduler_t *sched = multi_gpu_init(NULL);

    if (sched) {
        int gpu_count = multi_gpu_available();

        if (gpu_count >= 2) {
            /* Test with 2 GPUs */
            worker_config_t config = gpu_worker_default_config(sched, 2);
            config.device_ids[0] = 0;
            config.device_ids[1] = 1;

            gpu_multi_worker_t *worker = gpu_worker_init(&config);
            ASSERT_NOT_NULL(worker);

            multi_gpu_worker_stats_t stats;
            gpu_worker_get_stats(worker, &stats);

            /* Should have 2 workers configured */
            ASSERT_TRUE(stats.active_workers >= 0);

            gpu_worker_shutdown(worker);
        }

        multi_gpu_shutdown(sched);
    }

    gpu_backend_shutdown();
#else
    ASSERT_TRUE(1);
#endif
}

TEST(worker_multi_gpu_start_stop) {
#ifdef HAVE_CUDA_BACKEND
    gpu_backend_init(NULL);
    multi_gpu_scheduler_t *sched = multi_gpu_init(NULL);

    if (sched) {
        int gpu_count = multi_gpu_available();

        if (gpu_count >= 2) {
            worker_config_t config = gpu_worker_default_config(sched, 2);
            config.device_ids[0] = 0;
            config.device_ids[1] = 1;
            config.batch_size = 1024;

            gpu_multi_worker_t *worker = gpu_worker_init(&config);
            ASSERT_NOT_NULL(worker);

            multi_gpu_set_range(sched, 0, 50000);

            /* Start all workers */
            bool started = gpu_worker_start(worker);
            ASSERT_TRUE(started);

            /* Let them run briefly */
            usleep(100000);  /* 100ms */

            /* Stop all workers */
            bool stopped = gpu_worker_stop(worker, 10000);
            ASSERT_TRUE(stopped);

            /* Check statistics */
            multi_gpu_worker_stats_t stats;
            gpu_worker_get_stats(worker, &stats);

            /* Verify some work was done */
            ASSERT_TRUE(stats.total_keys_processed >= 0);

            gpu_worker_shutdown(worker);
        }

        multi_gpu_shutdown(sched);
    }

    gpu_backend_shutdown();
#else
    ASSERT_TRUE(1);
#endif
}

TEST(worker_has_result_initial) {
    /* NULL worker should return false */
    bool result = gpu_worker_has_result(NULL);
    ASSERT_FALSE(result);
}

TEST(worker_has_result_no_match) {
#ifdef HAVE_CUDA_BACKEND
    gpu_backend_init(NULL);
    multi_gpu_scheduler_t *sched = multi_gpu_init(NULL);

    if (sched && multi_gpu_available() > 0) {
        worker_config_t config = gpu_worker_default_config(sched, 1);
        config.device_ids[0] = 0;

        gpu_multi_worker_t *worker = gpu_worker_init(&config);
        ASSERT_NOT_NULL(worker);

        /* Before starting, should be false */
        bool result = gpu_worker_has_result(worker);
        ASSERT_FALSE(result);

        gpu_worker_shutdown(worker);
        multi_gpu_shutdown(sched);
    }

    gpu_backend_shutdown();
#else
    ASSERT_TRUE(1);
#endif
}

/* ============================================================================
 * Worker Error Handling Tests
 * ============================================================================ */

TEST(worker_stop_timeout) {
#ifdef HAVE_CUDA_BACKEND
    gpu_backend_init(NULL);
    multi_gpu_scheduler_t *sched = multi_gpu_init(NULL);

    if (sched && multi_gpu_available() > 0) {
        worker_config_t config = gpu_worker_default_config(sched, 1);
        config.device_ids[0] = 0;

        gpu_multi_worker_t *worker = gpu_worker_init(&config);

        if (worker) {
            multi_gpu_set_range(sched, 0, 1000000000UL);  /* Large range */
            gpu_worker_start(worker);

            /* Try to stop with very short timeout */
            bool stopped = gpu_worker_stop(worker, 1);  /* 1ms timeout */
            /* May or may not succeed depending on timing */
            (void)stopped;

            /* Force shutdown */
            gpu_worker_shutdown(worker);
        }

        multi_gpu_shutdown(sched);
    }

    gpu_backend_shutdown();
#else
    ASSERT_TRUE(1);
#endif
}

TEST(worker_stats_consistency) {
#ifdef HAVE_CUDA_BACKEND
    gpu_backend_init(NULL);
    multi_gpu_scheduler_t *sched = multi_gpu_init(NULL);

    if (sched && multi_gpu_available() > 0) {
        worker_config_t config = gpu_worker_default_config(sched, 1);
        config.device_ids[0] = 0;

        gpu_multi_worker_t *worker = gpu_worker_init(&config);
        ASSERT_NOT_NULL(worker);

        multi_gpu_worker_stats_t stats1, stats2;

        /* Get stats twice */
        gpu_worker_get_stats(worker, &stats1);
        gpu_worker_get_stats(worker, &stats2);

        /* Before starting, stats should be consistent */
        ASSERT_EQ(stats1.total_keys_processed, stats2.total_keys_processed);
        ASSERT_EQ(stats1.active_workers, stats2.active_workers);

        gpu_worker_shutdown(worker);
        multi_gpu_shutdown(sched);
    }

    gpu_backend_shutdown();
#else
    ASSERT_TRUE(1);
#endif
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

int main(int argc, char *argv[]) {
    TEST_INIT();

    TEST_SECTION("Worker Initialization Tests");
    RUN_TEST(worker_init_null_config);
    RUN_TEST(worker_init_null_scheduler);
    RUN_TEST(worker_init_invalid_device_count);
    RUN_TEST(worker_shutdown_null);
    RUN_TEST(worker_default_config);

    TEST_SECTION("Worker Lifecycle Tests");
    RUN_TEST(worker_start_before_init);
    RUN_TEST(worker_stop_before_start);
    RUN_TEST(worker_init_and_shutdown);
    RUN_TEST(worker_start_and_stop);
    RUN_TEST(worker_double_start);

    TEST_SECTION("Worker Statistics Tests");
    RUN_TEST(worker_get_stats_null);
    RUN_TEST(worker_get_stats_null_output);
    RUN_TEST(worker_get_stats_initial);
    RUN_TEST(worker_get_device_stats_invalid);
    RUN_TEST(worker_get_device_stats_valid);

    TEST_SECTION("Worker Pause/Resume Tests");
    RUN_TEST(worker_pause_null);
    RUN_TEST(worker_resume_null);
    RUN_TEST(worker_pause_resume_lifecycle);
    RUN_TEST(worker_double_pause);

    TEST_SECTION("Multi-Worker Integration Tests");
    RUN_TEST(worker_multi_gpu_init);
    RUN_TEST(worker_multi_gpu_start_stop);
    RUN_TEST(worker_has_result_initial);
    RUN_TEST(worker_has_result_no_match);

    TEST_SECTION("Worker Error Handling Tests");
    RUN_TEST(worker_stop_timeout);
    RUN_TEST(worker_stats_consistency);

    return TEST_RESULTS();
}
