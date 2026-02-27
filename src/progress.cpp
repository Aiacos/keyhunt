// src/progress.cpp
#include "progress.h"
#include "platform/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>

#include <sys/stat.h>
#include <sys/types.h>

#if PLATFORM_WINDOWS
#include <io.h>        /* _unlink, _access */
#define unlink _unlink
#else
#include <unistd.h>    /* unlink */
#endif

// Simple hash function for generating unique filenames
static unsigned int simple_hash(const char *str) {
    if (!str) return 0;
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

// Escape a string for JSON output (handles quotes, backslashes, newlines)
static void json_escape_string(FILE *f, const char *str) {
    if (!f || !str) return;

    for (const char *p = str; *p; p++) {
        switch (*p) {
            case '"':  fputs("\\\"", f); break;
            case '\\': fputs("\\\\", f); break;
            case '\n': fputs("\\n", f); break;
            case '\r': fputs("\\r", f); break;
            case '\t': fputs("\\t", f); break;
            default:   fputc(*p, f); break;
        }
    }
}

// Get expanded progress directory path (replaces ~ with $HOME)
static int get_progress_dir(char *path, size_t path_size) {
    if (!path || path_size == 0) return -1;

    const char *home = getenv("HOME");
    if (!home || !home[0]) {
        home = "/tmp";
    }

    int ret = snprintf(path, path_size, "%s/.keyhunt/progress", home);
    if (ret < 0 || (size_t)ret >= path_size) {
        return -1;
    }
    return 0;
}

// Recursive mkdir (like mkdir -p)
static int mkdirp(const char *path) {
    if (!path) return -1;

    char tmp[512];
    char *p = NULL;
    size_t len;

    int ret = snprintf(tmp, sizeof(tmp), "%s", path);
    if (ret < 0 || (size_t)ret >= sizeof(tmp)) {
        return -1;
    }

    len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            /* Ignore error if directory already exists */
            platform_dir_create(tmp);
            *p = '/';
        }
    }

    /* Final directory creation */
    if (platform_dir_create(tmp) != 0 && !platform_dir_exists(tmp)) {
        return -1;
    }

    return 0;
}

// Initialize progress system
int progress_init(void) {
    char dir[512];
    if (get_progress_dir(dir, sizeof(dir)) != 0) {
        return -1;
    }
    return mkdirp(dir);
}

// Get progress file path for given parameters
void progress_get_filepath(char *path, size_t path_size,
                           const char *mode, const char *target_file, int bits) {
    if (!path || path_size == 0) return;

    char dir[512];
    if (get_progress_dir(dir, sizeof(dir)) != 0) {
        path[0] = '\0';
        return;
    }

    unsigned int hash = simple_hash(target_file);
    snprintf(path, path_size, "%s/%s_%d_%08x.json",
             dir, mode ? mode : "unknown", bits, hash);
}

// Check if progress exists
bool progress_exists(const char *mode, const char *target_file, int bits) {
    char filepath[512];
    progress_get_filepath(filepath, sizeof(filepath), mode, target_file, bits);

    if (filepath[0] == '\0') return false;

    struct stat st;
    return (stat(filepath, &st) == 0);
}

// Create new progress file for a search session
int progress_create(progress_state_t *state, const char *mode,
                    const char *target_file, int bits,
                    const char *range_start, const char *range_end) {
    if (!state) return -1;

    // Initialize progress system
    if (progress_init() != 0) {
        return -1;
    }

    // Clear state
    memset(state, 0, sizeof(progress_state_t));

    // Set identity
    if (mode) {
        snprintf(state->mode, sizeof(state->mode), "%s", mode);
    }
    if (target_file) {
        snprintf(state->target_file, sizeof(state->target_file), "%s", target_file);
    }
    state->bits = bits;

    // Set range info
    if (range_start) {
        snprintf(state->range_start, sizeof(state->range_start), "%s", range_start);
        snprintf(state->current_position, sizeof(state->current_position), "%s", range_start);
    }
    if (range_end) {
        snprintf(state->range_end, sizeof(state->range_end), "%s", range_end);
    }

    // Initialize statistics
    state->keys_checked = 0;
    state->keys_total = 0;
    state->elapsed_seconds = 0.0;
    state->start_time = time(NULL);
    state->last_save_time = state->start_time;

    // Initialize speed history
    speed_history_init(&state->speed_history);

    // Save initial state
    return progress_save(state);
}

// Parse a string value from JSON (simple implementation)
static int parse_json_string(const char *json, const char *key, char *value, size_t value_size) {
    if (!json || !key || !value || value_size == 0) return -1;

    char search_key[128];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);

    const char *pos = strstr(json, search_key);
    if (!pos) return -1;

    pos = strchr(pos, ':');
    if (!pos) return -1;
    pos++;

    // Skip whitespace
    while (*pos == ' ' || *pos == '\t' || *pos == '\n') pos++;

    if (*pos != '"') return -1;
    pos++;

    // Copy until closing quote
    size_t i = 0;
    while (*pos && *pos != '"' && i < value_size - 1) {
        value[i++] = *pos++;
    }
    value[i] = '\0';

    return 0;
}

// Parse an integer value from JSON
static int parse_json_int(const char *json, const char *key, int *value) {
    if (!json || !key || !value) return -1;

    char search_key[128];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);

    const char *pos = strstr(json, search_key);
    if (!pos) return -1;

    pos = strchr(pos, ':');
    if (!pos) return -1;
    pos++;

    // Skip whitespace
    while (*pos == ' ' || *pos == '\t' || *pos == '\n') pos++;

    *value = atoi(pos);
    return 0;
}

// Parse a uint64 value from JSON
static int parse_json_uint64(const char *json, const char *key, uint64_t *value) {
    if (!json || !key || !value) return -1;

    char search_key[128];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);

    const char *pos = strstr(json, search_key);
    if (!pos) return -1;

    pos = strchr(pos, ':');
    if (!pos) return -1;
    pos++;

    // Skip whitespace
    while (*pos == ' ' || *pos == '\t' || *pos == '\n') pos++;

    *value = strtoull(pos, NULL, 10);
    return 0;
}

// Parse a double value from JSON
static int parse_json_double(const char *json, const char *key, double *value) {
    if (!json || !key || !value) return -1;

    char search_key[128];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);

    const char *pos = strstr(json, search_key);
    if (!pos) return -1;

    pos = strchr(pos, ':');
    if (!pos) return -1;
    pos++;

    // Skip whitespace
    while (*pos == ' ' || *pos == '\t' || *pos == '\n') pos++;

    *value = strtod(pos, NULL);
    return 0;
}

// Parse a boolean value from JSON
static int parse_json_bool(const char *json, const char *key, bool *value) {
    if (!json || !key || !value) return -1;

    char search_key[128];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);

    const char *pos = strstr(json, search_key);
    if (!pos) return -1;

    pos = strchr(pos, ':');
    if (!pos) return -1;
    pos++;

    // Skip whitespace
    while (*pos == ' ' || *pos == '\t' || *pos == '\n') pos++;

    *value = (strncmp(pos, "true", 4) == 0);
    return 0;
}

// Parse a time_t value from JSON
static int parse_json_time(const char *json, const char *key, time_t *value) {
    if (!json || !key || !value) return -1;

    char search_key[128];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);

    const char *pos = strstr(json, search_key);
    if (!pos) return -1;

    pos = strchr(pos, ':');
    if (!pos) return -1;
    pos++;

    // Skip whitespace
    while (*pos == ' ' || *pos == '\t' || *pos == '\n') pos++;

    *value = (time_t)strtoll(pos, NULL, 10);
    return 0;
}

// Load existing progress
int progress_load(progress_state_t *state, const char *mode,
                  const char *target_file, int bits) {
    if (!state) return -1;

    char filepath[512];
    progress_get_filepath(filepath, sizeof(filepath), mode, target_file, bits);

    if (filepath[0] == '\0') return -1;

    FILE *f = fopen(filepath, "r");
    if (!f) return -1;

    // Read entire file
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > 16384) {
        fclose(f);
        return -1;
    }

    char *json = (char *)malloc(size + 1);
    if (!json) {
        fclose(f);
        return -1;
    }

    size_t read_size = fread(json, 1, size, f);
    fclose(f);

    if ((long)read_size != size) {
        free(json);
        return -1;
    }
    json[size] = '\0';

    // Clear state first
    memset(state, 0, sizeof(progress_state_t));

    // Parse JSON fields
    parse_json_string(json, "mode", state->mode, sizeof(state->mode));
    parse_json_string(json, "target_file", state->target_file, sizeof(state->target_file));
    parse_json_int(json, "bits", &state->bits);

    parse_json_string(json, "range_start", state->range_start, sizeof(state->range_start));
    parse_json_string(json, "range_end", state->range_end, sizeof(state->range_end));
    parse_json_string(json, "current_position", state->current_position, sizeof(state->current_position));

    parse_json_uint64(json, "keys_checked", &state->keys_checked);
    parse_json_uint64(json, "keys_total", &state->keys_total);
    parse_json_double(json, "elapsed_seconds", &state->elapsed_seconds);
    parse_json_time(json, "start_time", &state->start_time);
    parse_json_time(json, "last_save_time", &state->last_save_time);

    parse_json_bool(json, "is_random_mode", &state->is_random_mode);
    parse_json_int(json, "thread_count", &state->thread_count);
    parse_json_int(json, "ranges_completed", &state->ranges_completed);
    parse_json_int(json, "ranges_total", &state->ranges_total);

    // Initialize speed history (reset on load, as it's runtime data)
    speed_history_init(&state->speed_history);

    free(json);
    return 0;
}

// Force save progress
int progress_save(const progress_state_t *state) {
    if (!state) return -1;

    char filepath[512];
    progress_get_filepath(filepath, sizeof(filepath),
                          state->mode, state->target_file, state->bits);

    if (filepath[0] == '\0') return -1;

    int pfd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (pfd < 0) return -1;
    FILE *f = fdopen(pfd, "w");
    if (!f) { close(pfd); return -1; }

    // Write JSON with proper escaping for string values
    fprintf(f, "{\n");
    fputs("  \"mode\": \"", f);
    json_escape_string(f, state->mode);
    fputs("\",\n", f);
    fputs("  \"target_file\": \"", f);
    json_escape_string(f, state->target_file);
    fputs("\",\n", f);
    fprintf(f, "  \"bits\": %d,\n", state->bits);
    fputs("  \"range_start\": \"", f);
    json_escape_string(f, state->range_start);
    fputs("\",\n", f);
    fputs("  \"range_end\": \"", f);
    json_escape_string(f, state->range_end);
    fputs("\",\n", f);
    fputs("  \"current_position\": \"", f);
    json_escape_string(f, state->current_position);
    fputs("\",\n", f);
    fprintf(f, "  \"keys_checked\": %llu,\n", (unsigned long long)state->keys_checked);
    fprintf(f, "  \"keys_total\": %llu,\n", (unsigned long long)state->keys_total);
    fprintf(f, "  \"elapsed_seconds\": %.2f,\n", state->elapsed_seconds);
    fprintf(f, "  \"start_time\": %lld,\n", (long long)state->start_time);
    fprintf(f, "  \"last_save_time\": %lld,\n", (long long)state->last_save_time);
    fprintf(f, "  \"is_random_mode\": %s,\n", state->is_random_mode ? "true" : "false");
    fprintf(f, "  \"thread_count\": %d,\n", state->thread_count);
    fprintf(f, "  \"ranges_completed\": %d,\n", state->ranges_completed);
    fprintf(f, "  \"ranges_total\": %d\n", state->ranges_total);
    fprintf(f, "}\n");

    fclose(f);
    return 0;
}

// Update progress (call periodically, auto-saves every 60s)
int progress_update(progress_state_t *state, const char *current_pos,
                    uint64_t keys_checked) {
    if (!state) return -1;

    // Update position
    if (current_pos) {
        snprintf(state->current_position, sizeof(state->current_position), "%s", current_pos);
    }

    // Update keys checked
    state->keys_checked = keys_checked;

    // Update elapsed time (total time since start)
    time_t now = time(NULL);
    state->elapsed_seconds = difftime(now, state->start_time);

    // Check if auto-save is needed
    if (difftime(now, state->last_save_time) >= PROGRESS_AUTOSAVE_INTERVAL) {
        state->last_save_time = now;
        return progress_save(state);
    }

    return 0;
}

// Mark progress as complete (deletes file)
int progress_complete(progress_state_t *state) {
    if (!state) return -1;

    char filepath[512];
    progress_get_filepath(filepath, sizeof(filepath),
                          state->mode, state->target_file, state->bits);

    if (filepath[0] == '\0') return -1;

    return unlink(filepath);
}

// List all saved progress files
int progress_list(void) {
    char dir[512];
    if (get_progress_dir(dir, sizeof(dir)) != 0) {
        return -1;
    }

    platform_dir_handle_t d = platform_dir_open(dir);
    if (d == NULL) {
        printf("No saved progress found.\n");
        return 0;
    }

    printf("\nSaved Progress Files:\n");
    printf("%-12s %-6s %-20s %-15s %s\n",
           "Mode", "Bits", "Keys Checked", "Elapsed", "File");
    printf("%-12s %-6s %-20s %-15s %s\n",
           "----", "----", "------------", "-------", "----");

    platform_dir_entry_t entry;
    int count = 0;

    while (platform_dir_read(d, &entry) == 1) {
        if (entry.is_directory) continue;

        const char *name = entry.name;
        size_t len = strlen(name);
        if (len < 5 || strcmp(name + len - 5, ".json") != 0) continue;

        // Load progress to get details
        char filepath[512];
        snprintf(filepath, sizeof(filepath), "%s/%s", dir, name);

        FILE *f = fopen(filepath, "r");
        if (!f) continue;

        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);

        if (size <= 0 || size > 16384) {
            fclose(f);
            continue;
        }

        char *json = (char *)malloc(size + 1);
        if (!json) {
            fclose(f);
            continue;
        }

        size_t read_size = fread(json, 1, size, f);
        fclose(f);

        if ((long)read_size != size) {
            free(json);
            continue;
        }
        json[size] = '\0';

        char mode[32] = {0};
        int bits = 0;
        uint64_t keys_checked = 0;
        double elapsed = 0;

        parse_json_string(json, "mode", mode, sizeof(mode));
        parse_json_int(json, "bits", &bits);
        parse_json_uint64(json, "keys_checked", &keys_checked);
        parse_json_double(json, "elapsed_seconds", &elapsed);

        free(json);

        // Format elapsed time
        char elapsed_str[32];
        int elapsed_int = (int)elapsed;
        if (elapsed_int < 3600) {
            snprintf(elapsed_str, sizeof(elapsed_str), "%dm %ds",
                     elapsed_int / 60, elapsed_int % 60);
        } else if (elapsed_int < 86400) {
            snprintf(elapsed_str, sizeof(elapsed_str), "%dh %dm",
                     elapsed_int / 3600, (elapsed_int % 3600) / 60);
        } else {
            snprintf(elapsed_str, sizeof(elapsed_str), "%dd %dh",
                     elapsed_int / 86400, (elapsed_int % 86400) / 3600);
        }

        printf("%-12s %-6d %-20llu %-15s %s\n",
               mode, bits, (unsigned long long)keys_checked, elapsed_str, name);
        count++;
    }

    platform_dir_close(d);

    if (count == 0) {
        printf("No saved progress found.\n");
    } else {
        printf("\nTotal: %d saved progress file(s)\n", count);
    }

    return count;
}

// Delete specific progress
int progress_delete(const char *mode, const char *target_file, int bits) {
    char filepath[512];
    progress_get_filepath(filepath, sizeof(filepath), mode, target_file, bits);

    if (filepath[0] == '\0') return -1;

    return unlink(filepath);
}

// Delete all progress files
int progress_clear_all(void) {
    char dir[512];
    if (get_progress_dir(dir, sizeof(dir)) != 0) {
        return -1;
    }

    platform_dir_handle_t d = platform_dir_open(dir);
    if (d == NULL) return 0;  // No directory means nothing to clear

    platform_dir_entry_t entry;
    int count = 0;

    while (platform_dir_read(d, &entry) == 1) {
        if (entry.is_directory) continue;

        const char *name = entry.name;
        size_t len = strlen(name);
        if (len < 5 || strcmp(name + len - 5, ".json") != 0) continue;

        char filepath[512];
        snprintf(filepath, sizeof(filepath), "%s/%s", dir, name);

        if (unlink(filepath) == 0) {
            count++;
        }
    }

    platform_dir_close(d);
    return count;
}

// Speed history functions

// Initialize speed history
void speed_history_init(speed_history_t *history) {
    if (!history) return;

    memset(history, 0, sizeof(speed_history_t));
    history->count = 0;
    history->write_index = 0;
}

// Add a speed sample to history
void speed_history_add_sample(speed_history_t *history, double keys_per_second) {
    if (!history) return;

    // Get current timestamp
    time_t now = time(NULL);

    // Store sample at write_index
    history->samples[history->write_index].timestamp = now;
    history->samples[history->write_index].keys_per_second = keys_per_second;

    // Advance write index (circular buffer)
    history->write_index = (history->write_index + 1) % SPEED_HISTORY_MAX_SAMPLES;

    // Increment count up to max
    if (history->count < SPEED_HISTORY_MAX_SAMPLES) {
        history->count++;
    }
}

// Get average speed over all samples
double speed_history_get_average(const speed_history_t *history) {
    if (!history || history->count == 0) return 0.0;

    double sum = 0.0;
    for (int i = 0; i < history->count; i++) {
        sum += history->samples[i].keys_per_second;
    }

    return sum / history->count;
}

// Get average speed over recent N samples (or all if fewer)
double speed_history_get_recent_average(const speed_history_t *history, int sample_count) {
    if (!history || history->count == 0 || sample_count <= 0) return 0.0;

    // Limit to available samples
    int samples_to_use = sample_count;
    if (samples_to_use > history->count) {
        samples_to_use = history->count;
    }

    // Calculate starting position (work backwards from most recent)
    // Most recent sample is at (write_index - 1 + MAX) % MAX
    int start_offset = history->write_index - samples_to_use;
    if (start_offset < 0) {
        start_offset += SPEED_HISTORY_MAX_SAMPLES;
    }

    double sum = 0.0;
    for (int i = 0; i < samples_to_use; i++) {
        int idx = (start_offset + i) % SPEED_HISTORY_MAX_SAMPLES;
        sum += history->samples[idx].keys_per_second;
    }

    return sum / samples_to_use;
}

// Get speed trend (-1 = decreasing, 0 = stable, 1 = increasing)
int speed_history_get_trend(const speed_history_t *history) {
    if (!history || history->count < 4) return 0;  // Need at least 4 samples for trend

    // Compare first half average vs second half average
    int half = history->count / 2;

    // Calculate first half average
    double first_half_sum = 0.0;
    for (int i = 0; i < half; i++) {
        first_half_sum += history->samples[i].keys_per_second;
    }
    double first_half_avg = first_half_sum / half;

    // Calculate second half average (most recent samples)
    double second_half_sum = 0.0;
    int second_half_start = history->count - half;
    for (int i = second_half_start; i < history->count; i++) {
        second_half_sum += history->samples[i].keys_per_second;
    }
    double second_half_avg = second_half_sum / half;

    // Determine trend (with 5% threshold to avoid noise)
    double threshold = first_half_avg * 0.05;
    if (second_half_avg > first_half_avg + threshold) {
        return 1;  // Increasing
    } else if (second_half_avg < first_half_avg - threshold) {
        return -1;  // Decreasing
    } else {
        return 0;  // Stable
    }
}
