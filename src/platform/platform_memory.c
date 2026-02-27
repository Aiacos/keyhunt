/*
 * Platform abstraction layer - Memory operations implementation
 *
 * Cross-platform process memory usage tracking for progress monitoring.
 * Provides unified API wrapping Windows GetProcessMemoryInfo and Linux /proc/self/status.
 */

#include "platform_memory.h"
#include "platform_types.h"

#if PLATFORM_WINDOWS
    #include <windows.h>
    #include <psapi.h>
#else
    #include <stdio.h>
    #include <string.h>
    #include <stdint.h>
#endif

/**
 * Get current process memory usage (RSS - Resident Set Size) in megabytes.
 *
 * Windows implementation:
 *   - Uses GetProcessMemoryInfo from psapi.h to get PROCESS_MEMORY_COUNTERS
 *   - Returns WorkingSetSize which is the physical memory used by the process
 *   - Converts bytes to megabytes by dividing by (1024 * 1024)
 *
 * Linux implementation:
 *   - Parses /proc/self/status to read VmRSS field
 *   - VmRSS is reported in KB by the kernel
 *   - Converts KB to MB by dividing by 1024
 *   - Returns 0 if file cannot be opened or VmRSS field not found
 *
 * The returned value represents physical memory currently used by the process.
 * This is useful for monitoring memory consumption in BSGS mode which can use
 * significant RAM for bloom filters and precomputed tables.
 */
uint64_t platform_memory_usage_mb(void)
{
#if PLATFORM_WINDOWS
    PROCESS_MEMORY_COUNTERS pmc;

    /* Get current process handle */
    HANDLE hProcess = GetCurrentProcess();

    /* Query memory information */
    if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
        /* Convert WorkingSetSize (bytes) to MB */
        uint64_t memory_mb = pmc.WorkingSetSize / (1024ULL * 1024ULL);
        return memory_mb;
    }

    /* Fallback if GetProcessMemoryInfo fails */
    return 0;

#else
    /* Linux: Parse /proc/self/status to get VmRSS */
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) {
        /* Fallback if /proc/self/status cannot be opened */
        return 0;
    }

    char line[256];
    uint64_t vmrss_kb = 0;

    /* Read file line by line looking for VmRSS field */
    while (fgets(line, sizeof(line), f)) {
        /* VmRSS line format: "VmRSS:    12345 kB" */
        if (strncmp(line, "VmRSS:", 6) == 0) {
            /* Parse the integer value (in KB) */
            if (sscanf(line + 6, "%llu", (unsigned long long *)&vmrss_kb) == 1) {
                break;
            }
        }
    }

    fclose(f);

    /* Convert KB to MB */
    uint64_t memory_mb = vmrss_kb / 1024ULL;
    return memory_mb;
#endif
}
