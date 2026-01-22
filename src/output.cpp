// src/output.cpp
#include "output.h"
#include <stdio.h>
#include <stdarg.h>

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
        // Full banner
        printf("\n");
        printf(CLR_CYAN "╔════════════════════════════════════════════════════════════╗" CLR_RESET "\n");
        printf(CLR_CYAN "║" CLR_RESET CLR_BOLD "  KEYHUNT %-48s" CLR_RESET CLR_CYAN "║" CLR_RESET "\n", version);
        printf(CLR_CYAN "╠════════════════════════════════════════════════════════════╣" CLR_RESET "\n");
        printf(CLR_CYAN "║" CLR_RESET "  Mode: %-10s  Threads: %-4d  Bits: %-4d            " CLR_CYAN "║" CLR_RESET "\n",
               mode, threads, bits);
        if (gpu_name && gpu_name[0]) {
            printf(CLR_CYAN "║" CLR_RESET "  GPU: %-52s" CLR_CYAN "║" CLR_RESET "\n", gpu_name);
        }
        printf(CLR_CYAN "╚════════════════════════════════════════════════════════════╝" CLR_RESET "\n\n");
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

void output_info(const char *fmt, ...) {
    if (g_output_level < OUTPUT_NORMAL || !fmt) return;

    va_list args;
    va_start(args, fmt);
    printf(CLR_CYAN "[I]" CLR_RESET " ");
    vprintf(fmt, args);
    va_end(args);
}

void output_success(const char *fmt, ...) {
    if (g_output_level < OUTPUT_MINIMAL || !fmt) return;

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

    printf("\n\n");
    printf(CLR_GREEN "╔════════════════════════════════════════════════════════════════╗" CLR_RESET "\n");
    printf(CLR_GREEN "║" CLR_BOLD CLR_YELLOW "              ★ PRIVATE KEY FOUND! ★                         " CLR_RESET CLR_GREEN "║" CLR_RESET "\n");
    printf(CLR_GREEN "╠════════════════════════════════════════════════════════════════╣" CLR_RESET "\n");
    printf(CLR_GREEN "║" CLR_RESET " Private Key: " CLR_BOLD "%-50.50s" CLR_RESET CLR_GREEN "║" CLR_RESET "\n", private_key);
    printf(CLR_GREEN "║" CLR_RESET " Address:     " CLR_BOLD "%-50.50s" CLR_RESET CLR_GREEN "║" CLR_RESET "\n", address);
    if (public_key && public_key[0]) {
        printf(CLR_GREEN "║" CLR_RESET " Public Key:  %-50.50s" CLR_GREEN "║" CLR_RESET "\n", public_key);
    }
    printf(CLR_GREEN "╚════════════════════════════════════════════════════════════════╝" CLR_RESET "\n\n");
}

void output_final_stats(uint64_t total_keys, double total_time_sec,
                        double avg_speed, int keys_found) {
    if (g_output_level == OUTPUT_SILENT) return;

    printf("\n\n");
    printf(CLR_CYAN "═══════════════════════════════════════════════════════════════" CLR_RESET "\n");
    printf(CLR_BOLD "                        FINAL STATISTICS                        " CLR_RESET "\n");
    printf(CLR_CYAN "═══════════════════════════════════════════════════════════════" CLR_RESET "\n");
    printf("  Total keys checked:  %.2e\n", (double)total_keys);
    printf("  Total time:          %.0f seconds (%.1f hours)\n",
           total_time_sec, total_time_sec/3600.0);
    printf("  Average speed:       %.2f Mkeys/s\n", avg_speed);
    printf("  Keys found:          %d\n", keys_found);
    printf(CLR_CYAN "═══════════════════════════════════════════════════════════════" CLR_RESET "\n\n");
}
