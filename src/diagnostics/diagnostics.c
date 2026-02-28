#include "diagnostics.h"
#include "gpu_diagnostics.h"
#include "../output.h"
#include "../core/sysinfo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ANSI color codes for terminal output (disabled on Windows by default)
#ifdef _WIN32
#define COLOR_RESET   ""
#define COLOR_GREEN   ""
#define COLOR_YELLOW  ""
#define COLOR_RED     ""
#define COLOR_BLUE    ""
#define COLOR_CYAN    ""
#define COLOR_MAGENTA ""
#define COLOR_BOLD    ""
#else
#define COLOR_RESET   "\033[0m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_RED     "\033[31m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_BOLD    "\033[1m"
#endif

// Helper: Add a check result to the report
void diag_add_check(
    diagnostic_report_t *report,
    diag_status_t status,
    const char *check_name,
    const char *message,
    const char *recommendation
) {
    if (report->check_count >= 32) {
        return; // Max checks reached
    }

    diag_check_result_t *check = &report->checks[report->check_count];
    check->status = status;
    strncpy(check->check_name, check_name, sizeof(check->check_name) - 1);
    check->check_name[sizeof(check->check_name) - 1] = '\0';
    strncpy(check->message, message, sizeof(check->message) - 1);
    check->message[sizeof(check->message) - 1] = '\0';
    strncpy(check->recommendation, recommendation, sizeof(check->recommendation) - 1);
    check->recommendation[sizeof(check->recommendation) - 1] = '\0';

    if (status == DIAG_ERROR) {
        report->error_count++;
    } else if (status == DIAG_WARNING) {
        report->warning_count++;
    }

    report->check_count++;
}

// Check CPU features
void diag_check_cpu_features(diagnostic_report_t *report) {
    const system_info_t *info = &report->sysinfo;
    char msg[256];
    char rec[256];

    // Check physical cores
    if (info->cpu_physical_cores > 0) {
        snprintf(msg, sizeof(msg), "%d physical cores, %d logical cores detected",
                 info->cpu_physical_cores, info->cpu_logical_cores);
        diag_add_check(report, DIAG_OK, "CPU Cores", msg, "Optimal for multi-threaded search");
    } else {
        snprintf(msg, sizeof(msg), "Could not detect physical core count");
        diag_add_check(report, DIAG_WARNING, "CPU Cores", msg,
                      "Will use logical cores, performance may vary");
    }

    // Check AVX2 support (critical for performance)
    if (info->has_avx2) {
        diag_add_check(report, DIAG_OK, "AVX2 Support",
                      "AVX2 SIMD instructions available (8-way parallel hashing)",
                      "Use default build for optimal performance");
    } else {
        diag_add_check(report, DIAG_WARNING, "AVX2 Support",
                      "AVX2 not available, will use SSE2 (4-way parallel)",
                      "Performance will be ~50% slower than AVX2 systems");
    }

    // Check AVX-512 support (best performance)
    if (info->has_avx512f && info->has_avx512dq) {
        diag_add_check(report, DIAG_OK, "AVX-512 Support",
                      "AVX-512 Foundation + DQ available (16-way parallel hashing)",
                      "Cutting-edge performance on compatible CPUs");
    } else if (info->has_avx512f) {
        diag_add_check(report, DIAG_INFO, "AVX-512 Support",
                      "AVX-512F available but AVX-512DQ missing",
                      "AVX-512 hashing requires both AVX-512F and AVX-512DQ");
    }

    // Check SHA-NI support (accelerates SHA256)
    if (info->has_sha_ni) {
        diag_add_check(report, DIAG_OK, "SHA-NI Support",
                      "Intel SHA extensions available (hardware SHA256 acceleration)",
                      "Optimal for address mode searches");
    }
}

// Check memory configuration
void diag_check_memory(diagnostic_report_t *report) {
    const system_info_t *info = &report->sysinfo;
    char msg[256];
    char rec[256];

    // Check total RAM
    if (info->ram_total > 0) {
        snprintf(msg, sizeof(msg), "Total RAM: %llu MB, Available: %llu MB, Free: %llu MB",
                 (unsigned long long)info->ram_total,
                 (unsigned long long)info->ram_available,
                 (unsigned long long)info->ram_free);
        diag_add_check(report, DIAG_INFO, "Memory Status", msg, "");
    }

    // Check available memory
    if (info->ram_available < 1024) {
        snprintf(msg, sizeof(msg), "Low available memory: %llu MB",
                 (unsigned long long)info->ram_available);
        snprintf(rec, sizeof(rec), "Close other applications or use smaller -n values for BSGS mode");
        diag_add_check(report, DIAG_WARNING, "Memory Availability", msg, rec);
    } else if (info->ram_available < 512) {
        snprintf(msg, sizeof(msg), "Very low available memory: %llu MB",
                 (unsigned long long)info->ram_available);
        snprintf(rec, sizeof(rec), "Insufficient memory for BSGS mode with large N values");
        diag_add_check(report, DIAG_ERROR, "Memory Availability", msg, rec);
    } else {
        snprintf(msg, sizeof(msg), "Available memory: %llu MB",
                 (unsigned long long)info->ram_available);
        snprintf(rec, sizeof(rec), "Sufficient for BSGS mode up to N=0x%llx",
                 (unsigned long long)info->recommended_n);
        diag_add_check(report, DIAG_OK, "Memory Availability", msg, rec);
    }

    // Check memory pressure (available vs total)
    if (info->ram_total > 0) {
        double available_percent = (100.0 * info->ram_available) / info->ram_total;
        if (available_percent < 20.0) {
            snprintf(msg, sizeof(msg), "Memory pressure detected (%.1f%% available)",
                     available_percent);
            diag_add_check(report, DIAG_WARNING, "Memory Pressure", msg,
                          "System may swap to disk, significantly reducing performance");
        }
    }
}

// Check cache configuration
void diag_check_cache(diagnostic_report_t *report) {
    const system_info_t *info = &report->sysinfo;
    char msg[256];
    char rec[256];

    // Check L1 cache
    if (info->cache_l1_size > 0) {
        snprintf(msg, sizeof(msg), "L1 cache: %llu KB per core",
                 (unsigned long long)info->cache_l1_size);
        diag_add_check(report, DIAG_INFO, "L1 Cache", msg, "");
    }

    // Check L2 cache
    if (info->cache_l2_size > 0) {
        snprintf(msg, sizeof(msg), "L2 cache: %llu KB per core",
                 (unsigned long long)info->cache_l2_size);
        diag_add_check(report, DIAG_INFO, "L2 Cache", msg, "");
    }

    // Check L3 cache (important for BSGS bloom filter lookups)
    if (info->cache_l3_size > 0) {
        snprintf(msg, sizeof(msg), "L3 cache: %llu KB total",
                 (unsigned long long)info->cache_l3_size);

        if (info->cache_l3_size < 4096) {
            snprintf(rec, sizeof(rec), "Small L3 cache may limit BSGS performance");
            diag_add_check(report, DIAG_WARNING, "L3 Cache", msg, rec);
        } else {
            snprintf(rec, sizeof(rec), "Adequate for bloom filter lookups");
            diag_add_check(report, DIAG_OK, "L3 Cache", msg, rec);
        }
    } else {
        diag_add_check(report, DIAG_WARNING, "L3 Cache",
                      "Could not detect L3 cache size",
                      "Performance estimation may be inaccurate");
    }
}

// Check GPU configuration
void diag_check_gpu(diagnostic_report_t *report) {
    const system_info_t *info = &report->sysinfo;
    char msg[256];
    char rec[256];

    // Run detailed GPU diagnostics using gpu_diagnostics module
    gpu_diag_info_t gpu_info;
    int gpu_diag_result = gpu_diag_check_compatibility(&gpu_info);

    // Check if CUDA is available at all
    if (!gpu_info.cuda_available) {
        diag_add_check(report, DIAG_INFO, "GPU Status",
                      "CUDA not available (CPU build or no CUDA support)",
                      "CPU-only mode will be used (--gpu flag will have no effect)");
        return;
    }

    // Check NVIDIA driver version
    if (gpu_info.driver_compatible) {
        snprintf(msg, sizeof(msg), "NVIDIA Driver: %d.%d detected",
                 gpu_info.driver_version_major, gpu_info.driver_version_minor);

        if (gpu_info.driver_version_major >= 470) {
            snprintf(rec, sizeof(rec), "Modern driver version, good compatibility");
            diag_add_check(report, DIAG_OK, "GPU Driver", msg, rec);
        } else if (gpu_info.driver_version_major >= 390) {
            snprintf(rec, sizeof(rec), "Older driver, consider updating for best performance");
            diag_add_check(report, DIAG_WARNING, "GPU Driver", msg, rec);
        } else {
            snprintf(rec, sizeof(rec), "Very old driver, may have compatibility issues");
            diag_add_check(report, DIAG_WARNING, "GPU Driver", msg, rec);
        }
    } else {
        snprintf(msg, sizeof(msg), "Failed to query NVIDIA driver version: %s",
                 gpu_info.error_message[0] ? gpu_info.error_message : "Unknown error");
        diag_add_check(report, DIAG_ERROR, "GPU Driver", msg,
                      "Install or update NVIDIA GPU drivers");
        return;
    }

    // Check CUDA runtime version
    snprintf(msg, sizeof(msg), "CUDA Runtime: %d.%d",
             gpu_info.cuda_runtime_version_major,
             gpu_info.cuda_runtime_version_minor);

    if (gpu_info.cuda_runtime_version_major >= 11) {
        snprintf(rec, sizeof(rec), "Modern CUDA version, full feature support");
        diag_add_check(report, DIAG_OK, "CUDA Runtime", msg, rec);
    } else {
        snprintf(rec, sizeof(rec), "Old CUDA version, consider updating CUDA toolkit");
        diag_add_check(report, DIAG_WARNING, "CUDA Runtime", msg, rec);
    }

    // Check GPU count and device availability
    if (gpu_info.gpu_count == 0) {
        snprintf(msg, sizeof(msg), "No CUDA-capable GPU detected: %s",
                 gpu_info.error_message[0] ? gpu_info.error_message : "No devices found");
        diag_add_check(report, DIAG_WARNING, "GPU Detection", msg,
                      "Verify GPU is properly installed and recognized by nvidia-smi");
        return;
    }

    snprintf(msg, sizeof(msg), "%d CUDA-capable GPU(s) detected", gpu_info.gpu_count);
    diag_add_check(report, DIAG_OK, "GPU Detection", msg,
                  "GPU acceleration available for search operations");

    // Check GPU model and hardware details
    if (gpu_info.gpu_name[0] != '\0') {
        snprintf(msg, sizeof(msg), "Primary GPU: %s", gpu_info.gpu_name);
        diag_add_check(report, DIAG_INFO, "GPU Model", msg, "");
    }

    // Check VRAM
    if (gpu_info.vram_mb > 0) {
        snprintf(msg, sizeof(msg), "GPU VRAM: %llu MB",
                 (unsigned long long)gpu_info.vram_mb);

        if (gpu_info.vram_mb < 2048) {
            snprintf(rec, sizeof(rec), "Limited VRAM, reduce batch sizes if GPU OOM occurs");
            diag_add_check(report, DIAG_WARNING, "GPU Memory", msg, rec);
        } else if (gpu_info.vram_mb >= 8192) {
            snprintf(rec, sizeof(rec), "Excellent VRAM capacity for large batch sizes");
            diag_add_check(report, DIAG_OK, "GPU Memory", msg, rec);
        } else {
            snprintf(rec, sizeof(rec), "Sufficient for GPU-accelerated searches");
            diag_add_check(report, DIAG_OK, "GPU Memory", msg, rec);
        }
    }

    // Check compute capability (critical for compatibility)
    snprintf(msg, sizeof(msg), "Compute Capability: %d.%d (sm_%d%d)",
             gpu_info.compute_major, gpu_info.compute_minor,
             gpu_info.compute_major, gpu_info.compute_minor);

    if (gpu_diag_result != 0 || !gpu_info.compute_capability_ok) {
        // GPU failed compatibility check
        snprintf(rec, sizeof(rec), "Compute capability %d.%d is below minimum 6.0",
                 gpu_info.compute_major, gpu_info.compute_minor);
        diag_add_check(report, DIAG_ERROR, "GPU Compute Capability", msg, rec);

        if (gpu_info.error_message[0] != '\0') {
            snprintf(msg, sizeof(msg), "GPU Compatibility Issue: %s", gpu_info.error_message);
            diag_add_check(report, DIAG_ERROR, "GPU Compatibility", msg,
                          "GPU does not meet minimum requirements for CUDA kernels");
        }
    } else {
        // GPU passed compatibility check
        if (gpu_info.compute_major >= 8) {
            snprintf(rec, sizeof(rec), "Excellent (Ampere/Ada/Hopper), optimal performance");
            diag_add_check(report, DIAG_OK, "GPU Compute Capability", msg, rec);
        } else if (gpu_info.compute_major >= 7) {
            snprintf(rec, sizeof(rec), "Very Good (Volta/Turing), excellent performance");
            diag_add_check(report, DIAG_OK, "GPU Compute Capability", msg, rec);
        } else if (gpu_info.compute_major >= 6) {
            snprintf(rec, sizeof(rec), "Good (Pascal), meets minimum requirements");
            diag_add_check(report, DIAG_OK, "GPU Compute Capability", msg, rec);
        } else {
            snprintf(rec, sizeof(rec), "Below minimum requirement (6.0)");
            diag_add_check(report, DIAG_ERROR, "GPU Compute Capability", msg, rec);
        }
    }

    // Check SM count (streaming multiprocessors)
    if (gpu_info.multiprocessors > 0) {
        snprintf(msg, sizeof(msg), "Streaming Multiprocessors (SMs): %d",
                 gpu_info.multiprocessors);

        if (gpu_info.multiprocessors >= 80) {
            snprintf(rec, sizeof(rec), "High-end GPU, excellent parallel performance");
        } else if (gpu_info.multiprocessors >= 40) {
            snprintf(rec, sizeof(rec), "Mid-range GPU, good parallel performance");
        } else {
            snprintf(rec, sizeof(rec), "Entry-level GPU, basic acceleration available");
        }
        diag_add_check(report, DIAG_INFO, "GPU Architecture", msg, rec);
    }

    // Add overall GPU status summary
    if (gpu_diag_result == 0 && gpu_info.compute_capability_ok) {
        diag_add_check(report, DIAG_OK, "GPU Overall Status",
                      "GPU is compatible and ready for acceleration",
                      "Use --gpu flag to enable GPU-accelerated searches");
    } else {
        diag_add_check(report, DIAG_ERROR, "GPU Overall Status",
                      "GPU detected but not compatible with this build",
                      "GPU acceleration will not be available");
    }
}

// Check system limits
void diag_check_system_limits(diagnostic_report_t *report) {
    const system_info_t *info = &report->sysinfo;
    char msg[256];
    char rec[256];

    // Check recommended thread count
    if (info->recommended_threads > 0) {
        snprintf(msg, sizeof(msg), "Recommended threads: %d (based on %d logical cores)",
                 info->recommended_threads, info->cpu_logical_cores);
        snprintf(rec, sizeof(rec), "Use -t %d or let auto-tuning select optimal value",
                 info->recommended_threads);
        diag_add_check(report, DIAG_INFO, "Thread Configuration", msg, rec);
    }

    // Check recommended BSGS parameters
    if (info->recommended_n > 0) {
        snprintf(msg, sizeof(msg), "Recommended N value: 0x%llx (K=%d)",
                 (unsigned long long)info->recommended_n,
                 info->recommended_kfactor);
        snprintf(rec, sizeof(rec), "Use -n 0x%llx -k %d for optimal BSGS performance",
                 (unsigned long long)info->recommended_n,
                 info->recommended_kfactor);
        diag_add_check(report, DIAG_INFO, "BSGS Configuration", msg, rec);
    }

    // Check batch size recommendation
    if (info->recommended_batch_size > 0) {
        snprintf(msg, sizeof(msg), "Recommended batch size: %u",
                 info->recommended_batch_size);
        diag_add_check(report, DIAG_INFO, "Batch Size", msg,
                      "Auto-tuned for cache alignment and SIMD efficiency");
    }
}

// Run full system diagnostics
void diagnostics_run(diagnostic_report_t *report) {
    memset(report, 0, sizeof(diagnostic_report_t));

    // Initialize system information
    sysinfo_init(&report->sysinfo);

    // Run individual diagnostic checks
    diag_check_cpu_features(report);
    diag_check_memory(report);
    diag_check_cache(report);
    diag_check_gpu(report);
    diag_check_system_limits(report);
}

// Print diagnostic report
void diagnostics_print_report(const diagnostic_report_t *report) {
    const system_info_t *info = &report->sysinfo;

    // Print header
    printf("\n");
    printf("%s%s╔════════════════════════════════════════════════════════════════════════╗%s\n",
           COLOR_BOLD, COLOR_CYAN, COLOR_RESET);
    printf("%s%s║                    KEYHUNT SYSTEM DIAGNOSTICS                          ║%s\n",
           COLOR_BOLD, COLOR_CYAN, COLOR_RESET);
    printf("%s%s╚════════════════════════════════════════════════════════════════════════╝%s\n",
           COLOR_BOLD, COLOR_CYAN, COLOR_RESET);
    printf("\n");

    // Print CPU summary
    printf("%s%s[CPU Information]%s\n", COLOR_BOLD, COLOR_BLUE, COLOR_RESET);
    if (info->cpu_model[0] != '\0') {
        printf("  Model: %s\n", info->cpu_model);
    }
    printf("  Physical Cores: %d\n", info->cpu_physical_cores);
    printf("  Logical Cores:  %d\n", info->cpu_logical_cores);
    printf("  Features: ");
    if (info->has_avx512f && info->has_avx512dq) {
        printf("%sAVX-512%s ", COLOR_GREEN, COLOR_RESET);
    }
    if (info->has_avx2) {
        printf("%sAVX2%s ", COLOR_GREEN, COLOR_RESET);
    }
    if (info->has_sha_ni) {
        printf("%sSHA-NI%s ", COLOR_GREEN, COLOR_RESET);
    }
    printf("\n\n");

    // Print memory summary
    printf("%s%s[Memory Information]%s\n", COLOR_BOLD, COLOR_BLUE, COLOR_RESET);
    printf("  Memory: %llu MB Total, %llu MB Available, %llu MB Free\n",
           (unsigned long long)info->ram_total,
           (unsigned long long)info->ram_available,
           (unsigned long long)info->ram_free);
    printf("  Total RAM:     %llu MB\n", (unsigned long long)info->ram_total);
    printf("  Available RAM: %llu MB\n", (unsigned long long)info->ram_available);
    printf("  Free RAM:      %llu MB\n", (unsigned long long)info->ram_free);
    if (info->cache_l3_size > 0) {
        printf("  L3 Cache:      %llu KB\n", (unsigned long long)info->cache_l3_size);
    }
    printf("\n");

    // Print GPU summary (if available)
    if (info->gpu_count > 0) {
        printf("%s%s[GPU Information]%s\n", COLOR_BOLD, COLOR_BLUE, COLOR_RESET);
        printf("  GPU Count: %d\n", info->gpu_count);
        if (info->gpu_name[0] != '\0') {
            printf("  Model: %s\n", info->gpu_name);
        }
        if (info->gpu_vram_mb > 0) {
            printf("  VRAM: %llu MB\n", (unsigned long long)info->gpu_vram_mb);
        }
        if (info->gpu_compute_capability > 0) {
            int major = info->gpu_compute_capability / 10;
            int minor = info->gpu_compute_capability % 10;
            printf("  Compute Capability: %d.%d\n", major, minor);
        }
        printf("\n");
    }

    // Print diagnostic check results
    printf("%s%s[Diagnostic Checks]%s\n", COLOR_BOLD, COLOR_BLUE, COLOR_RESET);
    for (int i = 0; i < report->check_count; i++) {
        const diag_check_result_t *check = &report->checks[i];

        // Status indicator with color
        const char *status_color;
        const char *status_symbol;
        switch (check->status) {
            case DIAG_OK:
                status_color = COLOR_GREEN;
                status_symbol = "✓";
                break;
            case DIAG_WARNING:
                status_color = COLOR_YELLOW;
                status_symbol = "⚠";
                break;
            case DIAG_ERROR:
                status_color = COLOR_RED;
                status_symbol = "✗";
                break;
            case DIAG_INFO:
            default:
                status_color = COLOR_BLUE;
                status_symbol = "ℹ";
                break;
        }

        printf("  %s%s%s %s%-24s%s %s\n",
               status_color, status_symbol, COLOR_RESET,
               COLOR_BOLD, check->check_name, COLOR_RESET,
               check->message);

        // Print recommendation if available
        if (check->recommendation[0] != '\0') {
            printf("    %s→ %s%s\n", COLOR_CYAN, check->recommendation, COLOR_RESET);
        }
    }
    printf("\n");

    // Print summary
    printf("%s%s[Summary]%s\n", COLOR_BOLD, COLOR_BLUE, COLOR_RESET);
    printf("  Total Checks: %d\n", report->check_count);
    if (report->error_count > 0) {
        printf("  %sErrors:       %d%s\n", COLOR_RED, report->error_count, COLOR_RESET);
    }
    if (report->warning_count > 0) {
        printf("  %sWarnings:     %d%s\n", COLOR_YELLOW, report->warning_count, COLOR_RESET);
    }
    if (report->error_count == 0 && report->warning_count == 0) {
        printf("  %sStatus:       All checks passed%s\n", COLOR_GREEN, COLOR_RESET);
    } else if (report->error_count > 0) {
        printf("  %sStatus:       Critical issues detected%s\n", COLOR_RED, COLOR_RESET);
    } else {
        printf("  %sStatus:       Minor issues detected%s\n", COLOR_YELLOW, COLOR_RESET);
    }
    printf("\n");

    // Print auto-tuning recommendations
    if (info->recommended_threads > 0 || info->recommended_n > 0) {
        printf("%s%s[Auto-Tuning Recommendations]%s\n", COLOR_BOLD, COLOR_GREEN, COLOR_RESET);
        if (info->recommended_threads > 0) {
            printf("  Threads:    -t %d\n", info->recommended_threads);
        }
        if (info->recommended_n > 0) {
            printf("  BSGS Mode:  -n 0x%llx -k %d\n",
                   (unsigned long long)info->recommended_n,
                   info->recommended_kfactor);
        }
        if (info->recommended_batch_size > 0) {
            printf("  Batch Size: %u (auto-tuned)\n", info->recommended_batch_size);
        }
        printf("\n");
    }
}
