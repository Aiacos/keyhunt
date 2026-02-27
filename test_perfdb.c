// Minimal test for perfdb database persistence
#include "src/database/perfdb.h"
#include "src/core/sysinfo.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main() {
    printf("Testing perfdb database persistence...\n\n");

    // Test 1: Save first benchmark
    printf("[Test 1] Saving first benchmark...\n");

    perfdb_benchmark_t bench1;
    memset(&bench1, 0, sizeof(perfdb_benchmark_t));

    strncpy(bench1.mode, "test_mode", sizeof(bench1.mode) - 1);
    bench1.bits = 66;
    strncpy(bench1.key_type, "both", sizeof(bench1.key_type) - 1);
    bench1.cpu_speed_mkeys = 10.5;
    bench1.gpu_speed_mkeys = 50.2;
    bench1.hybrid_speed_mkeys = 58.7;
    bench1.efficiency_ratio = 0.95;

    // Get real system info
    system_info_t sysinfo;
    sysinfo_init(&sysinfo);
    memcpy(&bench1.hardware, &sysinfo, sizeof(system_info_t));

    perfdb_compute_hardware_hash(&sysinfo, bench1.hardware_hash, sizeof(bench1.hardware_hash));
    strncpy(bench1.keyhunt_version, "test_v1", sizeof(bench1.keyhunt_version) - 1);
    bench1.benchmark_duration_seconds = 5;
    strncpy(bench1.notes, "Test benchmark run 1", sizeof(bench1.notes) - 1);

    int result1 = perfdb_save_benchmark(&bench1);
    if (result1 == 0) {
        printf("✓ First benchmark saved successfully\n");
    } else {
        printf("✗ Failed to save first benchmark\n");
        return 1;
    }

    // Test 2: Save second benchmark
    printf("\n[Test 2] Saving second benchmark...\n");

    perfdb_benchmark_t bench2;
    memset(&bench2, 0, sizeof(perfdb_benchmark_t));

    strncpy(bench2.mode, "test_mode", sizeof(bench2.mode) - 1);
    bench2.bits = 66;
    strncpy(bench2.key_type, "both", sizeof(bench2.key_type) - 1);
    bench2.cpu_speed_mkeys = 11.2;
    bench2.gpu_speed_mkeys = 51.8;
    bench2.hybrid_speed_mkeys = 60.1;
    bench2.efficiency_ratio = 0.96;

    memcpy(&bench2.hardware, &sysinfo, sizeof(system_info_t));
    perfdb_compute_hardware_hash(&sysinfo, bench2.hardware_hash, sizeof(bench2.hardware_hash));
    strncpy(bench2.keyhunt_version, "test_v1", sizeof(bench2.keyhunt_version) - 1);
    bench2.benchmark_duration_seconds = 5;
    strncpy(bench2.notes, "Test benchmark run 2", sizeof(bench2.notes) - 1);

    int result2 = perfdb_save_benchmark(&bench2);
    if (result2 == 0) {
        printf("✓ Second benchmark saved successfully\n");
    } else {
        printf("✗ Failed to save second benchmark\n");
        return 1;
    }

    // Test 3: Verify database exists
    printf("\n[Test 3] Verifying database exists...\n");

    char db_path[512];
    perfdb_get_filepath(db_path, sizeof(db_path));
    printf("Database path: %s\n", db_path);

    if (perfdb_exists()) {
        printf("✓ Database file exists\n");
    } else {
        printf("✗ Database file not found\n");
        return 1;
    }

    // Test 4: Query recent results
    printf("\n[Test 4] Querying recent benchmarks...\n");

    perfdb_result_t results[10];
    int count = perfdb_query_recent(results, 10, NULL);

    if (count >= 2) {
        printf("✓ Found %d benchmark(s) in database (expected at least 2)\n", count);

        printf("\nRecent benchmarks:\n");
        for (int i = 0; i < count && i < 5; i++) {
            printf("  %d. Mode: %s, CPU: %.2f Mkeys/s, GPU: %.2f Mkeys/s, Hybrid: %.2f Mkeys/s\n",
                   i + 1, results[i].mode, results[i].cpu_speed_mkeys,
                   results[i].gpu_speed_mkeys, results[i].hybrid_speed_mkeys);
        }
    } else {
        printf("✗ Expected at least 2 benchmarks, found %d\n", count);
        return 1;
    }

    printf("\n✓ All tests passed! Database persistence works correctly.\n");
    return 0;
}
