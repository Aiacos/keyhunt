// src/output.h
#ifndef OUTPUT_H
#define OUTPUT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Output verbosity levels
typedef enum {
    OUTPUT_SILENT = 0,   // No output except errors and results
    OUTPUT_MINIMAL = 1,  // Clean single-line progress
    OUTPUT_NORMAL = 2,   // Standard output (default)
    OUTPUT_VERBOSE = 3   // Debug-level output
} output_level_t;

// Initialize output system
void output_init(output_level_t level);

// Set output level
void output_set_level(output_level_t level);
output_level_t output_get_level(void);

// Startup banner
void output_banner(const char *version, const char *mode, int threads,
                   const char *gpu_name, int bits);

// Progress display (single-line, overwrites previous)
void output_progress(double percent, double speed_mkeys,
                     uint64_t keys_checked, int eta_seconds);

// Status messages (respects verbosity)
void output_info(const char *fmt, ...);      // [I] prefix
void output_success(const char *fmt, ...);   // [+] prefix
void output_warning(const char *fmt, ...);   // [W] prefix
void output_error(const char *fmt, ...);     // [E] prefix

// Key found celebration
void output_key_found(const char *private_key, const char *address,
                      const char *public_key);

// Final statistics
void output_final_stats(uint64_t total_keys, double total_time_sec,
                        double avg_speed, int keys_found);

// Progress bar helper
void output_progress_bar(double percent, int width);

// ASCII speed graph (uses block characters ▁▂▃▄▅▆▇█)
void output_speed_graph(double *values, int count, int height);

#ifdef __cplusplus
}
#endif

#endif // OUTPUT_H
