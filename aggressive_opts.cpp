/*
 * Aggressive CPU optimizations for maximum performance
 * Phase 1: Quick wins (2-3× improvement target)
 */

#include "aggressive_opts.h"
#include <unistd.h>
#include <sched.h>
#include <pthread.h>
#include <stdio.h>

// Get optimal thread count based on CPU topology
int get_optimal_thread_count(void) {
    int physical_cores = sysconf(_SC_NPROCESSORS_ONLN);

    // For CPU-bound workload with shared cache:
    // Use physical cores, not hyperthreads
    // Hyperthreading helps with I/O but hurts CPU-intensive work

    // Try to detect hyperthreading
    #ifdef __linux__
    FILE *fp = fopen("/sys/devices/system/cpu/smt/active", "r");
    if (fp) {
        int smt_active = 0;
        if (fscanf(fp, "%d", &smt_active) == 1 && smt_active) {
            // SMT/Hyperthreading is active
            // Use half the logical cores (= physical cores)
            physical_cores = (physical_cores + 1) / 2;
        }
        fclose(fp);
    }
    #endif

    // Clamp to reasonable range
    if (physical_cores < 1) physical_cores = 1;
    if (physical_cores > 64) physical_cores = 64;

    return physical_cores;
}

// Set thread affinity to specific CPU core
void set_thread_affinity(int thread_id) {
    #ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);

    // Pin thread to specific core to maximize cache locality
    int num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    int cpu_id = thread_id % num_cpus;

    CPU_SET(cpu_id, &cpuset);

    int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (rc != 0) {
        // Affinity setting failed, continue anyway
        return;
    }
    #else
    (void)thread_id;  // Unused on non-Linux
    #endif
}

// Helper: Suggest optimal CPU_GRP_SIZE multipliers
uint32_t suggest_cpu_grp_multiplier(uint32_t base_size, size_t cache_size) {
    // For ModInv optimization:
    // Larger groups = better amortization
    // But diminishing returns after certain size

    if (cache_size >= 32 * 1024 * 1024) {
        // 32MB+ L3: can handle 16K points
        return (16384 / base_size);
    } else if (cache_size >= 16 * 1024 * 1024) {
        // 16MB+ L3: optimal at 8K
        return (8192 / base_size);
    } else if (cache_size >= 8 * 1024 * 1024) {
        // 8MB+ L3: optimal at 4K
        return (4096 / base_size);
    } else if (cache_size >= 2 * 1024 * 1024) {
        // 2MB+ L2: use 2K
        return (2048 / base_size);
    }

    // Default: no change
    return 1;
}
