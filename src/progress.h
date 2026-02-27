// src/progress.h
#ifndef PROGRESS_H
#define PROGRESS_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROGRESS_DIR "~/.keyhunt/progress"
#define PROGRESS_AUTOSAVE_INTERVAL 60  // seconds
#define SPEED_HISTORY_MAX_SAMPLES 60  // Track last 60 samples (1 per second = 1 minute of history)

// Speed sample for history tracking
typedef struct {
    time_t timestamp;         // When the sample was taken
    double keys_per_second;   // Speed at that moment
} speed_sample_t;

// Speed history tracking (circular buffer)
typedef struct {
    speed_sample_t samples[SPEED_HISTORY_MAX_SAMPLES];
    int count;          // Current number of samples (0 to MAX_SAMPLES)
    int write_index;    // Next position to write (circular buffer index)
} speed_history_t;

// Progress state structure
typedef struct {
    // Identity
    char mode[32];
    char target_file[256];
    int bits;

    // Range info
    char range_start[68];
    char range_end[68];
    char current_position[68];

    // Statistics
    uint64_t keys_checked;
    uint64_t keys_total;
    double elapsed_seconds;
    time_t start_time;
    time_t last_save_time;

    // State
    bool is_random_mode;
    int thread_count;

    // For random mode: track checked ranges
    int ranges_completed;
    int ranges_total;

    // Speed tracking
    speed_history_t speed_history;
} progress_state_t;

// Initialize progress system (creates directory if needed)
int progress_init(void);

// Create new progress file for a search session
int progress_create(progress_state_t *state, const char *mode,
                    const char *target_file, int bits,
                    const char *range_start, const char *range_end);

// Load existing progress (returns 0 if found, -1 if not)
int progress_load(progress_state_t *state, const char *mode,
                  const char *target_file, int bits);

// Update progress (call periodically, auto-saves every 60s)
int progress_update(progress_state_t *state, const char *current_pos,
                    uint64_t keys_checked);

// Force save progress
int progress_save(const progress_state_t *state);

// Mark progress as complete (deletes file)
int progress_complete(progress_state_t *state);

// Get progress file path for given parameters
void progress_get_filepath(char *path, size_t path_size,
                           const char *mode, const char *target_file, int bits);

// Check if progress exists
bool progress_exists(const char *mode, const char *target_file, int bits);

// List all saved progress files
int progress_list(void);

// Delete specific progress
int progress_delete(const char *mode, const char *target_file, int bits);

// Delete all progress files
int progress_clear_all(void);

// Speed history functions
// Initialize speed history
void speed_history_init(speed_history_t *history);

// Add a speed sample to history
void speed_history_add_sample(speed_history_t *history, double keys_per_second);

// Get average speed over all samples
double speed_history_get_average(const speed_history_t *history);

// Get average speed over recent N samples (or all if fewer)
double speed_history_get_recent_average(const speed_history_t *history, int sample_count);

// Get speed trend (-1 = decreasing, 0 = stable, 1 = increasing)
int speed_history_get_trend(const speed_history_t *history);

#ifdef __cplusplus
}
#endif

#endif // PROGRESS_H
