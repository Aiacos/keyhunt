/*
 * wizard.h - Interactive Wizard for Keyhunt Distributed Mode
 *
 * Usage: ./keyhunt --wizard or ./keyhunt -W
 *        ./keyhunt --wizard-client host:port  (non-interactive client mode)
 *
 * Features:
 * - Interactive setup for server/client mode
 * - Auto-configuration with JSON persistence
 * - Community progress integration (BTCPuzzle.info)
 * - Server handles ONLY orchestration (no computation)
 * - Server spawns local client process for computation
 * - Clean separation of concerns for better performance
 * - Automatic puzzle database updates
 */

#ifndef WIZARD_H
#define WIZARD_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Limits */
#define WIZARD_MAX_PUZZLES 256
#define WIZARD_MAX_EXCLUSIONS 1000000

/* Privatekeys.pw cloud search integration */
#define PRIVATEKEYS_CLOUD_URL "https://privatekeys.pw/cloud-search"
#define PRIVATEKEYS_CACHE_DIR ".keyhunt"
#define PRIVATEKEYS_CACHE_FILE "privatekeys_progress.json"
#define PRIVATEKEYS_REFRESH_INTERVAL (24 * 60 * 60)  /* 24 hours */

/* Keys.lol puzzle progress integration */
#define KEYSLOL_URL "https://keys.lol/api/puzzle"
#define KEYSLOL_CACHE_FILE "keyslol_progress.json"
#define KEYSLOL_REFRESH_INTERVAL (24 * 60 * 60)  /* 24 hours */

/* Puzzle definition (can be loaded from file or web) */
typedef struct {
    int number;
    char target_address[64];
    char range_start[68];
    char range_end[68];
    int bits;
    double reward_btc;
    bool has_public_key;
    char public_key[132];   /* Compressed or uncompressed */
    bool solved;
    char solved_date[24];
    char solver[64];
} puzzle_def_t;

/* Wizard configuration (JSON-serializable) */
typedef struct {
    int version;

    /* Puzzle settings */
    int puzzle_number;
    char target_address[64];
    char range_start[68];
    char range_end[68];
    int bits;

    /* Server settings */
    char server_host[256];
    int server_port;
    uint64_t work_unit_size;
    int checkpoint_interval_sec;
    char auth_token[64];            /* Authentication token for workers */

    /* Search settings */
    char mode[32];           /* "address", "bsgs", "xpoint" */
    char key_type[16];       /* "compress", "uncompress", "both" */
    bool random_mode;
    int threads;
    int gpu_percent;

    /* BSGS-specific parameters (auto-calculated based on RAM) */
    uint64_t bsgs_n;         /* N parameter for BSGS */
    int bsgs_k;              /* K factor for BSGS */

    /* Community sync */
    bool community_enabled;
    char community_source[64];
    int community_sync_interval_sec;
    time_t community_last_sync;

    /* Progress tracking */
    uint64_t total_ranges;
    uint64_t local_completed;
    uint64_t community_excluded;
    double privatekeys_percent;      /* Progress from privatekeys.pw */
    char progress_file[256];
    char exclusion_file[256];

    /* Webhook notifications */
    char webhook_discord_url[512];   /* Discord webhook URL for notifications */
    char webhook_telegram_url[512];  /* Telegram bot token or webhook URL */

    /* Progress reporting (opt-in) */
    bool report_progress_enabled;    /* Enable progress reporting to community endpoint */
    char report_progress_url[512];   /* URL endpoint for progress reporting */

    /* Runtime state (not saved to JSON) */
    bool is_server;
    bool server_also_worker;
} wizard_config_t;

/* Community range data (from BTCPuzzle.info) */
typedef struct {
    char range_id[32];
    char hex_start[68];
    char hex_end[68];
    time_t scanned_time;
    char worker[64];
} community_range_t;

/* Privatekeys.pw progress data */
typedef struct {
    int puzzle_number;
    double percent_scanned;      /* e.g., 0.022477 */
    uint64_t keys_scanned;       /* Absolute count if available */
    time_t fetch_time;           /* When data was fetched */
} privatekeys_progress_t;

/* Keys.lol progress data */
typedef struct {
    int puzzle_number;
    double percent_scanned;      /* e.g., 0.022477 */
    uint64_t keys_scanned;       /* Absolute count if available */
    time_t fetch_time;           /* When data was fetched */
} keyslol_progress_t;

/* ============================================================================
 * Wizard Entry Point
 * ============================================================================ */

/**
 * Run the interactive wizard
 * @return 0 on success, -1 on error, 1 on user cancel
 */
int wizard_run(void);

/* ============================================================================
 * Configuration Functions
 * ============================================================================ */

void wizard_config_init(wizard_config_t *cfg);
int wizard_config_load(wizard_config_t *cfg, const char *filepath);
int wizard_config_save(const wizard_config_t *cfg, const char *filepath);
void wizard_config_print(const wizard_config_t *cfg);

/* ============================================================================
 * Puzzle Database Functions
 * ============================================================================ */

/**
 * Get built-in puzzles (hardcoded fallback)
 */
const puzzle_def_t* wizard_get_builtin_puzzles(int *count);

/**
 * Get puzzle by number
 */
const puzzle_def_t* wizard_get_puzzle(int number);

/**
 * Download puzzle list from BTCPuzzle.info
 * @param puzzles Output array (caller must free)
 * @param count Output count
 * @return 0 on success, -1 on error
 */
int wizard_download_puzzles(puzzle_def_t **puzzles, int *count);

/**
 * Save puzzles to local cache file
 */
int wizard_save_puzzles_cache(const puzzle_def_t *puzzles, int count, const char *filepath);

/**
 * Load puzzles from local cache file
 */
int wizard_load_puzzles_cache(puzzle_def_t **puzzles, int *count, const char *filepath);

/**
 * Save puzzles to text file
 */
int wizard_save_puzzles_to_txt(const puzzle_def_t *puzzles, int count, const char *filepath);

/**
 * Load puzzles from text file
 */
int wizard_load_puzzles_from_txt(const char *filepath, puzzle_def_t **puzzles, int *count);

/* ============================================================================
 * UI Functions
 * ============================================================================ */

void wizard_ui_clear(void);
void wizard_print_header(const char *title);
void wizard_print_separator(void);
void wizard_print_step(int current, int total, const char *title);
int wizard_ask_choice(const char *prompt, const char **options, int count, int default_choice);
int wizard_ask_string(const char *prompt, char *buffer, size_t bufsize, const char *default_val);
int wizard_ask_int(const char *prompt, int min_val, int max_val, int default_val);
bool wizard_ask_yesno(const char *prompt, bool default_val);
void wizard_print_config_summary(const wizard_config_t *cfg);
void wizard_print_progress(int current, int total, double speed, const char *status);
void wizard_print_privacy_warning(void);

/* ============================================================================
 * Community Sync Functions
 * ============================================================================ */

/**
 * Fetch scanned ranges from BTCPuzzle.info
 */
int wizard_community_fetch(int puzzle_number, community_range_t **ranges, int *count);

/**
 * Free community ranges
 */
void wizard_community_free(community_range_t *ranges, int count);

/**
 * Merge community exclusions into local exclusion file
 */
int wizard_community_merge_exclusions(const char *exclusion_file,
                                       const community_range_t *ranges, int count);

/**
 * Check if range is excluded
 */
bool wizard_is_range_excluded(const char *exclusion_file, const char *range_start);

/**
 * Fetch and aggregate progress from all community sources
 * @param puzzle_number Puzzle to check
 * @param btc_ranges Output: BTCPuzzle.info ranges (caller must free)
 * @param btc_count Output: number of BTCPuzzle.info ranges
 * @param privatekeys_progress Output: privatekeys.pw progress data
 * @param keyslol_progress Output: keys.lol progress data
 * @return 0 on success (at least one source), -1 on complete failure
 */
int wizard_community_fetch_all_sources(int puzzle_number,
                                        community_range_t **btc_ranges,
                                        int *btc_count,
                                        privatekeys_progress_t *privatekeys_progress,
                                        keyslol_progress_t *keyslol_progress);

/**
 * Report progress to community endpoint (opt-in)
 *
 * Sends current search progress to a community API endpoint if enabled.
 * Requires report_progress_enabled = true in configuration.
 *
 * @param cfg Wizard configuration with progress data
 * @return 0 on success, -1 on error, 1 if reporting disabled
 */
int wizard_community_report_progress(const wizard_config_t *cfg);

/* ============================================================================
 * Privatekeys.pw Cloud Search Integration
 * ============================================================================ */

/**
 * Fetch progress from privatekeys.pw (scrapes HTML)
 * @param puzzle_number Puzzle to check (currently only 71 supported)
 * @param progress Output progress data
 * @return 0 on success, -1 on error
 */
int wizard_privatekeys_fetch_progress(int puzzle_number, privatekeys_progress_t *progress);

/**
 * Get progress with 24h caching
 * @param puzzle_number Puzzle to check
 * @param progress Output progress data
 * @return 0 on success (fresh or cached), -1 on error (no data available)
 */
int wizard_privatekeys_get_progress(int puzzle_number, privatekeys_progress_t *progress);

/**
 * Calculate search offset based on community progress (sequential mode)
 * @param puzzle Puzzle definition with range bounds
 * @param percent_scanned Community progress percentage
 * @param adjusted_start Output: hex string of adjusted start position
 */
void wizard_calculate_search_offset(const puzzle_def_t *puzzle,
                                    double percent_scanned,
                                    char *adjusted_start);

/**
 * Check if a range falls within community-scanned region (random mode)
 * @param puzzle Puzzle definition
 * @param range_start Hex string of range start
 * @param percent_scanned Community progress percentage
 * @return true if range is in scanned region, false otherwise
 */
bool wizard_is_in_scanned_region(const puzzle_def_t *puzzle,
                                 const char *range_start,
                                 double percent_scanned);

/* ============================================================================
 * Keys.lol Puzzle Progress Integration
 * ============================================================================ */

/**
 * Fetch progress from Keys.lol (scrapes HTML/API)
 * @param puzzle_number Puzzle to check
 * @param progress Output progress data
 * @return 0 on success, -1 on error
 */
int wizard_keyslol_fetch_progress(int puzzle_number, keyslol_progress_t *progress);

/**
 * Get progress with 24h caching
 * @param puzzle_number Puzzle to check
 * @param progress Output progress data
 * @return 0 on success (fresh or cached), -1 on error (no data available)
 */
int wizard_keyslol_get_progress(int puzzle_number, keyslol_progress_t *progress);

/**
 * Save locally completed range to progress file
 * @param puzzle_number Puzzle number (used for filename)
 * @param range_start Hex string start
 * @param range_end Hex string end
 * @return 0 on success, -1 on error
 */
int wizard_save_local_progress(int puzzle_number,
                               const char *range_start,
                               const char *range_end);

/**
 * Load local progress ranges count
 * @param puzzle_number Puzzle number
 * @return Number of locally completed ranges, 0 if none
 */
int wizard_load_local_progress_count(int puzzle_number);

/* ============================================================================
 * Server Mode (Coordinator + Local Worker)
 * ============================================================================ */

/**
 * Run in server mode
 * - Starts coordinator to distribute work
 * - Also runs local worker if server_also_worker is true
 * - Handles community exclusions
 */
int wizard_server_run(wizard_config_t *cfg);

/* ============================================================================
 * Client Mode (Worker)
 * ============================================================================ */

/**
 * Run in client mode
 * - Connects to server and fetches config
 * - Detects local hardware
 * - Runs as worker
 */
int wizard_client_run(wizard_config_t *cfg);

/**
 * Run in client mode (non-interactive, auto-configured)
 * - Used when server spawns local client process
 * - Parses host:port from string
 * - Auto-detects hardware and connects
 * @param host_port String in format "host:port" (e.g., "localhost:7777")
 * @return 0 on clean exit, -1 on error
 */
int wizard_client_run_auto(const char *host_port);

/* ============================================================================
 * Search Function (implemented in keyhunt.cpp)
 * ============================================================================ */

/**
 * Search a specific range for target address
 * @param start Hex string start of range
 * @param end Hex string end of range
 * @param target Target address to find
 * @param mode Search mode ("address", "rmd160", etc)
 * @param key_type Key type ("compress", "uncompress", "both")
 * @param threads Number of CPU threads
 * @param gpu_percent GPU usage percentage (0 = disabled)
 * @param keys_checked Output: number of keys checked
 * @param stop_flag Pointer to stop flag (set to 0 to stop)
 * @param found_key Output: found private key (hex) if any
 * @param found_addr Output: found address if any
 * @return 0 = not found, 1 = found, -1 = error
 */
int keyhunt_search_range(const char *start, const char *end,
                         const char *target, const char *mode,
                         const char *key_type, int threads, int gpu_percent,
                         uint64_t *keys_checked, volatile int *stop_flag,
                         char *found_key, char *found_addr);

#ifdef __cplusplus
}
#endif

#endif /* WIZARD_H */
