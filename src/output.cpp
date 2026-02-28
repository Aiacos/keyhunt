// src/output.cpp
#include "output.h"
#include "platform/platform.h"
#include <stdio.h>
#include <stdarg.h>
#if !PLATFORM_WINDOWS
#include <unistd.h>
#endif

static output_level_t g_output_level = OUTPUT_NORMAL;

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

// Box drawing characters
#define BOX_TL      "╔"
#define BOX_TR      "╗"
#define BOX_BL      "╚"
#define BOX_BR      "╝"
#define BOX_H       "═"
#define BOX_V       "║"

// Max ETA seconds (1 year)
#define MAX_ETA_SECONDS (365*24*3600)

void output_init(output_level_t level) {
    g_output_level = level;
}

void output_set_level(output_level_t level) {
    g_output_level = level;
}

output_level_t output_get_level(void) {
    return g_output_level;
}

void output_banner(const char *version, const char *mode, int threads,
                   const char *gpu_name, int bits) {
    if (g_output_level == OUTPUT_SILENT) return;

    if (g_output_level == OUTPUT_MINIMAL) {
        // Single line banner
        printf("keyhunt %s | %s mode | %d threads", version, mode, threads);
        if (gpu_name && gpu_name[0]) {
            printf(" | GPU: %s", gpu_name);
        }
        if (bits > 0) {
            printf(" | %d bits", bits);
        }
        printf("\n\n");
    } else {
        // Full banner with responsive width
        int width = platform_terminal_width();
        if (width > 80) width = 80;  /* Cap at reasonable width */
        if (width < 50) width = 50;  /* Minimum for readability */

        char title_line[256];
        char info_line[256];
        char gpu_line[256];

        snprintf(title_line, sizeof(title_line), "  KEYHUNT %s", version);
        snprintf(info_line, sizeof(info_line), "  Mode: %s  Threads: %d  Bits: %d",
                 mode, threads, bits);
        if (gpu_name && gpu_name[0]) {
            snprintf(gpu_line, sizeof(gpu_line), "  GPU: %s", gpu_name);
        }

        printf("\n");
        printf(CLR_CYAN BOX_TL);
        for (int i = 0; i < width - 2; i++) printf(BOX_H);
        printf(BOX_TR CLR_RESET "\n");

        printf(CLR_CYAN BOX_V CLR_RESET CLR_BOLD " %-*s" CLR_RESET CLR_CYAN BOX_V CLR_RESET "\n",
               width - 3, title_line);

        printf(CLR_CYAN "╠");
        for (int i = 0; i < width - 2; i++) printf(BOX_H);
        printf("╣" CLR_RESET "\n");

        printf(CLR_CYAN BOX_V CLR_RESET " %-*s" CLR_CYAN BOX_V CLR_RESET "\n",
               width - 3, info_line);

        if (gpu_name && gpu_name[0]) {
            printf(CLR_CYAN BOX_V CLR_RESET " %-*s" CLR_CYAN BOX_V CLR_RESET "\n",
                   width - 3, gpu_line);
        }

        printf(CLR_CYAN BOX_BL);
        for (int i = 0; i < width - 2; i++) printf(BOX_H);
        printf(BOX_BR CLR_RESET "\n\n");
    }
    fflush(stdout);
}

void output_progress(double percent, double speed_mkeys,
                     uint64_t keys_checked, int eta_seconds) {
    if (g_output_level == OUTPUT_SILENT) return;

    // Format ETA
    char eta_str[32];
    if (eta_seconds < 0 || eta_seconds > MAX_ETA_SECONDS) {
        snprintf(eta_str, sizeof(eta_str), "N/A");
    } else if (eta_seconds < 3600) {
        snprintf(eta_str, sizeof(eta_str), "%dm %ds", eta_seconds/60, eta_seconds%60);
    } else if (eta_seconds < 86400) {
        snprintf(eta_str, sizeof(eta_str), "%dh %dm", eta_seconds/3600, (eta_seconds%3600)/60);
    } else {
        snprintf(eta_str, sizeof(eta_str), "%dd %dh", eta_seconds/86400, (eta_seconds%86400)/3600);
    }

    if (g_output_level == OUTPUT_MINIMAL) {
        // Clean single line with progress bar
        printf("\r");
        output_progress_bar(percent, 30);
        printf(" %.1f%% | %.2f Mkeys/s | ETA: %s   ", percent, speed_mkeys, eta_str);
        fflush(stdout);
    } else {
        // More detailed progress
        printf("\r[" CLR_CYAN "Progress" CLR_RESET "] ");
        output_progress_bar(percent, 25);
        printf(" %.2f%% | %.2f Mkeys/s | Keys: %.2e | ETA: %s   ",
               percent, speed_mkeys, (double)keys_checked, eta_str);
        fflush(stdout);
    }
}

void output_progress_detailed(double percent, double speed_mkeys,
                              uint64_t keys_checked, int eta_seconds,
                              uint64_t memory_used_mb, uint64_t memory_total_mb,
                              double *speed_history, int speed_history_count,
                              int active_threads) {
    // Only show detailed progress in NORMAL and VERBOSE modes
    if (g_output_level < OUTPUT_NORMAL) return;

    // Get terminal width for responsive layout
    int width = platform_terminal_width();
    if (width > 80) width = 80;  /* Cap at reasonable width */
    if (width < 60) width = 60;  /* Minimum for detailed display */

    // Format ETA
    char eta_str[32];
    if (eta_seconds < 0 || eta_seconds > MAX_ETA_SECONDS) {
        snprintf(eta_str, sizeof(eta_str), "N/A");
    } else if (eta_seconds < 3600) {
        snprintf(eta_str, sizeof(eta_str), "%dm %ds", eta_seconds/60, eta_seconds%60);
    } else if (eta_seconds < 86400) {
        snprintf(eta_str, sizeof(eta_str), "%dh %dm", eta_seconds/3600, (eta_seconds%3600)/60);
    } else {
        snprintf(eta_str, sizeof(eta_str), "%dd %dh", eta_seconds/86400, (eta_seconds%86400)/3600);
    }

    // Clear previous output and move up (10 lines for the box)
    printf("\033[10A\033[J");

    // Top border
    printf(CLR_CYAN BOX_TL);
    for (int i = 0; i < width - 2; i++) printf(BOX_H);
    printf(BOX_TR CLR_RESET "\n");

    // Title line
    printf(CLR_CYAN BOX_V CLR_RESET CLR_BOLD " %-*s" CLR_RESET CLR_CYAN BOX_V CLR_RESET "\n",
           width - 3, "SEARCH PROGRESS");

    // Separator
    printf(CLR_CYAN "╠");
    for (int i = 0; i < width - 2; i++) printf(BOX_H);
    printf("╣" CLR_RESET "\n");

    // Progress bar line
    printf(CLR_CYAN BOX_V CLR_RESET " Progress: ");
    output_progress_bar(percent, 30);
    printf(" %.2f%% " CLR_CYAN BOX_V CLR_RESET "\n", percent);

    // Speed line with graph
    printf(CLR_CYAN BOX_V CLR_RESET " Speed:    %.2f Mkeys/s ", speed_mkeys);
    if (speed_history && speed_history_count > 0) {
        printf("[");
        output_speed_graph(speed_history, speed_history_count, 3);
        printf("]");
    }
    printf(" " CLR_CYAN BOX_V CLR_RESET "\n");

    // Keys checked line
    char keys_str[64];
    if (keys_checked >= 1e12) {
        snprintf(keys_str, sizeof(keys_str), "%.2fT keys", keys_checked / 1e12);
    } else if (keys_checked >= 1e9) {
        snprintf(keys_str, sizeof(keys_str), "%.2fG keys", keys_checked / 1e9);
    } else if (keys_checked >= 1e6) {
        snprintf(keys_str, sizeof(keys_str), "%.2fM keys", keys_checked / 1e6);
    } else {
        snprintf(keys_str, sizeof(keys_str), "%.2fK keys", keys_checked / 1e3);
    }
    printf(CLR_CYAN BOX_V CLR_RESET " Keys:     %-*s" CLR_CYAN BOX_V CLR_RESET "\n",
           width - 13, keys_str);

    // ETA line
    printf(CLR_CYAN BOX_V CLR_RESET " ETA:      %-*s" CLR_CYAN BOX_V CLR_RESET "\n",
           width - 13, eta_str);

    // Memory usage line with bar
    if (memory_total_mb > 0) {
        printf(CLR_CYAN BOX_V CLR_RESET " Memory:   ");
        output_memory_bar(memory_used_mb, memory_total_mb, 30);
        printf(" %llu/%llu MB " CLR_CYAN BOX_V CLR_RESET "\n",
               (unsigned long long)memory_used_mb, (unsigned long long)memory_total_mb);
    } else {
        printf(CLR_CYAN BOX_V CLR_RESET " Memory:   %-*s" CLR_CYAN BOX_V CLR_RESET "\n",
               width - 13, "N/A");
    }

    // Threads line
    printf(CLR_CYAN BOX_V CLR_RESET " Threads:  %-*d" CLR_CYAN BOX_V CLR_RESET "\n",
           width - 13, active_threads);

    // Bottom border
    printf(CLR_CYAN BOX_BL);
    for (int i = 0; i < width - 2; i++) printf(BOX_H);
    printf(BOX_BR CLR_RESET "\n");

    fflush(stdout);
}

void output_progress_bar(double percent, int width) {
    int filled = (int)(percent / 100.0 * width);
    if (filled > width) filled = width;
    if (filled < 0) filled = 0;

    printf(CLR_GREEN);
    for (int i = 0; i < filled; i++) printf("█");
    printf(CLR_DIM);
    for (int i = filled; i < width; i++) printf("░");
    printf(CLR_RESET);
}

void output_memory_bar(uint64_t used_mb, uint64_t total_mb, int width) {
    // Validate inputs
    if (total_mb == 0) return;
    if (width < 5) width = 5;
    if (width > 60) width = 60;

    // Calculate percentage
    double percent = (double)used_mb / (double)total_mb * 100.0;
    int filled = (int)(percent / 100.0 * width);
    if (filled > width) filled = width;
    if (filled < 0) filled = 0;

    // Choose color based on usage level
    const char *color;
    if (percent < 60.0) {
        color = CLR_GREEN;      // Safe: < 60%
    } else if (percent < 80.0) {
        color = CLR_YELLOW;     // Warning: 60-80%
    } else {
        color = CLR_RED;        // Danger: > 80%
    }

    // Render bar
    printf("%s", color);
    for (int i = 0; i < filled; i++) printf("█");
    printf(CLR_DIM);
    for (int i = filled; i < width; i++) printf("░");
    printf(CLR_RESET);
}

void output_speed_graph(double *values, int count, int height) {
    // Validate inputs
    if (!values || count <= 0) return;
    if (height < 3) height = 3;
    if (height > 5) height = 5;

    // Find min/max for scaling
    double min_val = values[0];
    double max_val = values[0];
    for (int i = 1; i < count; i++) {
        if (values[i] < min_val) min_val = values[i];
        if (values[i] > max_val) max_val = values[i];
    }

    // Avoid division by zero
    double range = max_val - min_val;
    if (range < 0.001) range = 0.001;

    // Block characters for 8 levels of height (1/8 to 8/8)
    const char *blocks[] = {" ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};

    // Render graph from top to bottom
    printf(CLR_CYAN);
    for (int i = 0; i < count; i++) {
        // Normalize value to 0-8 range
        double normalized = ((values[i] - min_val) / range) * 8.0;
        int level = (int)(normalized + 0.5);  // Round to nearest
        if (level < 0) level = 0;
        if (level > 8) level = 8;

        printf("%s", blocks[level]);
    }
    printf(CLR_RESET);
    fflush(stdout);
}

void output_info(const char *fmt, ...) {
    if (g_output_level < OUTPUT_NORMAL || !fmt) return;

    va_list args;
    va_start(args, fmt);
    printf(CLR_CYAN "[I]" CLR_RESET " ");
    vprintf(fmt, args);
    va_end(args);
}

void output_success(const char *fmt, ...) {
    // Success messages only in NORMAL and VERBOSE modes
    // MINIMAL mode should only show progress and key found
    if (g_output_level < OUTPUT_NORMAL || !fmt) return;

    va_list args;
    va_start(args, fmt);
    printf(CLR_GREEN "[+]" CLR_RESET " ");
    vprintf(fmt, args);
    va_end(args);
}

// Note: warnings and errors always output regardless of verbosity level
// since they represent critical information that should never be silenced
void output_warning(const char *fmt, ...) {
    if (!fmt) return;

    va_list args;
    va_start(args, fmt);
    fprintf(stderr, CLR_YELLOW "[W]" CLR_RESET " ");
    vfprintf(stderr, fmt, args);
    va_end(args);
}

void output_error(const char *fmt, ...) {
    if (!fmt) return;

    va_list args;
    va_start(args, fmt);
    fprintf(stderr, CLR_RED "[E]" CLR_RESET " ");
    vfprintf(stderr, fmt, args);
    va_end(args);
}

void output_key_found(const char *private_key, const char *address,
                      const char *public_key) {
    if (!private_key || !address) return;

    int width = platform_terminal_width();
    if (width > 80) width = 80;  /* Cap at reasonable width */
    if (width < 50) width = 50;  /* Minimum for readability */

    char title_line[256] = "              ★ PRIVATE KEY FOUND! ★";
    char pk_line[256];
    char addr_line[256];
    char pub_line[256];

    snprintf(pk_line, sizeof(pk_line), " Private Key: %s", private_key);
    snprintf(addr_line, sizeof(addr_line), " Address:     %s", address);
    if (public_key && public_key[0]) {
        snprintf(pub_line, sizeof(pub_line), " Public Key:  %s", public_key);
    }

    printf("\n\n");
    printf(CLR_GREEN BOX_TL);
    for (int i = 0; i < width - 2; i++) printf(BOX_H);
    printf(BOX_TR CLR_RESET "\n");

    printf(CLR_GREEN BOX_V CLR_BOLD CLR_YELLOW " %-*s" CLR_RESET CLR_GREEN BOX_V CLR_RESET "\n",
           width - 3, title_line);

    printf(CLR_GREEN "╠");
    for (int i = 0; i < width - 2; i++) printf(BOX_H);
    printf("╣" CLR_RESET "\n");

    printf(CLR_GREEN BOX_V CLR_RESET CLR_BOLD " %-*.*s" CLR_RESET CLR_GREEN BOX_V CLR_RESET "\n",
           width - 3, width - 3, pk_line);
    printf(CLR_GREEN BOX_V CLR_RESET CLR_BOLD " %-*.*s" CLR_RESET CLR_GREEN BOX_V CLR_RESET "\n",
           width - 3, width - 3, addr_line);

    if (public_key && public_key[0]) {
        printf(CLR_GREEN BOX_V CLR_RESET " %-*.*s" CLR_GREEN BOX_V CLR_RESET "\n",
               width - 3, width - 3, pub_line);
    }

    printf(CLR_GREEN BOX_BL);
    for (int i = 0; i < width - 2; i++) printf(BOX_H);
    printf(BOX_BR CLR_RESET "\n\n");

    fflush(stdout);  // Ensure key found message is immediately visible
}

void output_final_stats(uint64_t total_keys, double total_time_sec,
                        double avg_speed, int keys_found) {
    if (g_output_level == OUTPUT_SILENT) return;

    int width = platform_terminal_width();
    if (width > 80) width = 80;  /* Cap at reasonable width */
    if (width < 50) width = 50;  /* Minimum for readability */

    printf("\n\n");
    printf(CLR_CYAN);
    for (int i = 0; i < width; i++) printf(BOX_H);
    printf(CLR_RESET "\n");

    printf(CLR_BOLD "%-*s" CLR_RESET "\n", width, "                        FINAL STATISTICS");

    printf(CLR_CYAN);
    for (int i = 0; i < width; i++) printf(BOX_H);
    printf(CLR_RESET "\n");

    printf("  Total keys checked:  %.2e\n", (double)total_keys);
    printf("  Total time:          %.0f seconds (%.1f hours)\n",
           total_time_sec, total_time_sec/3600.0);
    printf("  Average speed:       %.2f Mkeys/s\n", avg_speed);
    printf("  Keys found:          %d\n", keys_found);

    printf(CLR_CYAN);
    for (int i = 0; i < width; i++) printf(BOX_H);
    printf(CLR_RESET "\n\n");
}

void output_gpu_stats(int device_count, const int *device_ids,
                      const uint64_t *keys_processed,
                      const double *throughput_mkeys,
                      const char **device_names) {
    if (g_output_level == OUTPUT_SILENT) return;
    if (!device_ids || !keys_processed || !throughput_mkeys) return;
    if (device_count <= 0) return;

    // Clear current line if in progress display mode
    printf("\n");

    if (g_output_level == OUTPUT_MINIMAL) {
        // Compact single-line format for minimal mode
        printf("[" CLR_CYAN "GPU" CLR_RESET "] ");
        for (int i = 0; i < device_count; i++) {
            if (i > 0) printf(" | ");
            printf("GPU%d: %.1f MK/s", device_ids[i], throughput_mkeys[i]);
        }
        printf("\n");
    } else {
        // Detailed multi-line format for normal/verbose modes
        printf(CLR_CYAN);
        printf("┌─ GPU Statistics ─────────────────────────────────────────────┐\n");
        printf(CLR_RESET);

        for (int i = 0; i < device_count; i++) {
            // Format keys processed in human-readable units (K, M, B)
            char keys_str[32];
            double keys = (double)keys_processed[i];
            if (keys >= 1e9) {
                snprintf(keys_str, sizeof(keys_str), "%.2fB", keys / 1e9);
            } else if (keys >= 1e6) {
                snprintf(keys_str, sizeof(keys_str), "%.2fM", keys / 1e6);
            } else if (keys >= 1e3) {
                snprintf(keys_str, sizeof(keys_str), "%.2fK", keys / 1e3);
            } else {
                snprintf(keys_str, sizeof(keys_str), "%.0f", keys);
            }

            // Format device line
            printf(CLR_CYAN "│" CLR_RESET " ");
            printf(CLR_BOLD "GPU %d" CLR_RESET, device_ids[i]);

            // Add device name if available
            if (device_names && device_names[i] && device_names[i][0]) {
                printf(": " CLR_GREEN "%-20s" CLR_RESET, device_names[i]);
            } else {
                printf(":                       ");
            }

            // Throughput
            printf(" │ " CLR_YELLOW "%.2f" CLR_RESET " MK/s", throughput_mkeys[i]);

            // Keys processed
            printf(" │ " CLR_BLUE "%9s" CLR_RESET " keys", keys_str);

            printf("\n");
        }

        printf(CLR_CYAN);
        printf("└──────────────────────────────────────────────────────────────┘\n");
        printf(CLR_RESET);
    }

    fflush(stdout);
}
