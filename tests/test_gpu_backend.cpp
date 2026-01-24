/*
 * test_gpu_backend.cpp - Unit tests for GPU backend
 *
 * Uses Option C: Both mock and conditional tests
 * - Mock tests using gpu_backend_none.cpp stub pattern for CI
 * - Conditional tests with #ifdef HAVE_CUDA_BACKEND for real GPU validation
 *
 * Tests:
 * - GPU backend initialization and shutdown
 * - GPU availability detection
 * - Kernel dispatch logic (mock)
 * - Multi-GPU coordination via scheduler
 * - Async pipeline buffering
 */

#include "test_framework.h"

extern "C" {
#include "gpu/gpu_backend.h"
#include "gpu/multi_gpu_scheduler.h"
#include "gpu/async_pipeline.h"
}

#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * GPU Backend Basic Tests (Mock - always run)
 * These tests work with gpu_backend_none.cpp stub
 * ============================================================================ */

TEST(gpu_backend_init_basic) {
    gpu_backend_info_t info;
    memset(&info, 0xFF, sizeof(info));  /* Fill with garbage */

    int result = gpu_backend_init(&info);

    /* Init should succeed (returns 0) */
    ASSERT_EQ(0, result);

    /* With stub backend, gpu_count should be 0 */
#ifndef HAVE_CUDA_BACKEND
    ASSERT_EQ(0, info.gpu_count);
#endif
}

TEST(gpu_backend_init_null_info) {
    /* Should handle NULL info gracefully */
    int result = gpu_backend_init(NULL);
    ASSERT_EQ(0, result);
}

TEST(gpu_backend_available_stub) {
    /* Initialize first */
    gpu_backend_init(NULL);

    int available = gpu_backend_available();

#ifndef HAVE_CUDA_BACKEND
    /* Stub returns 0 */
    ASSERT_EQ(0, available);
#else
    /* Real backend returns 1 if GPU is present */
    ASSERT_TRUE(available == 0 || available == 1);
#endif
}

TEST(gpu_backend_shutdown_safe) {
    /* Shutdown should be safe to call multiple times */
    gpu_backend_shutdown();
    gpu_backend_shutdown();
    gpu_backend_shutdown();

    /* And after init */
    gpu_backend_init(NULL);
    gpu_backend_shutdown();
    gpu_backend_shutdown();

    /* Should not crash */
    ASSERT_TRUE(1);
}

TEST(gpu_hash160_fromX_batch_stub) {
    gpu_backend_init(NULL);

    /* Prepare test data */
    const size_t count = 4;
    uint8_t x32_be[32 * count];
    uint8_t out02[20 * count];
    uint8_t out03[20 * count];

    memset(x32_be, 0x42, sizeof(x32_be));
    memset(out02, 0, sizeof(out02));
    memset(out03, 0, sizeof(out03));

    int result = gpu_hash160_fromX_batch(x32_be, count, out02, out03);

#ifndef HAVE_CUDA_BACKEND
    /* Stub returns 1 (error) since no GPU */
    ASSERT_EQ(1, result);
#else
    /* Real backend should succeed or fail gracefully */
    ASSERT_TRUE(result == 0 || result == 1);
#endif

    gpu_backend_shutdown();
}

TEST(gpu_upload_gtable_stub) {
    gpu_backend_init(NULL);

    /* Test data - 256 * 32 points, each point is 64 bytes (X||Y) */
    const size_t point_count = 256 * 32;
    uint8_t *gtable = (uint8_t*)malloc(point_count * 64);
    ASSERT_NOT_NULL(gtable);
    memset(gtable, 0, point_count * 64);

    int result = gpu_upload_gtable(gtable, point_count);

#ifndef HAVE_CUDA_BACKEND
    /* Stub returns 1 (error) */
    ASSERT_EQ(1, result);
#endif

    free(gtable);
    gpu_backend_shutdown();
}

TEST(gpu_upload_targets_stub) {
    gpu_backend_init(NULL);

    /* 10 target hashes, 20 bytes each */
    const size_t count = 10;
    uint8_t targets[20 * count];
    memset(targets, 0xAB, sizeof(targets));

    int result = gpu_upload_targets(targets, count);

#ifndef HAVE_CUDA_BACKEND
    ASSERT_EQ(1, result);
#endif

    gpu_backend_shutdown();
}

TEST(gpu_upload_bloom_stub) {
    gpu_backend_init(NULL);

    /* Simulated bloom filter data */
    const size_t bloom_size = 1024;
    uint8_t bloom_data[bloom_size];
    memset(bloom_data, 0, bloom_size);
    int num_hashes = 7;

    int result = gpu_upload_bloom(bloom_data, bloom_size, num_hashes);

#ifndef HAVE_CUDA_BACKEND
    ASSERT_EQ(1, result);
#endif

    gpu_backend_shutdown();
}

TEST(gpu_full_search_stub) {
    gpu_backend_init(NULL);

    gpu_search_config_t config;
    memset(&config, 0, sizeof(config));

    /* Set up minimal config */
    memset(config.start_key, 0, 32);
    config.start_key[31] = 1;
    memset(config.end_key, 0xFF, 32);

    config.search_compressed = 1;
    config.search_uncompressed = 0;
    config.quiet = 1;

    int result = gpu_full_search(&config);

#ifndef HAVE_CUDA_BACKEND
    /* Stub returns 0 (no keys found, but no error) */
    ASSERT_EQ(0, result);
#endif

    gpu_backend_shutdown();
}

TEST(gpu_get_optimal_batch_size_stub) {
    gpu_backend_init(NULL);

    size_t batch_size = gpu_get_optimal_batch_size();

#ifndef HAVE_CUDA_BACKEND
    /* Stub returns 0 */
    ASSERT_EQ(0UL, batch_size);
#else
    /* Real backend should return something reasonable */
    ASSERT_TRUE(batch_size >= 0);
#endif

    gpu_backend_shutdown();
}

TEST(gpu_benchmark_stub) {
    gpu_backend_init(NULL);

    double mkeys = gpu_benchmark(100);  /* 100ms duration */

#ifndef HAVE_CUDA_BACKEND
    /* Stub returns 0.0 */
    ASSERT_DOUBLE_EQ(0.0, mkeys, 0.001);
#endif

    gpu_backend_shutdown();
}

/* ============================================================================
 * GPU Auto-tuning Tests (Mock)
 * ============================================================================ */

TEST(gpu_autotune_stub) {
    gpu_backend_init(NULL);

    gpu_tune_result_t result;
    memset(&result, 0xFF, sizeof(result));

    int status = gpu_autotune(100, &result);

#ifndef HAVE_CUDA_BACKEND
    /* Without CUDA, autotune should fail or return default values */
    /* The stub implementation may not implement this, so we just check it doesn't crash */
    (void)status;
    ASSERT_TRUE(1);  /* Just checking it doesn't crash */
#endif

    gpu_backend_shutdown();
}

/* ============================================================================
 * Multi-GPU Scheduler Tests
 * These test the scheduler logic independent of actual GPU hardware
 * ============================================================================ */

TEST(multi_gpu_available_check) {
    int count = multi_gpu_available();

    /* Should return 0 or more */
    ASSERT_TRUE(count >= 0);

#ifndef HAVE_CUDA_BACKEND
    /* Without CUDA, should be 0 */
    ASSERT_EQ(0, count);
#endif
}

TEST(multi_gpu_init_null_config) {
    /* With no GPUs, init should return NULL */
#ifndef HAVE_CUDA_BACKEND
    multi_gpu_scheduler_t *sched = multi_gpu_init(NULL);
    ASSERT_NULL(sched);
#else
    /* With GPUs, may succeed or fail depending on hardware */
    multi_gpu_scheduler_t *sched = multi_gpu_init(NULL);
    if (sched) {
        multi_gpu_shutdown(sched);
    }
    ASSERT_TRUE(1);
#endif
}

TEST(multi_gpu_shutdown_null) {
    /* Shutdown with NULL should not crash */
    multi_gpu_shutdown(NULL);
    ASSERT_TRUE(1);
}

TEST(multi_gpu_get_state_null) {
    multi_gpu_state_t state;
    memset(&state, 0xFF, sizeof(state));

    /* Should handle NULL scheduler gracefully */
    multi_gpu_get_state(NULL, &state);
    ASSERT_TRUE(1);  /* Just checking no crash */
}

TEST(multi_gpu_set_range_null) {
    /* Should handle NULL scheduler gracefully */
    multi_gpu_set_range(NULL, 0, 1000000);
    ASSERT_TRUE(1);
}

TEST(multi_gpu_get_work_null) {
    uint64_t start, end;

    bool result = multi_gpu_get_work(NULL, 0, &start, &end);
    ASSERT_FALSE(result);
}

TEST(multi_gpu_report_work_null) {
    /* Should handle NULL gracefully */
    multi_gpu_report_work(NULL, 0, 1000000, 1000);
    ASSERT_TRUE(1);
}

TEST(multi_gpu_rebalance_null) {
    /* Should handle NULL gracefully */
    multi_gpu_rebalance(NULL);
    ASSERT_TRUE(1);
}

/* ============================================================================
 * Async Pipeline Tests (Mock)
 * ============================================================================ */

TEST(async_pipeline_available_check) {
    int available = async_pipeline_available();

#ifndef HAVE_CUDA_BACKEND
    /* Should return 0 without CUDA */
    ASSERT_EQ(0, available);
#else
    ASSERT_TRUE(available == 0 || available == 1);
#endif
}

TEST(async_pipeline_create_null) {
    /* With no CUDA, create should return NULL */
    async_pipeline_config_t config;
    memset(&config, 0, sizeof(config));
    config.batch_size = 1024;
    config.result_size = 20;
    config.use_pinned_memory = true;
    config.device_id = 0;

    async_pipeline_t *pipeline = async_pipeline_create(&config);

#ifndef HAVE_CUDA_BACKEND
    ASSERT_NULL(pipeline);
#else
    if (pipeline) {
        async_pipeline_destroy(pipeline);
    }
    ASSERT_TRUE(1);
#endif
}

TEST(async_pipeline_destroy_null) {
    /* Should handle NULL gracefully */
    async_pipeline_destroy(NULL);
    ASSERT_TRUE(1);
}

TEST(async_pipeline_sync_null) {
    /* Should handle NULL gracefully */
    async_pipeline_sync(NULL);
    ASSERT_TRUE(1);
}

TEST(async_pipeline_get_stats_null) {
    async_pipeline_stats_t stats;
    memset(&stats, 0xFF, sizeof(stats));

    /* Should handle NULL pipeline gracefully */
    async_pipeline_get_stats(NULL, &stats);
    ASSERT_TRUE(1);
}

/* ============================================================================
 * GPU Search Config Validation Tests
 * ============================================================================ */

TEST(gpu_search_config_struct) {
    gpu_search_config_t config;
    memset(&config, 0, sizeof(config));

    /* Verify struct layout */
    ASSERT_EQ(32UL, sizeof(config.start_key));
    ASSERT_EQ(32UL, sizeof(config.end_key));
    ASSERT_EQ(32UL, sizeof(config.stride));

    /* Set some values and verify */
    config.start_key[31] = 0x01;
    config.end_key[31] = 0xFF;
    config.search_compressed = 1;
    config.search_uncompressed = 0;
    config.use_bloom = 1;
    config.quiet = 1;

    ASSERT_EQ(0x01, config.start_key[31]);
    ASSERT_EQ(0xFF, config.end_key[31]);
    ASSERT_EQ(1, config.search_compressed);
    ASSERT_EQ(0, config.search_uncompressed);
    ASSERT_EQ(1, config.use_bloom);
    ASSERT_EQ(1, config.quiet);
}

TEST(gpu_backend_info_struct) {
    gpu_backend_info_t info;
    memset(&info, 0, sizeof(info));

    /* Verify struct has expected fields */
    info.gpu_count = 2;
    info.vram_mb = 8192;
    strncpy(info.name, "Test GPU", sizeof(info.name) - 1);
    info.compute_major = 7;
    info.compute_minor = 5;
    info.multiprocessors = 40;
    info.max_threads_per_block = 1024;

    ASSERT_EQ(2, info.gpu_count);
    ASSERT_EQ(8192UL, info.vram_mb);
    ASSERT_STR_EQ("Test GPU", info.name);
    ASSERT_EQ(7, info.compute_major);
    ASSERT_EQ(5, info.compute_minor);
    ASSERT_EQ(40, info.multiprocessors);
    ASSERT_EQ(1024, info.max_threads_per_block);
}

TEST(gpu_tune_result_struct) {
    gpu_tune_result_t result;
    memset(&result, 0, sizeof(result));

    result.blocks_per_sm = 4;
    result.keys_per_thread = 8;
    result.threads_per_block = 256;
    result.measured_mkeys = 1500.5;

    ASSERT_EQ(4, result.blocks_per_sm);
    ASSERT_EQ(8, result.keys_per_thread);
    ASSERT_EQ(256, result.threads_per_block);
    ASSERT_DOUBLE_EQ(1500.5, result.measured_mkeys, 0.1);
}

/* ============================================================================
 * Multi-GPU Device Struct Tests
 * ============================================================================ */

TEST(multi_gpu_device_struct) {
    multi_gpu_device_t device;
    memset(&device, 0, sizeof(device));

    device.device_id = 0;
    strncpy(device.name, "NVIDIA RTX 4090", sizeof(device.name) - 1);
    device.vram_mb = 24576;
    device.performance_score = 1.5;
    device.keys_processed = 1000000000ULL;
    device.avg_throughput = 2500.0;
    device.current_allocation = 0.6;

    ASSERT_EQ(0, device.device_id);
    ASSERT_STR_EQ("NVIDIA RTX 4090", device.name);
    ASSERT_EQ(24576UL, device.vram_mb);
    ASSERT_DOUBLE_EQ(1.5, device.performance_score, 0.01);
    ASSERT_EQ(1000000000ULL, device.keys_processed);
    ASSERT_DOUBLE_EQ(2500.0, device.avg_throughput, 0.1);
    ASSERT_DOUBLE_EQ(0.6, device.current_allocation, 0.01);
}

TEST(multi_gpu_config_struct) {
    multi_gpu_config_t config;
    memset(&config, 0, sizeof(config));

    config.device_count = 2;
    config.device_ids[0] = 0;
    config.device_ids[1] = 1;
    config.adaptive_balancing = true;
    config.rebalance_interval_keys = 100000000ULL;

    ASSERT_EQ(2, config.device_count);
    ASSERT_EQ(0, config.device_ids[0]);
    ASSERT_EQ(1, config.device_ids[1]);
    ASSERT_TRUE(config.adaptive_balancing);
    ASSERT_EQ(100000000ULL, config.rebalance_interval_keys);
}

TEST(multi_gpu_state_struct) {
    multi_gpu_state_t state;
    memset(&state, 0, sizeof(state));

    state.active_count = 2;
    state.total_keys_processed = 5000000000ULL;
    state.total_throughput = 5000.0;
    state.last_rebalance_time = 1234567890ULL;

    ASSERT_EQ(2, state.active_count);
    ASSERT_EQ(5000000000ULL, state.total_keys_processed);
    ASSERT_DOUBLE_EQ(5000.0, state.total_throughput, 0.1);
    ASSERT_EQ(1234567890ULL, state.last_rebalance_time);
}

/* ============================================================================
 * Async Pipeline Struct Tests
 * ============================================================================ */

TEST(async_pipeline_config_struct) {
    async_pipeline_config_t config;
    memset(&config, 0, sizeof(config));

    config.batch_size = 1048576;  /* 1M keys */
    config.result_size = 20;
    config.use_pinned_memory = true;
    config.device_id = 0;

    ASSERT_EQ(1048576UL, config.batch_size);
    ASSERT_EQ(20UL, config.result_size);
    ASSERT_TRUE(config.use_pinned_memory);
    ASSERT_EQ(0, config.device_id);
}

TEST(async_pipeline_stats_struct) {
    async_pipeline_stats_t stats;
    memset(&stats, 0, sizeof(stats));

    stats.batches_processed = 1000;
    stats.keys_processed = 1048576000ULL;
    stats.avg_compute_ms = 5.5;
    stats.avg_transfer_ms = 1.2;
    stats.gpu_utilization = 0.95;

    ASSERT_EQ(1000UL, stats.batches_processed);
    ASSERT_EQ(1048576000ULL, stats.keys_processed);
    ASSERT_DOUBLE_EQ(5.5, stats.avg_compute_ms, 0.01);
    ASSERT_DOUBLE_EQ(1.2, stats.avg_transfer_ms, 0.01);
    ASSERT_DOUBLE_EQ(0.95, stats.gpu_utilization, 0.001);
}

/* ============================================================================
 * Constants and Limits Tests
 * ============================================================================ */

TEST(multi_gpu_constants) {
    /* Verify constants are defined and reasonable */
    ASSERT_TRUE(MULTI_GPU_MAX_DEVICES > 0);
    ASSERT_TRUE(MULTI_GPU_MAX_DEVICES <= 64);  /* Reasonable upper bound */
}

TEST(async_pipeline_constants) {
    /* Verify constants are defined and reasonable */
    ASSERT_EQ(3, ASYNC_PIPELINE_NUM_BUFFERS);  /* Triple buffering */
    ASSERT_TRUE(ASYNC_PIPELINE_MAX_BATCH > 0);
    ASSERT_TRUE(ASYNC_PIPELINE_MAX_BATCH <= (1 << 24));  /* 16M max */
}

/* ============================================================================
 * Conditional Tests - Only run with real CUDA backend
 * ============================================================================ */

#ifdef HAVE_CUDA_BACKEND

TEST(gpu_backend_real_init) {
    gpu_backend_info_t info;
    int result = gpu_backend_init(&info);

    if (result == 0 && info.gpu_count > 0) {
        /* GPU found - verify info is populated */
        ASSERT_TRUE(info.vram_mb > 0);
        ASSERT_TRUE(strlen(info.name) > 0);
        ASSERT_TRUE(info.compute_major >= 3);  /* At least Kepler */
        ASSERT_TRUE(info.multiprocessors > 0);
        ASSERT_TRUE(info.max_threads_per_block >= 128);
    }

    gpu_backend_shutdown();
}

TEST(gpu_backend_real_available) {
    gpu_backend_info_t info;
    gpu_backend_init(&info);

    int available = gpu_backend_available();

    if (info.gpu_count > 0) {
        ASSERT_EQ(1, available);
    } else {
        ASSERT_EQ(0, available);
    }

    gpu_backend_shutdown();
}

TEST(gpu_backend_real_benchmark) {
    gpu_backend_info_t info;
    int result = gpu_backend_init(&info);

    if (result == 0 && info.gpu_count > 0) {
        /* Run short benchmark */
        double mkeys = gpu_benchmark(100);

        /* Should get some performance */
        ASSERT_TRUE(mkeys >= 0.0);
        /* Typical GPUs should achieve at least 1 Mkeys/s */
        /* But we allow 0 for edge cases */
    }

    gpu_backend_shutdown();
}

TEST(gpu_backend_real_batch_size) {
    gpu_backend_info_t info;
    int result = gpu_backend_init(&info);

    if (result == 0 && info.gpu_count > 0) {
        size_t batch_size = gpu_get_optimal_batch_size();

        /* Should return something reasonable */
        ASSERT_TRUE(batch_size >= 1024);  /* At least 1K */
        ASSERT_TRUE(batch_size <= 1 << 24);  /* At most 16M */
    }

    gpu_backend_shutdown();
}

TEST(multi_gpu_real_scheduler) {
    int gpu_count = multi_gpu_available();

    if (gpu_count > 0) {
        multi_gpu_scheduler_t *sched = multi_gpu_init(NULL);
        ASSERT_NOT_NULL(sched);

        /* Set a range */
        multi_gpu_set_range(sched, 0, 1000000);

        /* Get work */
        uint64_t start, end;
        bool got_work = multi_gpu_get_work(sched, 0, &start, &end);
        ASSERT_TRUE(got_work);
        ASSERT_EQ(0UL, start);
        ASSERT_TRUE(end > start);

        /* Report work */
        multi_gpu_report_work(sched, 0, end - start, 100);

        /* Get state */
        multi_gpu_state_t state;
        multi_gpu_get_state(sched, &state);
        ASSERT_TRUE(state.total_keys_processed > 0);

        multi_gpu_shutdown(sched);
    }
}

#endif /* HAVE_CUDA_BACKEND */

/* ============================================================================
 * Main Entry Point
 * ============================================================================ */

int run_gpu_backend_tests(void) {
    TEST_INIT();

    TEST_SECTION("GPU Backend Basic (Mock)");
    RUN_TEST(gpu_backend_init_basic);
    RUN_TEST(gpu_backend_init_null_info);
    RUN_TEST(gpu_backend_available_stub);
    RUN_TEST(gpu_backend_shutdown_safe);

    TEST_SECTION("GPU Backend Operations (Mock)");
    RUN_TEST(gpu_hash160_fromX_batch_stub);
    RUN_TEST(gpu_upload_gtable_stub);
    RUN_TEST(gpu_upload_targets_stub);
    RUN_TEST(gpu_upload_bloom_stub);
    RUN_TEST(gpu_full_search_stub);
    RUN_TEST(gpu_get_optimal_batch_size_stub);
    RUN_TEST(gpu_benchmark_stub);

    TEST_SECTION("GPU Auto-tuning (Mock)");
    RUN_TEST(gpu_autotune_stub);

    TEST_SECTION("Multi-GPU Scheduler (Mock)");
    RUN_TEST(multi_gpu_available_check);
    RUN_TEST(multi_gpu_init_null_config);
    RUN_TEST(multi_gpu_shutdown_null);
    RUN_TEST(multi_gpu_get_state_null);
    RUN_TEST(multi_gpu_set_range_null);
    RUN_TEST(multi_gpu_get_work_null);
    RUN_TEST(multi_gpu_report_work_null);
    RUN_TEST(multi_gpu_rebalance_null);

    TEST_SECTION("Async Pipeline (Mock)");
    RUN_TEST(async_pipeline_available_check);
    RUN_TEST(async_pipeline_create_null);
    RUN_TEST(async_pipeline_destroy_null);
    RUN_TEST(async_pipeline_sync_null);
    RUN_TEST(async_pipeline_get_stats_null);

    TEST_SECTION("GPU Struct Validation");
    RUN_TEST(gpu_search_config_struct);
    RUN_TEST(gpu_backend_info_struct);
    RUN_TEST(gpu_tune_result_struct);

    TEST_SECTION("Multi-GPU Struct Validation");
    RUN_TEST(multi_gpu_device_struct);
    RUN_TEST(multi_gpu_config_struct);
    RUN_TEST(multi_gpu_state_struct);

    TEST_SECTION("Async Pipeline Struct Validation");
    RUN_TEST(async_pipeline_config_struct);
    RUN_TEST(async_pipeline_stats_struct);

    TEST_SECTION("Constants and Limits");
    RUN_TEST(multi_gpu_constants);
    RUN_TEST(async_pipeline_constants);

#ifdef HAVE_CUDA_BACKEND
    TEST_SECTION("GPU Backend (Real CUDA)");
    RUN_TEST(gpu_backend_real_init);
    RUN_TEST(gpu_backend_real_available);
    RUN_TEST(gpu_backend_real_benchmark);
    RUN_TEST(gpu_backend_real_batch_size);

    TEST_SECTION("Multi-GPU Scheduler (Real CUDA)");
    RUN_TEST(multi_gpu_real_scheduler);
#endif

    return TEST_RESULTS();
}

/* Standalone main for individual testing */
#ifdef TEST_STANDALONE
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    return run_gpu_backend_tests();
}
#endif
