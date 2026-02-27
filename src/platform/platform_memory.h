/*
 * Platform abstraction layer - Memory operations
 *
 * Cross-platform process memory usage tracking for progress monitoring.
 * Provides unified API wrapping Windows GetProcessMemoryInfo and Linux /proc/self/status.
 */

#ifndef PLATFORM_MEMORY_H
#define PLATFORM_MEMORY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get current process memory usage (RSS - Resident Set Size) in megabytes.
 *
 * Windows implementation:
 *   - Uses GetProcessMemoryInfo from psapi.h
 *   - Returns WorkingSetSize (physical memory used by process)
 *   - Converts bytes to MB
 *
 * Linux implementation:
 *   - Parses /proc/self/status to read VmRSS field
 *   - VmRSS is already in KB, so converts KB to MB
 *   - Falls back to 0 if /proc/self/status cannot be read
 *
 * The returned value represents physical memory currently used by the process.
 * This is useful for monitoring memory consumption in BSGS mode.
 *
 * @return Current process memory usage in MB, or 0 if detection fails.
 */
uint64_t platform_memory_usage_mb(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_MEMORY_H */
