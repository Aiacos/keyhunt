#include "parameter_validator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// ANSI color codes for terminal output (disabled on Windows by default)
#ifdef _WIN32
#define COLOR_RESET   ""
#define COLOR_GREEN   ""
#define COLOR_YELLOW  ""
#define COLOR_RED     ""
#define COLOR_BLUE    ""
#else
#define COLOR_RESET   "\033[0m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_RED     "\033[31m"
#define COLOR_BLUE    "\033[34m"
#endif

// Helper: Calculate BSGS memory requirement
static uint64_t calculate_bsgs_memory_mb(uint64_t n, int kfactor) {
    // M = sqrt(N)
    uint64_t m = (uint64_t)sqrt((double)n);

    // bloom1 size in bytes: (M * K) * 3.5
    uint64_t bloom1_bytes = (m * kfactor * 35) / 10;

    // bloom2 size: bloom1 / 32
    uint64_t bloom2_bytes = bloom1_bytes / 32;

    // bloom3 size: bloom1 / 1024
    uint64_t bloom3_bytes = bloom1_bytes / 1024;

    // bP table size: (M / 32 * K) * 16 bytes
    uint64_t bp_table_bytes = (m / 32 * kfactor) * 16;

    // Total in MB
    uint64_t total_bytes = bloom1_bytes + bloom2_bytes + bloom3_bytes + bp_table_bytes;
    return total_bytes / (1024 * 1024);
}

// Helper: Check if value is power of 2
static bool is_power_of_2(uint64_t n) {
    return n && !(n & (n - 1));
}

// Helper: Round up to next power of 2
static uint64_t next_power_of_2(uint64_t n) {
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n |= n >> 32;
    n++;
    return n;
}

int validate_threads(
    int user_threads,
    const system_info_t *sysinfo,
    param_validation_result_t *result
) {
    memset(result, 0, sizeof(param_validation_result_t));
    result->original_value = user_threads;
    result->suggested_value = sysinfo->recommended_threads;
    result->corrected_value = user_threads;

    // If user specified 0, use auto-tuned value
    if (user_threads == 0) {
        result->corrected_value = sysinfo->recommended_threads;
        result->status = PARAM_OK;
        snprintf(result->message, sizeof(result->message),
                "Using auto-tuned value: %d threads (optimal for %d logical cores)",
                result->corrected_value, sysinfo->cpu_logical_cores);
        result->applied_correction = true;
        return result->corrected_value;
    }

    // Validate against available cores
    if (user_threads > sysinfo->cpu_logical_cores * 2) {
        // Far too many threads
        result->status = PARAM_CORRECTED;
        result->corrected_value = sysinfo->cpu_logical_cores;
        snprintf(result->message, sizeof(result->message),
                "Requested %d threads exceeds available cores (%d). Auto-corrected to %d.",
                user_threads, sysinfo->cpu_logical_cores, result->corrected_value);
        result->applied_correction = true;
        return result->corrected_value;
    }

    if (user_threads > sysinfo->cpu_logical_cores) {
        // More threads than cores (may cause context switching overhead)
        result->status = PARAM_WARNING;
        result->corrected_value = user_threads; // Keep user value but warn
        snprintf(result->message, sizeof(result->message),
                "Using %d threads on %d cores may cause overhead. Optimal: %d",
                user_threads, sysinfo->cpu_logical_cores, sysinfo->recommended_threads);
        result->applied_correction = false;
        return result->corrected_value;
    }

    if (user_threads == sysinfo->recommended_threads) {
        // Perfect!
        result->status = PARAM_OK;
        snprintf(result->message, sizeof(result->message),
                "Thread count is optimal for your hardware");
        return result->corrected_value;
    }

    if (user_threads < sysinfo->recommended_threads) {
        // Using fewer threads than optimal (but not wrong)
        result->status = PARAM_SUBOPTIMAL;
        snprintf(result->message, sizeof(result->message),
                "Using %d threads. Could use up to %d for better performance.",
                user_threads, sysinfo->recommended_threads);
        return result->corrected_value;
    }

    // User value is reasonable
    result->status = PARAM_OK;
    snprintf(result->message, sizeof(result->message), "Thread count validated");
    return result->corrected_value;
}

uint64_t validate_n_value(
    uint64_t user_n,
    int kfactor,
    const system_info_t *sysinfo,
    param_validation_result_t *result
) {
    memset(result, 0, sizeof(param_validation_result_t));
    result->original_value = (int)(user_n >> 32); // Store high bits for display
    result->suggested_value = (int)(sysinfo->recommended_n >> 32);

    // If user_n is 0, use auto-tuned value
    if (user_n == 0) {
        result->corrected_value = (int)(sysinfo->recommended_n >> 32);
        result->status = PARAM_OK;
        snprintf(result->message, sizeof(result->message),
                "Using auto-tuned N value: 0x%llx (optimal for %llu MB RAM)",
                (unsigned long long)sysinfo->recommended_n,
                (unsigned long long)sysinfo->ram_available);
        result->applied_correction = true;
        return sysinfo->recommended_n;
    }

    // Calculate memory requirement for user's N
    uint64_t required_ram_mb = calculate_bsgs_memory_mb(user_n, kfactor);
    uint64_t available_ram_mb = (sysinfo->ram_available * 80) / 100; // Use 80% max

    // Check if N will fit in RAM
    if (required_ram_mb > available_ram_mb) {
        // N is too large - will cause OOM
        result->status = PARAM_CORRECTED;

        // Find largest safe N value
        uint64_t safe_n = sysinfo->recommended_n;
        uint64_t safe_ram = calculate_bsgs_memory_mb(safe_n, kfactor);

        result->corrected_value = (int)(safe_n >> 32);
        snprintf(result->message, sizeof(result->message),
                "N=0x%llx requires %llu MB but only %llu MB available. Auto-corrected to 0x%llx (%llu MB)",
                (unsigned long long)user_n, (unsigned long long)required_ram_mb,
                (unsigned long long)available_ram_mb, (unsigned long long)safe_n,
                (unsigned long long)safe_ram);
        result->applied_correction = true;
        return safe_n;
    }

    // Check if N is too small (inefficient)
    if (user_n < 0x1000000000ULL) { // Less than 2^36
        result->status = PARAM_WARNING;
        snprintf(result->message, sizeof(result->message),
                "N=0x%llx is very small. Consider larger N for better range coverage.",
                (unsigned long long)user_n);
        result->applied_correction = false;
        return user_n;
    }

    // Check if M = sqrt(N) is valid for algorithm
    uint64_t m = (uint64_t)sqrt((double)user_n);
    if (m * m != user_n) {
        // N is not a perfect square - round up
        result->status = PARAM_WARNING;
        uint64_t corrected_n = (m + 1) * (m + 1);
        snprintf(result->message, sizeof(result->message),
                "N=0x%llx is not a perfect square. Closest perfect square: 0x%llx",
                (unsigned long long)user_n, (unsigned long long)corrected_n);
        result->applied_correction = false;
        return user_n; // Don't auto-correct this, just warn
    }

    // Check if using less than 50% of available RAM (could use larger N)
    if (required_ram_mb < available_ram_mb / 2) {
        result->status = PARAM_SUBOPTIMAL;
        snprintf(result->message, sizeof(result->message),
                "N=0x%llx uses only %llu MB of %llu MB available. Could use larger N=0x%llx",
                (unsigned long long)user_n, (unsigned long long)required_ram_mb,
                (unsigned long long)available_ram_mb, (unsigned long long)sysinfo->recommended_n);
        return user_n;
    }

    // User N is valid and reasonable
    result->status = PARAM_OK;
    snprintf(result->message, sizeof(result->message),
            "N value validated: will use %llu MB RAM",
            (unsigned long long)required_ram_mb);
    return user_n;
}

int validate_kfactor(
    int user_kfactor,
    uint64_t n_value,
    const system_info_t *sysinfo,
    param_validation_result_t *result
) {
    memset(result, 0, sizeof(param_validation_result_t));
    result->original_value = user_kfactor;
    result->suggested_value = sysinfo->recommended_kfactor;
    result->corrected_value = user_kfactor;

    // If user_kfactor is 0, use auto-tuned value
    if (user_kfactor == 0) {
        result->corrected_value = sysinfo->recommended_kfactor;
        result->status = PARAM_OK;
        snprintf(result->message, sizeof(result->message),
                "Using auto-tuned K factor: %d (optimal for N=0x%llx)",
                result->corrected_value, (unsigned long long)n_value);
        result->applied_correction = true;
        return result->corrected_value;
    }

    // Validate K factor range
    if (user_kfactor < 128) {
        result->status = PARAM_CORRECTED;
        result->corrected_value = 128;
        snprintf(result->message, sizeof(result->message),
                "K factor %d is too small (min 128). Auto-corrected to %d",
                user_kfactor, result->corrected_value);
        result->applied_correction = true;
        return result->corrected_value;
    }

    if (user_kfactor > 8192) {
        result->status = PARAM_WARNING;
        snprintf(result->message, sizeof(result->message),
                "K factor %d is very large. May cause excessive memory usage.",
                user_kfactor);
        // Don't auto-correct - user may have a reason
        return user_kfactor;
    }

    // Check if K * N will fit in RAM
    uint64_t required_ram_mb = calculate_bsgs_memory_mb(n_value, user_kfactor);
    uint64_t available_ram_mb = (sysinfo->ram_available * 80) / 100;

    if (required_ram_mb > available_ram_mb) {
        // K factor too large for current N
        result->status = PARAM_CORRECTED;

        // Calculate maximum safe K factor
        int safe_k = sysinfo->recommended_kfactor;
        while (safe_k > 128) {
            uint64_t test_ram = calculate_bsgs_memory_mb(n_value, safe_k);
            if (test_ram <= available_ram_mb) break;
            safe_k /= 2;
        }

        result->corrected_value = safe_k;
        snprintf(result->message, sizeof(result->message),
                "K=%d with N=0x%llx requires %llu MB but only %llu MB available. Auto-corrected to K=%d",
                user_kfactor, (unsigned long long)n_value,
                (unsigned long long)required_ram_mb, (unsigned long long)available_ram_mb,
                result->corrected_value);
        result->applied_correction = true;
        return result->corrected_value;
    }

    // Recommend power of 2 values for better performance
    if (!is_power_of_2(user_kfactor) && user_kfactor > 256) {
        result->status = PARAM_SUBOPTIMAL;
        uint64_t suggested_k = next_power_of_2(user_kfactor);
        if (suggested_k > 8192) suggested_k = 8192;
        snprintf(result->message, sizeof(result->message),
                "K=%d works but K=%llu (power of 2) may perform better",
                user_kfactor, (unsigned long long)suggested_k);
        return user_kfactor;
    }

    // K factor is valid
    result->status = PARAM_OK;
    snprintf(result->message, sizeof(result->message), "K factor validated");
    return user_kfactor;
}

uint32_t validate_batch_size(
    uint32_t user_batch_size,
    const system_info_t *sysinfo,
    param_validation_result_t *result
) {
    memset(result, 0, sizeof(param_validation_result_t));
    result->original_value = user_batch_size;
    result->suggested_value = sysinfo->recommended_batch_size;
    result->corrected_value = user_batch_size;

    // Auto-tune if user specified 0
    if (user_batch_size == 0) {
        result->corrected_value = sysinfo->recommended_batch_size;
        result->status = PARAM_OK;
        snprintf(result->message, sizeof(result->message),
                "Using auto-tuned batch size: %u (optimal for L3 cache)",
                result->corrected_value);
        result->applied_correction = true;
        return result->corrected_value;
    }

    // Batch size must be multiple of 8 (for AVX2) and power of 2 is best
    if (user_batch_size % 8 != 0) {
        result->status = PARAM_CORRECTED;
        result->corrected_value = ((user_batch_size + 7) / 8) * 8;
        snprintf(result->message, sizeof(result->message),
                "Batch size must be multiple of 8 for AVX2. Corrected %u → %u",
                user_batch_size, result->corrected_value);
        result->applied_correction = true;
        return result->corrected_value;
    }

    // Warn if batch size is very large (cache thrashing)
    if (user_batch_size > 4096) {
        result->status = PARAM_WARNING;
        snprintf(result->message, sizeof(result->message),
                "Batch size %u may cause cache thrashing. Optimal: %u",
                user_batch_size, sysinfo->recommended_batch_size);
        return user_batch_size;
    }

    // Warn if batch size is very small (poor batching efficiency)
    if (user_batch_size < 256) {
        result->status = PARAM_WARNING;
        snprintf(result->message, sizeof(result->message),
                "Batch size %u is small. Optimal: %u for better ModInv efficiency",
                user_batch_size, sysinfo->recommended_batch_size);
        return user_batch_size;
    }

    // Batch size is reasonable
    result->status = PARAM_OK;
    snprintf(result->message, sizeof(result->message), "Batch size validated");
    return user_batch_size;
}

int validate_blocks_per_sm(
    int user_blocks_per_sm,
    const struct gpu_backend_info_t *gpu_info,
    param_validation_result_t *result
) {
    memset(result, 0, sizeof(param_validation_result_t));
    result->original_value = user_blocks_per_sm;
    result->corrected_value = user_blocks_per_sm;

    // If gpu_info is NULL, we can't validate
    if (gpu_info == NULL) {
        result->status = PARAM_WARNING;
        snprintf(result->message, sizeof(result->message),
                "GPU info not available - cannot validate blocks_per_sm");
        return user_blocks_per_sm;
    }

    // Determine hardware limits based on compute capability
    int max_blocks_per_sm = 32; // Conservative default
    int recommended_blocks = 4; // Typical optimal value

    // Set limits based on compute capability
    if (gpu_info->compute_major == 7) {
        // Volta/Turing (7.x)
        max_blocks_per_sm = 32;
        recommended_blocks = 4;
    } else if (gpu_info->compute_major == 8) {
        // Ampere (8.x)
        max_blocks_per_sm = 32;
        recommended_blocks = 4;
    } else if (gpu_info->compute_major >= 9) {
        // Ada Lovelace/Hopper (9.x+)
        max_blocks_per_sm = 32;
        recommended_blocks = 4;
    } else if (gpu_info->compute_major == 6) {
        // Pascal (6.x)
        max_blocks_per_sm = 32;
        recommended_blocks = 3;
    } else if (gpu_info->compute_major == 5) {
        // Maxwell (5.x)
        max_blocks_per_sm = 32;
        recommended_blocks = 3;
    } else {
        // Older or unknown architecture
        max_blocks_per_sm = 16;
        recommended_blocks = 2;
    }

    result->suggested_value = recommended_blocks;

    // If user specified 0, use auto-tuned/recommended value
    if (user_blocks_per_sm == 0) {
        result->corrected_value = recommended_blocks;
        result->status = PARAM_OK;
        snprintf(result->message, sizeof(result->message),
                "Using recommended value: %d blocks/SM (optimal for compute %d.%d)",
                result->corrected_value, gpu_info->compute_major, gpu_info->compute_minor);
        result->applied_correction = true;
        return result->corrected_value;
    }

    // Validate against maximum hardware limit
    if (user_blocks_per_sm > max_blocks_per_sm) {
        result->status = PARAM_CORRECTED;
        result->corrected_value = max_blocks_per_sm;
        snprintf(result->message, sizeof(result->message),
                "Requested %d blocks/SM exceeds hardware limit (%d). Auto-corrected to %d.",
                user_blocks_per_sm, max_blocks_per_sm, result->corrected_value);
        result->applied_correction = true;
        return result->corrected_value;
    }

    // Check if value is too low (underutilization)
    if (user_blocks_per_sm == 1) {
        result->status = PARAM_WARNING;
        result->corrected_value = user_blocks_per_sm;
        snprintf(result->message, sizeof(result->message),
                "Only 1 block/SM may underutilize GPU. Consider 2-%d for better occupancy.",
                recommended_blocks * 2);
        result->applied_correction = false;
        return result->corrected_value;
    }

    // Check if value is too high (may cause resource pressure)
    if (user_blocks_per_sm > recommended_blocks * 3) {
        result->status = PARAM_WARNING;
        result->corrected_value = user_blocks_per_sm;
        snprintf(result->message, sizeof(result->message),
                "Using %d blocks/SM is high. May cause register/shared memory pressure. Optimal: %d-%d",
                user_blocks_per_sm, recommended_blocks / 2, recommended_blocks * 2);
        result->applied_correction = false;
        return result->corrected_value;
    }

    // Check if value is optimal
    if (user_blocks_per_sm == recommended_blocks) {
        result->status = PARAM_OK;
        snprintf(result->message, sizeof(result->message),
                "Blocks per SM is optimal for compute %d.%d",
                gpu_info->compute_major, gpu_info->compute_minor);
        return result->corrected_value;
    }

    // Check if value is in reasonable range
    if (user_blocks_per_sm >= recommended_blocks / 2 &&
        user_blocks_per_sm <= recommended_blocks * 2) {
        result->status = PARAM_OK;
        snprintf(result->message, sizeof(result->message),
                "Blocks per SM is within reasonable range (%d-%d)",
                recommended_blocks / 2, recommended_blocks * 2);
        return result->corrected_value;
    }

    // Value is suboptimal but not dangerous
    result->status = PARAM_SUBOPTIMAL;
    snprintf(result->message, sizeof(result->message),
            "Using %d blocks/SM. Recommended: %d for better performance.",
            user_blocks_per_sm, recommended_blocks);
    return result->corrected_value;
}

int validate_threads_per_block(
    int user_threads_per_block,
    const struct gpu_backend_info_t *gpu_info,
    param_validation_result_t *result
) {
    memset(result, 0, sizeof(param_validation_result_t));
    result->original_value = user_threads_per_block;
    result->corrected_value = user_threads_per_block;

    // If gpu_info is NULL, we can't validate
    if (gpu_info == NULL) {
        result->status = PARAM_WARNING;
        snprintf(result->message, sizeof(result->message),
                "GPU info not available - cannot validate threads_per_block");
        return user_threads_per_block;
    }

    // Get hardware limit from GPU info
    int max_threads_per_block = gpu_info->max_threads_per_block;
    if (max_threads_per_block == 0) {
        max_threads_per_block = 1024; // Safe default for modern GPUs
    }

    // Determine recommended value based on compute capability
    int recommended_threads = 256; // Conservative default

    // Set recommendations based on compute capability
    if (gpu_info->compute_major == 7) {
        // Volta/Turing (7.x) - good occupancy with 256-512
        recommended_threads = 256;
    } else if (gpu_info->compute_major == 8) {
        // Ampere (8.x) - can handle 512 well
        recommended_threads = 512;
    } else if (gpu_info->compute_major >= 9) {
        // Ada Lovelace/Hopper (9.x+) - efficient with 512-1024
        recommended_threads = 512;
    } else if (gpu_info->compute_major == 6) {
        // Pascal (6.x) - 256 is optimal
        recommended_threads = 256;
    } else if (gpu_info->compute_major == 5) {
        // Maxwell (5.x) - smaller is better
        recommended_threads = 128;
    } else {
        // Older or unknown architecture
        recommended_threads = 128;
    }

    result->suggested_value = recommended_threads;

    // If user specified 0, use auto-tuned/recommended value
    if (user_threads_per_block == 0) {
        result->corrected_value = recommended_threads;
        result->status = PARAM_OK;
        snprintf(result->message, sizeof(result->message),
                "Using recommended value: %d threads/block (optimal for compute %d.%d)",
                result->corrected_value, gpu_info->compute_major, gpu_info->compute_minor);
        result->applied_correction = true;
        return result->corrected_value;
    }

    // Validate against maximum hardware limit
    if (user_threads_per_block > max_threads_per_block) {
        result->status = PARAM_CORRECTED;
        result->corrected_value = max_threads_per_block;
        snprintf(result->message, sizeof(result->message),
                "Requested %d threads/block exceeds hardware limit (%d). Auto-corrected to %d.",
                user_threads_per_block, max_threads_per_block, result->corrected_value);
        result->applied_correction = true;
        return result->corrected_value;
    }

    // Threads per block must be multiple of warp size (32)
    if (user_threads_per_block % 32 != 0) {
        result->status = PARAM_CORRECTED;
        result->corrected_value = ((user_threads_per_block + 31) / 32) * 32;
        // Ensure we don't exceed max
        if (result->corrected_value > max_threads_per_block) {
            result->corrected_value = (max_threads_per_block / 32) * 32;
        }
        snprintf(result->message, sizeof(result->message),
                "Threads per block must be multiple of 32 (warp size). Corrected %d → %d",
                user_threads_per_block, result->corrected_value);
        result->applied_correction = true;
        return result->corrected_value;
    }

    // Check if value is too low (poor GPU utilization)
    if (user_threads_per_block < 64) {
        result->status = PARAM_WARNING;
        result->corrected_value = user_threads_per_block;
        snprintf(result->message, sizeof(result->message),
                "Only %d threads/block is very low. Consider %d-%d for better GPU utilization.",
                user_threads_per_block, 128, recommended_threads);
        result->applied_correction = false;
        return result->corrected_value;
    }

    // Check if value is too high (may cause register pressure)
    if (user_threads_per_block > 768 && recommended_threads <= 512) {
        result->status = PARAM_WARNING;
        result->corrected_value = user_threads_per_block;
        snprintf(result->message, sizeof(result->message),
                "Using %d threads/block is high. May cause register pressure. Optimal: %d",
                user_threads_per_block, recommended_threads);
        result->applied_correction = false;
        return result->corrected_value;
    }

    // Check if value is optimal
    if (user_threads_per_block == recommended_threads) {
        result->status = PARAM_OK;
        snprintf(result->message, sizeof(result->message),
                "Threads per block is optimal for compute %d.%d",
                gpu_info->compute_major, gpu_info->compute_minor);
        return result->corrected_value;
    }

    // Check if value is in reasonable range (within 2x of recommended)
    if (user_threads_per_block >= recommended_threads / 2 &&
        user_threads_per_block <= recommended_threads * 2 &&
        user_threads_per_block <= max_threads_per_block) {
        result->status = PARAM_OK;
        snprintf(result->message, sizeof(result->message),
                "Threads per block is within reasonable range (%d-%d)",
                recommended_threads / 2, recommended_threads * 2);
        return result->corrected_value;
    }

    // Value is suboptimal but not dangerous
    result->status = PARAM_SUBOPTIMAL;
    snprintf(result->message, sizeof(result->message),
            "Using %d threads/block. Recommended: %d for better performance.",
            user_threads_per_block, recommended_threads);
    return result->corrected_value;
}

void print_validation_result(const param_validation_result_t *result, const char *param_name) {
    const char *color;
    const char *prefix;

    switch (result->status) {
        case PARAM_OK:
            color = COLOR_GREEN;
            prefix = "[✓]";
            break;
        case PARAM_SUBOPTIMAL:
            color = COLOR_BLUE;
            prefix = "[i]";
            break;
        case PARAM_CORRECTED:
            color = COLOR_YELLOW;
            prefix = "[!]";
            break;
        case PARAM_WARNING:
            color = COLOR_RED;
            prefix = "[⚠]";
            break;
        default:
            color = COLOR_RESET;
            prefix = "[?]";
    }

    printf("%s%s %s: %s%s\n", color, prefix, param_name, result->message, COLOR_RESET);
}

bool validate_all_parameters(
    int *threads,
    uint64_t *n_value,
    int *kfactor,
    uint32_t *batch_size,
    const system_info_t *sysinfo,
    bool auto_correct
) {
    param_validation_result_t result;
    bool all_safe = true;

    printf("\n[+] Validating parameters against hardware...\n");
    fflush(stdout);

    // Validate threads
    int validated_threads = validate_threads(*threads, sysinfo, &result);
    if (auto_correct && result.applied_correction) {
        *threads = validated_threads;
    }
    // Always print result (even if OK, for transparency)
    print_validation_result(&result, "Threads");
    fflush(stdout);
    if (result.status == PARAM_WARNING || result.status == PARAM_CORRECTED) {
        all_safe = false;
    }

    // Validate batch size
    uint32_t validated_batch = validate_batch_size(*batch_size, sysinfo, &result);
    if (auto_correct && result.applied_correction) {
        *batch_size = validated_batch;
    }
    // Always print batch size result
    print_validation_result(&result, "Batch Size");
    if (result.status == PARAM_CORRECTED) all_safe = false;

    // For BSGS mode, validate N and K factor
    if (*n_value > 0) {
        uint64_t validated_n = validate_n_value(*n_value, *kfactor, sysinfo, &result);
        if (auto_correct && result.applied_correction) {
            *n_value = validated_n;
        }
        // Always print N validation result
        print_validation_result(&result, "N Value");
        if (result.status == PARAM_CORRECTED || result.status == PARAM_WARNING) {
            all_safe = false; // OOM would have occurred or other issues
        }

        int validated_k = validate_kfactor(*kfactor, *n_value, sysinfo, &result);
        if (auto_correct && result.applied_correction) {
            *kfactor = validated_k;
        }
        // Always print K validation result
        print_validation_result(&result, "K Factor");
        if (result.status == PARAM_CORRECTED || result.status == PARAM_WARNING) {
            all_safe = false;
        }
    }

    if (all_safe) {
        printf("%s[✓] All parameters validated successfully%s\n", COLOR_GREEN, COLOR_RESET);
    } else {
        printf("%s[!] Some parameters were adjusted for safety%s\n", COLOR_YELLOW, COLOR_RESET);
    }
    fflush(stdout);

    return all_safe;
}

void recommend_bsgs_params(
    const system_info_t *sysinfo,
    uint64_t target_ram_mb,
    uint64_t *out_n,
    int *out_kfactor
) {
    // If no target specified, use 60% of available RAM
    if (target_ram_mb == 0) {
        target_ram_mb = (sysinfo->ram_available * 60) / 100;
    }

    // Candidate configurations (N, K) sorted by descending RAM usage
    struct {
        uint64_t n;
        int k;
        uint64_t ram_mb;
    } candidates[] = {
        {0x400000000000ULL, 4096, 0},
        {0x100000000000ULL, 4096, 0},
        {0x40000000000ULL,  2048, 0},
        {0x10000000000ULL,  2048, 0},
        {0x10000000000ULL,  1024, 0},
        {0x4000000000ULL,   1024, 0},
        {0x1000000000ULL,   512,  0},
    };

    int n_candidates = sizeof(candidates) / sizeof(candidates[0]);

    // Calculate RAM for each
    for (int i = 0; i < n_candidates; i++) {
        candidates[i].ram_mb = calculate_bsgs_memory_mb(candidates[i].n, candidates[i].k);
    }

    // Pick first that fits in target RAM
    *out_n = 0x1000000000ULL;  // Safe minimum default
    *out_kfactor = 512;

    for (int i = 0; i < n_candidates; i++) {
        if (candidates[i].ram_mb <= target_ram_mb) {
            *out_n = candidates[i].n;
            *out_kfactor = candidates[i].k;
            printf("[i] Recommended BSGS: N=0x%llx, K=%d (uses %llu MB of %llu MB available)\n",
                   (unsigned long long)*out_n, *out_kfactor,
                   (unsigned long long)candidates[i].ram_mb,
                   (unsigned long long)target_ram_mb);
            return;
        }
    }

    // If we get here, even minimum config doesn't fit
    printf("[W] Only %llu MB RAM available. Using minimal BSGS config (may be slow).\n",
           (unsigned long long)target_ram_mb);
}
