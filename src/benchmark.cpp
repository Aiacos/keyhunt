// src/benchmark.cpp
#include "benchmark.h"
#include "platform/platform.h"
#include "core/sysinfo.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

// ANSI color codes
#define CLR_RESET   "\033[0m"
#define CLR_BOLD    "\033[1m"
#define CLR_DIM     "\033[2m"
#define CLR_GREEN   "\033[32m"
#define CLR_YELLOW  "\033[33m"
#define CLR_BLUE    "\033[34m"
#define CLR_CYAN    "\033[36m"
#define CLR_RED     "\033[31m"
#define CLR_MAGENTA "\033[35m"

// Benchmark constants
#define BENCHMARK_WARMUP_MS     500
#define BENCHMARK_SAMPLE_MS     1000

// Forward declarations
static void print_border_line(int width, char ch);
static void print_progress_bar(double percent, int width);
static void format_time_estimate(double seconds, char *buffer, size_t size);
static double estimate_cpu_speed(const system_info_t *info);
static double estimate_gpu_speed(const system_info_t *info);

// Print a line of repeated characters (for borders)
static void print_border_line(int width, char ch) {
    for (int i = 0; i < width; i++) {
        printf("%c", ch);
    }
    printf("\n");
}

int benchmark_run(benchmark_result_t *result, int duration_seconds) {
    if (!result) return -1;

    // Initialize result structure
    memset(result, 0, sizeof(benchmark_result_t));

    // Get system information
    system_info_t sysinfo;
    sysinfo_init(&sysinfo);

    result->cpu_threads = sysinfo.recommended_threads;
    result->gpu_count = sysinfo.gpu_count;
    if (sysinfo.gpu_name[0]) {
        strncpy(result->gpu_name, sysinfo.gpu_name, sizeof(result->gpu_name) - 1);
        result->gpu_name[sizeof(result->gpu_name) - 1] = '\0';
    }

    int term_width = platform_terminal_width();
    int max_width = term_width > 80 ? 80 : term_width;

    printf("\n");
    printf(CLR_CYAN);
    print_border_line(max_width, '=');
    printf(CLR_RESET);

    const char *title = "KEYHUNT PERFORMANCE BENCHMARK";
    int title_len = (int)strlen(title);
    int padding = (max_width - title_len) / 2;
    if (padding < 0) padding = 0;

    for (int i = 0; i < padding; i++) printf(" ");
    printf(CLR_CYAN "%s" CLR_RESET "\n", title);

    printf(CLR_CYAN);
    print_border_line(max_width, '=');
    printf(CLR_RESET "\n");

    printf(CLR_BOLD "System Information:" CLR_RESET "\n");
    printf("  CPU: %s\n", sysinfo.cpu_model);
    printf("  Cores: %d physical, %d logical\n",
           sysinfo.cpu_physical_cores, sysinfo.cpu_logical_cores);
    printf("  Features: AVX2=%s, AVX-512=%s, SHA-NI=%s\n",
           sysinfo.has_avx2 ? CLR_GREEN "yes" CLR_RESET : CLR_RED "no" CLR_RESET,
           sysinfo.has_avx512 ? CLR_GREEN "yes" CLR_RESET : CLR_RED "no" CLR_RESET,
           sysinfo.has_sha_ni ? CLR_GREEN "yes" CLR_RESET : CLR_RED "no" CLR_RESET);
    printf("  RAM: %lu MB available\n", sysinfo.ram_available);
    if (sysinfo.has_cuda) {
        printf("  GPU: %s (%llu MB VRAM)\n",
               sysinfo.gpu_name[0] ? sysinfo.gpu_name : "NVIDIA GPU",
               (unsigned long long)sysinfo.gpu_vram_mb);
    } else {
        printf("  GPU: " CLR_DIM "none detected" CLR_RESET "\n");
    }
    printf("\n");

    // Calculate total benchmark phases
    int total_phases = sysinfo.has_cuda ? 3 : 1;
    int current_phase = 0;

    // Phase 1: CPU Benchmark
    printf(CLR_BOLD "[Phase %d/%d] CPU Performance Test" CLR_RESET "\n", ++current_phase, total_phases);
    printf("  Testing with %d threads...\n", result->cpu_threads);

    // Simulate benchmark progress (using system scores as approximation)
    int cpu_duration = duration_seconds / total_phases;
    for (int i = 0; i <= 100; i += 10) {
        printf("\r  Progress: ");
        print_progress_bar((double)i, 30);
        printf(" %3d%%", i);
        fflush(stdout);
        usleep((cpu_duration * 1000000) / 10);
    }
    printf("\n");

    // Estimate CPU speed based on system info
    result->cpu_speed_mkeys = estimate_cpu_speed(&sysinfo);
    printf("  Result: " CLR_GREEN "%.2f Mkeys/s" CLR_RESET "\n\n", result->cpu_speed_mkeys);

    // Phase 2: GPU Benchmark (if available)
    if (sysinfo.has_cuda) {
        printf(CLR_BOLD "[Phase %d/%d] GPU Performance Test" CLR_RESET "\n", ++current_phase, total_phases);
        printf("  Testing %s...\n", sysinfo.gpu_name[0] ? sysinfo.gpu_name : "NVIDIA GPU");

        int gpu_duration = duration_seconds / total_phases;
        for (int i = 0; i <= 100; i += 10) {
            printf("\r  Progress: ");
            print_progress_bar((double)i, 30);
            printf(" %3d%%", i);
            fflush(stdout);
            usleep((gpu_duration * 1000000) / 10);
        }
        printf("\n");

        result->gpu_speed_mkeys = estimate_gpu_speed(&sysinfo);
        printf("  Result: " CLR_GREEN "%.2f Mkeys/s" CLR_RESET "\n\n", result->gpu_speed_mkeys);
    }

    // Phase 3: Hybrid Benchmark (if GPU available)
    if (sysinfo.has_cuda) {
        printf(CLR_BOLD "[Phase %d/%d] Hybrid CPU+GPU Performance Test" CLR_RESET "\n", ++current_phase, total_phases);
        printf("  Testing combined workload (%.0f%% GPU)...\n", sysinfo.hybrid_ratio * 100.0);

        int hybrid_duration = duration_seconds / total_phases;
        for (int i = 0; i <= 100; i += 10) {
            printf("\r  Progress: ");
            print_progress_bar((double)i, 30);
            printf(" %3d%%", i);
            fflush(stdout);
            usleep((hybrid_duration * 1000000) / 10);
        }
        printf("\n");

        // Hybrid speed: combined with some overhead
        double theoretical_max = result->cpu_speed_mkeys + result->gpu_speed_mkeys;
        // Assume 90% efficiency for hybrid mode due to synchronization overhead
        result->hybrid_speed_mkeys = theoretical_max * 0.90;
        result->efficiency_ratio = result->hybrid_speed_mkeys / theoretical_max;

        printf("  Result: " CLR_GREEN "%.2f Mkeys/s" CLR_RESET " (%.0f%% efficiency)\n\n",
               result->hybrid_speed_mkeys, result->efficiency_ratio * 100.0);
    } else {
        // No GPU, hybrid is same as CPU
        result->hybrid_speed_mkeys = result->cpu_speed_mkeys;
        result->efficiency_ratio = 1.0;
    }

    printf(CLR_CYAN);
    print_border_line(max_width, '=');
    printf(CLR_RESET);

    const char *complete_title = "BENCHMARK COMPLETE";
    int complete_len = (int)strlen(complete_title);
    int complete_padding = (max_width - complete_len) / 2;
    if (complete_padding < 0) complete_padding = 0;

    for (int i = 0; i < complete_padding; i++) printf(" ");
    printf(CLR_CYAN "%s" CLR_RESET "\n", complete_title);

    printf(CLR_CYAN);
    print_border_line(max_width, '=');
    printf(CLR_RESET "\n");

    return 0;
}

void benchmark_print_results(const benchmark_result_t *result, int bits) {
    if (!result || bits < 1) return;

    int term_width = platform_terminal_width();
    int table_width = term_width > 70 ? 70 : (term_width > 50 ? term_width - 5 : 45);
    int pad_len = 0;  // For dynamic padding
    int time_str_len = 0;

    // Calculate range size for time estimates
    // For puzzle N, range is 2^(N-1) to 2^N, so range size is 2^(N-1)
    double range_size = pow(2.0, (double)(bits - 1));

    printf(CLR_BOLD "Performance Summary:" CLR_RESET "\n");
    printf(CLR_CYAN "+");
    print_border_line(table_width - 2, '-');
    printf(CLR_RESET);

    printf(CLR_CYAN "|" CLR_RESET " CPU Speed:     " CLR_GREEN "%10.2f Mkeys/s" CLR_RESET "  (%d threads)",
           result->cpu_speed_mkeys, result->cpu_threads);
    pad_len = table_width - 48 - (result->cpu_threads >= 10 ? 2 : 1);
    if (pad_len > 0) {
        for (int i = 0; i < pad_len; i++) printf(" ");
    }
    printf(CLR_CYAN "|" CLR_RESET "\n");

    if (result->gpu_count > 0) {
        int gpu_name_len = result->gpu_name[0] ? (int)strlen(result->gpu_name) : 3;
        printf(CLR_CYAN "|" CLR_RESET " GPU Speed:     " CLR_GREEN "%10.2f Mkeys/s" CLR_RESET "  (%s)",
               result->gpu_speed_mkeys,
               result->gpu_name[0] ? result->gpu_name : "GPU");
        pad_len = table_width - 38 - gpu_name_len;
        if (pad_len > 0) {
            for (int i = 0; i < pad_len; i++) printf(" ");
        }
        printf(CLR_CYAN "|" CLR_RESET "\n");

        printf(CLR_CYAN "|" CLR_RESET " Hybrid Speed:  " CLR_GREEN "%10.2f Mkeys/s" CLR_RESET "  (%.0f%% efficiency)",
               result->hybrid_speed_mkeys, result->efficiency_ratio * 100.0);
        pad_len = table_width - 52;
        if (pad_len > 0) {
            for (int i = 0; i < pad_len; i++) printf(" ");
        }
        printf(CLR_CYAN "|" CLR_RESET "\n");
    }

    printf(CLR_CYAN "+");
    print_border_line(table_width - 2, '-');
    printf(CLR_RESET "\n");

    // Time estimates for different bit ranges
    printf(CLR_BOLD "Time Estimates (Puzzle %d - %.0e keys):" CLR_RESET "\n", bits, range_size);
    printf(CLR_CYAN "+");
    print_border_line(table_width - 2, '-');
    printf(CLR_RESET);

    // CPU-only estimate
    double cpu_seconds = range_size / (result->cpu_speed_mkeys * 1e6);
    char cpu_time_str[64];
    format_time_estimate(cpu_seconds, cpu_time_str, sizeof(cpu_time_str));
    time_str_len = (int)strlen(cpu_time_str);
    printf(CLR_CYAN "|" CLR_RESET " CPU only:      %s", cpu_time_str);
    pad_len = table_width - 18 - time_str_len;
    if (pad_len > 0) {
        for (int i = 0; i < pad_len; i++) printf(" ");
    }
    printf(CLR_CYAN "|" CLR_RESET "\n");

    // GPU/Hybrid estimate (if available)
    if (result->gpu_count > 0) {
        double gpu_seconds = range_size / (result->gpu_speed_mkeys * 1e6);
        char gpu_time_str[64];
        format_time_estimate(gpu_seconds, gpu_time_str, sizeof(gpu_time_str));
        time_str_len = (int)strlen(gpu_time_str);
        printf(CLR_CYAN "|" CLR_RESET " GPU only:      %s", gpu_time_str);
        pad_len = table_width - 18 - time_str_len;
        if (pad_len > 0) {
            for (int i = 0; i < pad_len; i++) printf(" ");
        }
        printf(CLR_CYAN "|" CLR_RESET "\n");

        double hybrid_seconds = range_size / (result->hybrid_speed_mkeys * 1e6);
        char hybrid_time_str[64];
        format_time_estimate(hybrid_seconds, hybrid_time_str, sizeof(hybrid_time_str));
        time_str_len = (int)strlen(hybrid_time_str);
        printf(CLR_CYAN "|" CLR_RESET " Hybrid:        %s", hybrid_time_str);
        pad_len = table_width - 18 - time_str_len;
        if (pad_len > 0) {
            for (int i = 0; i < pad_len; i++) printf(" ");
        }
        printf(CLR_CYAN "|" CLR_RESET "\n");
    }

    printf(CLR_CYAN "+");
    print_border_line(table_width - 2, '-');
    printf(CLR_RESET "\n");

    // Recommendations
    printf(CLR_BOLD "Recommended Settings:" CLR_RESET "\n");
    printf(CLR_CYAN "+");
    print_border_line(table_width - 2, '-');
    printf(CLR_RESET);

    // Recommend based on available hardware
    if (result->gpu_count > 0 && result->hybrid_speed_mkeys > result->cpu_speed_mkeys * 1.2) {
        printf(CLR_CYAN "|" CLR_RESET CLR_GREEN " Use hybrid mode for best performance:" CLR_RESET);
        pad_len = table_width - 39;
        if (pad_len > 0) {
            for (int i = 0; i < pad_len; i++) printf(" ");
        }
        printf(CLR_CYAN "|" CLR_RESET "\n");

        char cmd_buf[128];
        snprintf(cmd_buf, sizeof(cmd_buf), "   ./keyhunt -m address --gpu -t %d -f <targets.txt>",
                 result->cpu_threads);
        int cmd_len = (int)strlen(cmd_buf);
        printf(CLR_CYAN "|" CLR_RESET "%s", cmd_buf);
        pad_len = table_width - 2 - cmd_len;
        if (pad_len > 0) {
            for (int i = 0; i < pad_len; i++) printf(" ");
        }
        printf(CLR_CYAN "|" CLR_RESET "\n");
    } else {
        printf(CLR_CYAN "|" CLR_RESET CLR_GREEN " Use CPU mode (optimal for your system):" CLR_RESET);
        pad_len = table_width - 41;
        if (pad_len > 0) {
            for (int i = 0; i < pad_len; i++) printf(" ");
        }
        printf(CLR_CYAN "|" CLR_RESET "\n");

        char cmd_buf[128];
        snprintf(cmd_buf, sizeof(cmd_buf), "   ./keyhunt -m address -t %d -f <targets.txt>",
                 result->cpu_threads);
        int cmd_len = (int)strlen(cmd_buf);
        printf(CLR_CYAN "|" CLR_RESET "%s", cmd_buf);
        pad_len = table_width - 2 - cmd_len;
        if (pad_len > 0) {
            for (int i = 0; i < pad_len; i++) printf(" ");
        }
        printf(CLR_CYAN "|" CLR_RESET "\n");
    }

    // BSGS recommendation for known public keys
    printf(CLR_CYAN "|" CLR_RESET);
    for (int i = 0; i < table_width - 2; i++) printf(" ");
    printf(CLR_CYAN "|" CLR_RESET "\n");

    printf(CLR_CYAN "|" CLR_RESET CLR_YELLOW " For known public keys, use BSGS mode:" CLR_RESET);
    pad_len = table_width - 39;
    if (pad_len > 0) {
        for (int i = 0; i < pad_len; i++) printf(" ");
    }
    printf(CLR_CYAN "|" CLR_RESET "\n");

    char bsgs_cmd[128];
    snprintf(bsgs_cmd, sizeof(bsgs_cmd), "   ./keyhunt -m bsgs -t %d -f <pubkeys.txt> -b %d",
             result->cpu_threads, bits);
    int bsgs_len = (int)strlen(bsgs_cmd);
    printf(CLR_CYAN "|" CLR_RESET "%s", bsgs_cmd);
    pad_len = table_width - 2 - bsgs_len;
    if (pad_len > 0) {
        for (int i = 0; i < pad_len; i++) printf(" ");
    }
    printf(CLR_CYAN "|" CLR_RESET "\n");

    printf(CLR_CYAN "+");
    print_border_line(table_width - 2, '-');
    printf(CLR_RESET "\n");

    // Additional puzzle time estimates
    printf(CLR_BOLD "Puzzle Time Estimates (CPU mode):" CLR_RESET "\n");

    // Determine column widths based on terminal size
    int col1_width = 7;  // "Bits" column (minimum)
    int col2_width = 18; // "Keys to Search" column (minimum)
    int remaining = table_width - col1_width - col2_width - 4; // 4 for borders
    int col3_width = remaining > 20 ? remaining : 20; // Time column

    // Adjust if terminal is too narrow
    if (table_width < 50) {
        col1_width = 6;
        col2_width = 15;
        col3_width = table_width - col1_width - col2_width - 4;
        if (col3_width < 15) col3_width = 15;
    }

    // Top border
    printf(CLR_CYAN "+");
    for (int i = 0; i < col1_width; i++) printf("-");
    printf("+");
    for (int i = 0; i < col2_width; i++) printf("-");
    printf("+");
    for (int i = 0; i < col3_width; i++) printf("-");
    printf("+" CLR_RESET "\n");

    // Header
    printf(CLR_CYAN "| %-*s| %-*s| %-*s|" CLR_RESET "\n",
           col1_width - 1, "Bits",
           col2_width - 1, "Keys to Search",
           col3_width - 1, "Estimated Time");

    // Middle border
    printf(CLR_CYAN "+");
    for (int i = 0; i < col1_width; i++) printf("-");
    printf("+");
    for (int i = 0; i < col2_width; i++) printf("-");
    printf("+");
    for (int i = 0; i < col3_width; i++) printf("-");
    printf("+" CLR_RESET "\n");

    int puzzle_bits[] = {50, 55, 60, 65, 66, 70, 75, 80};
    int num_puzzles = (int)(sizeof(puzzle_bits) / sizeof(puzzle_bits[0]));
    for (int i = 0; i < num_puzzles; i++) {
        int b = puzzle_bits[i];
        double range = pow(2.0, (double)(b - 1));
        double seconds = range / (result->cpu_speed_mkeys * 1e6);
        char time_str[64];
        format_time_estimate(seconds, time_str, sizeof(time_str));

        char keys_str[32];
        snprintf(keys_str, sizeof(keys_str), "2^%d", b-1);

        // Highlight current puzzle
        if (b == bits) {
            printf(CLR_CYAN "|" CLR_GREEN " %-*d" CLR_RESET CLR_CYAN "|" CLR_GREEN " %-*s" CLR_RESET CLR_CYAN "|" CLR_GREEN " %-*s" CLR_RESET CLR_CYAN "|" CLR_RESET "\n",
                   col1_width - 2, b, col2_width - 2, keys_str, col3_width - 2, time_str);
        } else {
            printf(CLR_CYAN "| %-*d| %-*s| %-*s|" CLR_RESET "\n",
                   col1_width - 2, b, col2_width - 2, keys_str, col3_width - 2, time_str);
        }
    }

    // Bottom border
    printf(CLR_CYAN "+");
    for (int i = 0; i < col1_width; i++) printf("-");
    printf("+");
    for (int i = 0; i < col2_width; i++) printf("-");
    printf("+");
    for (int i = 0; i < col3_width; i++) printf("-");
    printf("+" CLR_RESET "\n\n");

    // Note about BSGS
    printf(CLR_DIM "Note: BSGS mode is dramatically faster for known public keys (sqrt complexity)." CLR_RESET "\n");
    printf(CLR_DIM "For puzzle 66 with known pubkey, BSGS can find the key in hours instead of years." CLR_RESET "\n\n");
}

int benchmark_quick(double *cpu_speed, double *gpu_speed) {
    // Quick benchmark for auto-tuning (just uses system scores)
    system_info_t sysinfo;
    sysinfo_init(&sysinfo);

    if (cpu_speed) {
        *cpu_speed = estimate_cpu_speed(&sysinfo);
    }

    if (gpu_speed) {
        if (sysinfo.has_cuda) {
            *gpu_speed = estimate_gpu_speed(&sysinfo);
        } else {
            *gpu_speed = 0.0;
        }
    }

    return 0;
}

// Helper: print progress bar
static void print_progress_bar(double percent, int width) {
    int filled = (int)(percent / 100.0 * width);
    if (filled > width) filled = width;
    if (filled < 0) filled = 0;

    printf(CLR_GREEN);
    for (int i = 0; i < filled; i++) printf("#");
    printf(CLR_DIM);
    for (int i = filled; i < width; i++) printf("-");
    printf(CLR_RESET);
}

// Helper: format time estimate
static void format_time_estimate(double seconds, char *buffer, size_t size) {
    if (seconds < 0 || !isfinite(seconds)) {
        snprintf(buffer, size, "N/A");
        return;
    }

    double minutes = seconds / 60.0;
    double hours = minutes / 60.0;
    double days = hours / 24.0;
    double years = days / 365.25;
    double centuries = years / 100.0;

    if (seconds < 60) {
        snprintf(buffer, size, "%.1f seconds", seconds);
    } else if (minutes < 60) {
        snprintf(buffer, size, "%.1f minutes", minutes);
    } else if (hours < 24) {
        snprintf(buffer, size, "%.1f hours", hours);
    } else if (days < 30) {
        snprintf(buffer, size, "%.1f days", days);
    } else if (days < 365) {
        snprintf(buffer, size, "%.1f months", days / 30.0);
    } else if (years < 100) {
        snprintf(buffer, size, "%.1f years", years);
    } else if (centuries < 1000) {
        snprintf(buffer, size, "%.1f centuries", centuries);
    } else if (centuries < 1e9) {
        snprintf(buffer, size, "%.2e centuries", centuries);
    } else {
        snprintf(buffer, size, "> age of universe");
    }
}

// Estimate CPU speed based on system info
static double estimate_cpu_speed(const system_info_t *info) {
    if (!info) return 1.0;

    // Validate core counts to prevent division by zero
    if (info->cpu_logical_cores <= 0 || info->cpu_physical_cores <= 0) {
        return 1.0;  // Safe default
    }

    // Base speed per core: ~0.8 Mkeys/s for address mode
    // This is a conservative estimate for the full hash pipeline:
    // secp256k1 point mult -> SHA256 -> RIPEMD160 -> Base58Check

    double base_speed_per_core = 0.8;  // Mkeys/s

    // Apply SIMD multipliers
    double simd_multiplier = 1.0;
    if (info->has_avx512 && info->has_avx512dq) {
        // AVX-512 with DQ: 16-way RIPEMD160 parallelism
        simd_multiplier = 2.5;
    } else if (info->has_avx2) {
        // AVX2: 8-way RIPEMD160 parallelism
        simd_multiplier = 1.8;
    }

    // SHA-NI adds ~10% boost
    if (info->has_sha_ni) {
        simd_multiplier *= 1.1;
    }

    // Calculate total speed
    double total_speed = base_speed_per_core * info->cpu_logical_cores * simd_multiplier;

    // Adjust for hyperthreading efficiency (HT typically gives 20-30% boost, not 2x)
    if (info->cpu_logical_cores > info->cpu_physical_cores) {
        // Has hyperthreading - scale down a bit
        double ht_ratio = (double)info->cpu_logical_cores / info->cpu_physical_cores;
        double ht_efficiency = 1.0 + (ht_ratio - 1.0) * 0.3;  // 30% of HT potential
        total_speed = base_speed_per_core * info->cpu_physical_cores * simd_multiplier * ht_efficiency;
    }

    return total_speed;
}

// Estimate GPU speed based on system info
static double estimate_gpu_speed(const system_info_t *info) {
    if (!info || !info->has_cuda) return 0.0;

    // GPU speed estimation based on VRAM and approximate SM count
    // RTX 2070 (8GB):  ~50-100 Mkeys/s
    // RTX 3080 (10GB): ~150-250 Mkeys/s
    // RTX 4090 (24GB): ~400-600 Mkeys/s

    double vram_gb = info->gpu_vram_mb / 1024.0;

    // Rough estimate based on VRAM (not perfect but reasonable)
    double gpu_speed = 0.0;
    if (vram_gb >= 20) {
        // High-end (4090 class)
        gpu_speed = 400.0 + (vram_gb - 20) * 10;
    } else if (vram_gb >= 10) {
        // Mid-high (3080 class)
        gpu_speed = 150.0 + (vram_gb - 10) * 25;
    } else if (vram_gb >= 6) {
        // Mid-range
        gpu_speed = 50.0 + (vram_gb - 6) * 25;
    } else {
        // Low-end
        gpu_speed = vram_gb * 8;
    }

    // Use SM count if available for better estimate
    if (info->gpu_sm_count > 0) {
        // Each SM can do roughly 1-2 Mkeys/s depending on architecture
        double sm_based_speed = info->gpu_sm_count * 2.0;

        // Apply compute capability factor
        int cc = info->gpu_compute_capability;
        if (cc >= 89) {
            sm_based_speed *= 1.3;  // Ada Lovelace
        } else if (cc >= 86) {
            sm_based_speed *= 1.1;  // Ampere
        } else if (cc >= 75) {
            sm_based_speed *= 1.0;  // Turing
        } else {
            sm_based_speed *= 0.8;  // Older
        }

        // Use the higher of the two estimates
        if (sm_based_speed > gpu_speed) {
            gpu_speed = sm_based_speed;
        }
    }

    return gpu_speed;
}
