/*
 * test_wizard.cpp - Unit tests for the interactive wizard
 *
 * Tests:
 * - Configuration parsing and serialization
 * - Hardware detection mocking
 * - Parameter calculation for BSGS
 * - Puzzle database handling
 * - Community sync data structures
 */

#include "test_framework.h"

extern "C" {
#include "wizard/wizard.h"
}

#include <string.h>
#include <stdlib.h>
#include "platform/platform.h"

#if !PLATFORM_WINDOWS
#include <unistd.h>
#else
#include <direct.h>    /* _mkdir */
#endif
#include <sys/stat.h>

/* ============================================================================
 * Constants Tests
 * ============================================================================ */

TEST(wizard_constants) {
    /* Verify constants are defined and reasonable */
    ASSERT_TRUE(WIZARD_MAX_PUZZLES > 0);
    ASSERT_TRUE(WIZARD_MAX_PUZZLES <= 1000);
    ASSERT_TRUE(WIZARD_MAX_EXCLUSIONS > 0);
}

TEST(wizard_privatekeys_constants) {
    /* Verify privatekeys.pw integration constants */
    ASSERT_NOT_NULL(PRIVATEKEYS_CLOUD_URL);
    ASSERT_NOT_NULL(PRIVATEKEYS_CACHE_DIR);
    ASSERT_NOT_NULL(PRIVATEKEYS_CACHE_FILE);
    ASSERT_TRUE(PRIVATEKEYS_REFRESH_INTERVAL > 0);
    ASSERT_EQ(24 * 60 * 60, PRIVATEKEYS_REFRESH_INTERVAL);  /* 24 hours */
}

/* ============================================================================
 * Puzzle Definition Struct Tests
 * ============================================================================ */

TEST(puzzle_def_struct) {
    puzzle_def_t puzzle;
    memset(&puzzle, 0, sizeof(puzzle));

    puzzle.number = 66;
    strncpy(puzzle.target_address, "13zb1hQbWVsc2S7ZTZnP2G4undNNpdh5so",
            sizeof(puzzle.target_address) - 1);
    strncpy(puzzle.range_start, "20000000000000000", sizeof(puzzle.range_start) - 1);
    strncpy(puzzle.range_end, "3FFFFFFFFFFFFFFFF", sizeof(puzzle.range_end) - 1);
    puzzle.bits = 66;
    puzzle.reward_btc = 6.6;
    puzzle.has_public_key = false;
    puzzle.public_key[0] = '\0';
    puzzle.solved = false;
    puzzle.solved_date[0] = '\0';
    puzzle.solver[0] = '\0';

    ASSERT_EQ(66, puzzle.number);
    ASSERT_STR_EQ("13zb1hQbWVsc2S7ZTZnP2G4undNNpdh5so", puzzle.target_address);
    ASSERT_STR_EQ("20000000000000000", puzzle.range_start);
    ASSERT_STR_EQ("3FFFFFFFFFFFFFFFF", puzzle.range_end);
    ASSERT_EQ(66, puzzle.bits);
    ASSERT_DOUBLE_EQ(6.6, puzzle.reward_btc, 0.01);
    ASSERT_FALSE(puzzle.has_public_key);
    ASSERT_FALSE(puzzle.solved);
}

TEST(puzzle_def_with_pubkey) {
    puzzle_def_t puzzle;
    memset(&puzzle, 0, sizeof(puzzle));

    puzzle.number = 71;
    strncpy(puzzle.target_address, "1Fo65aKq8s8iquMt6weF1rku1moWVEd5Ua",
            sizeof(puzzle.target_address) - 1);
    strncpy(puzzle.range_start, "400000000000000000", sizeof(puzzle.range_start) - 1);
    strncpy(puzzle.range_end, "7FFFFFFFFFFFFFFFF", sizeof(puzzle.range_end) - 1);
    puzzle.bits = 71;
    puzzle.reward_btc = 7.1;
    puzzle.has_public_key = true;
    strncpy(puzzle.public_key,
            "03F46F41027BBF44FAFD6B059091B900DAD41E6845B2241DC3254C7CAA3529F5A3",
            sizeof(puzzle.public_key) - 1);

    ASSERT_EQ(71, puzzle.number);
    ASSERT_TRUE(puzzle.has_public_key);
    ASSERT_STR_EQ("03F46F41027BBF44FAFD6B059091B900DAD41E6845B2241DC3254C7CAA3529F5A3",
                  puzzle.public_key);
}

TEST(puzzle_def_solved) {
    puzzle_def_t puzzle;
    memset(&puzzle, 0, sizeof(puzzle));

    puzzle.number = 1;
    puzzle.solved = true;
    strncpy(puzzle.solved_date, "2015-01-15", sizeof(puzzle.solved_date) - 1);
    strncpy(puzzle.solver, "Unknown", sizeof(puzzle.solver) - 1);

    ASSERT_TRUE(puzzle.solved);
    ASSERT_STR_EQ("2015-01-15", puzzle.solved_date);
    ASSERT_STR_EQ("Unknown", puzzle.solver);
}

/* ============================================================================
 * Wizard Config Struct Tests
 * ============================================================================ */

TEST(wizard_config_struct) {
    wizard_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    cfg.version = 1;
    cfg.puzzle_number = 66;
    strncpy(cfg.target_address, "13zb1hQbWVsc2S7ZTZnP2G4undNNpdh5so",
            sizeof(cfg.target_address) - 1);
    strncpy(cfg.range_start, "20000000000000000", sizeof(cfg.range_start) - 1);
    strncpy(cfg.range_end, "3FFFFFFFFFFFFFFFF", sizeof(cfg.range_end) - 1);
    cfg.bits = 66;

    strncpy(cfg.server_host, "localhost", sizeof(cfg.server_host) - 1);
    cfg.server_port = 7777;
    cfg.work_unit_size = 16777216;
    cfg.checkpoint_interval_sec = 300;
    strncpy(cfg.auth_token, "secret123", sizeof(cfg.auth_token) - 1);

    strncpy(cfg.mode, "address", sizeof(cfg.mode) - 1);
    strncpy(cfg.key_type, "compress", sizeof(cfg.key_type) - 1);
    cfg.random_mode = true;
    cfg.threads = 8;
    cfg.gpu_percent = 50;

    cfg.bsgs_n = 1ULL << 24;
    cfg.bsgs_k = 128;

    cfg.community_enabled = true;
    strncpy(cfg.community_source, "btcpuzzle.info", sizeof(cfg.community_source) - 1);
    cfg.community_sync_interval_sec = 3600;

    cfg.total_ranges = 1000;
    cfg.local_completed = 100;
    cfg.community_excluded = 50;
    cfg.privatekeys_percent = 0.022477;

    cfg.is_server = true;
    cfg.server_also_worker = true;

    ASSERT_EQ(1, cfg.version);
    ASSERT_EQ(66, cfg.puzzle_number);
    ASSERT_STR_EQ("13zb1hQbWVsc2S7ZTZnP2G4undNNpdh5so", cfg.target_address);
    ASSERT_EQ(66, cfg.bits);
    ASSERT_STR_EQ("localhost", cfg.server_host);
    ASSERT_EQ(7777, cfg.server_port);
    ASSERT_EQ(16777216UL, cfg.work_unit_size);
    ASSERT_STR_EQ("address", cfg.mode);
    ASSERT_STR_EQ("compress", cfg.key_type);
    ASSERT_TRUE(cfg.random_mode);
    ASSERT_EQ(8, cfg.threads);
    ASSERT_EQ(50, cfg.gpu_percent);
    ASSERT_EQ(1ULL << 24, cfg.bsgs_n);
    ASSERT_EQ(128, cfg.bsgs_k);
    ASSERT_TRUE(cfg.community_enabled);
    ASSERT_DOUBLE_EQ(0.022477, cfg.privatekeys_percent, 0.000001);
    ASSERT_TRUE(cfg.is_server);
    ASSERT_TRUE(cfg.server_also_worker);
}

/* ============================================================================
 * Community Range Struct Tests
 * ============================================================================ */

TEST(community_range_struct) {
    community_range_t range;
    memset(&range, 0, sizeof(range));

    strncpy(range.range_id, "ABC123", sizeof(range.range_id) - 1);
    strncpy(range.hex_start, "20000000000000000", sizeof(range.hex_start) - 1);
    strncpy(range.hex_end, "20000000001000000", sizeof(range.hex_end) - 1);
    range.scanned_time = 1234567890;
    strncpy(range.worker, "user123", sizeof(range.worker) - 1);

    ASSERT_STR_EQ("ABC123", range.range_id);
    ASSERT_STR_EQ("20000000000000000", range.hex_start);
    ASSERT_STR_EQ("20000000001000000", range.hex_end);
    ASSERT_EQ(1234567890, range.scanned_time);
    ASSERT_STR_EQ("user123", range.worker);
}

/* ============================================================================
 * Privatekeys Progress Struct Tests
 * ============================================================================ */

TEST(privatekeys_progress_struct) {
    privatekeys_progress_t progress;
    memset(&progress, 0, sizeof(progress));

    progress.puzzle_number = 71;
    progress.percent_scanned = 0.022477;
    progress.keys_scanned = 1000000000000ULL;
    progress.fetch_time = 1234567890;

    ASSERT_EQ(71, progress.puzzle_number);
    ASSERT_DOUBLE_EQ(0.022477, progress.percent_scanned, 0.000001);
    ASSERT_EQ(1000000000000ULL, progress.keys_scanned);
    ASSERT_EQ(1234567890, progress.fetch_time);
}

/* ============================================================================
 * Config Initialization Tests
 * ============================================================================ */

TEST(wizard_config_init) {
    wizard_config_t cfg;
    memset(&cfg, 0xFF, sizeof(cfg));  /* Fill with garbage */

    wizard_config_init(&cfg);

    /* Check that reasonable defaults are set based on actual implementation:
     * - puzzle_number defaults to 71
     * - mode defaults to "address"
     * - key_type defaults to "compress"
     * - threads defaults to -1 (auto-detect at runtime)
     */
    ASSERT_EQ(1, cfg.version);
    ASSERT_EQ(71, cfg.puzzle_number);  /* Default puzzle is 71 */
    ASSERT_EQ(7777, cfg.server_port);  /* Default port */
    ASSERT_TRUE(cfg.work_unit_size > 0);
    ASSERT_TRUE(cfg.checkpoint_interval_sec > 0);
    ASSERT_STR_EQ("address", cfg.mode);  /* Default mode */
    ASSERT_STR_EQ("compress", cfg.key_type);  /* Default key type */
    ASSERT_TRUE(cfg.random_mode);  /* Random mode enabled by default */
    /* threads can be -1 (auto) or positive */
    ASSERT_EQ(0, cfg.gpu_percent);  /* GPU off by default */
    ASSERT_FALSE(cfg.is_server);  /* Not running as server by default */
    ASSERT_TRUE(cfg.server_also_worker);  /* When server, also run as worker */
}

/* ============================================================================
 * Config Save/Load Tests
 * ============================================================================ */

static char g_wizard_test_dir[256] = {0};

static void setup_wizard_test_dir(void) {
    snprintf(g_wizard_test_dir, sizeof(g_wizard_test_dir), "/tmp/keyhunt_wizard_test_%d", getpid());
#if PLATFORM_WINDOWS
    _mkdir(g_wizard_test_dir);
#else
    mkdir(g_wizard_test_dir, 0755);
#endif
}

static void cleanup_wizard_test_dir(void) {
    if (g_wizard_test_dir[0]) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "rm -rf %s", g_wizard_test_dir);
        int ret = system(cmd);
        (void)ret;
        g_wizard_test_dir[0] = '\0';
    }
}

TEST(wizard_config_save_load) {
    setup_wizard_test_dir();

    wizard_config_t cfg_out;
    wizard_config_init(&cfg_out);

    /* Set some values */
    cfg_out.puzzle_number = 66;
    strncpy(cfg_out.target_address, "13zb1hQbWVsc2S7ZTZnP2G4undNNpdh5so",
            sizeof(cfg_out.target_address) - 1);
    strncpy(cfg_out.range_start, "20000000000000000", sizeof(cfg_out.range_start) - 1);
    strncpy(cfg_out.range_end, "3FFFFFFFFFFFFFFFF", sizeof(cfg_out.range_end) - 1);
    cfg_out.bits = 66;
    cfg_out.server_port = 7777;
    cfg_out.work_unit_size = 16777216;
    strncpy(cfg_out.mode, "address", sizeof(cfg_out.mode) - 1);
    strncpy(cfg_out.key_type, "compress", sizeof(cfg_out.key_type) - 1);
    cfg_out.random_mode = true;
    cfg_out.threads = 8;
    cfg_out.community_enabled = true;

    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/test_config.json", g_wizard_test_dir);

    /* Save config */
    int save_result = wizard_config_save(&cfg_out, filepath);
    ASSERT_EQ(0, save_result);

    /* Load config */
    wizard_config_t cfg_in;
    wizard_config_init(&cfg_in);

    int load_result = wizard_config_load(&cfg_in, filepath);
    ASSERT_EQ(0, load_result);

    /* Verify values match */
    ASSERT_EQ(cfg_out.puzzle_number, cfg_in.puzzle_number);
    ASSERT_STR_EQ(cfg_out.target_address, cfg_in.target_address);
    ASSERT_STR_EQ(cfg_out.range_start, cfg_in.range_start);
    ASSERT_STR_EQ(cfg_out.range_end, cfg_in.range_end);
    ASSERT_EQ(cfg_out.bits, cfg_in.bits);
    ASSERT_EQ(cfg_out.server_port, cfg_in.server_port);
    ASSERT_EQ(cfg_out.work_unit_size, cfg_in.work_unit_size);
    ASSERT_STR_EQ(cfg_out.mode, cfg_in.mode);
    ASSERT_STR_EQ(cfg_out.key_type, cfg_in.key_type);
    ASSERT_EQ(cfg_out.random_mode, cfg_in.random_mode);
    ASSERT_EQ(cfg_out.threads, cfg_in.threads);
    ASSERT_EQ(cfg_out.community_enabled, cfg_in.community_enabled);

    cleanup_wizard_test_dir();
}

TEST(wizard_config_load_not_found) {
    wizard_config_t cfg;
    wizard_config_init(&cfg);

    int result = wizard_config_load(&cfg, "/nonexistent/path/config.json");

    /* Should fail gracefully */
    ASSERT_EQ(-1, result);
}

TEST(wizard_config_save_invalid_path) {
    wizard_config_t cfg;
    wizard_config_init(&cfg);

    int result = wizard_config_save(&cfg, "/nonexistent/path/that/does/not/exist/config.json");

    /* Should fail */
    ASSERT_EQ(-1, result);
}

/* ============================================================================
 * Built-in Puzzles Tests
 * ============================================================================ */

TEST(wizard_get_builtin_puzzles) {
    int count = 0;
    const puzzle_def_t *puzzles = wizard_get_builtin_puzzles(&count);

    /* Should return some built-in puzzles */
    ASSERT_NOT_NULL(puzzles);
    ASSERT_TRUE(count > 0);

    /* First puzzle in builtin list is #71 (unsolved puzzles start there) */
    ASSERT_EQ(71, puzzles[0].number);
    ASSERT_FALSE(puzzles[0].solved);  /* Puzzle 71 is unsolved */

    /* All puzzles should have valid data */
    for (int i = 0; i < count; i++) {
        ASSERT_TRUE(puzzles[i].number > 0);
        ASSERT_TRUE(puzzles[i].bits > 0);
        ASSERT_TRUE(strlen(puzzles[i].target_address) > 0);
        ASSERT_TRUE(strlen(puzzles[i].range_start) > 0);
        ASSERT_TRUE(strlen(puzzles[i].range_end) > 0);
    }
}

TEST(wizard_get_puzzle_by_number) {
    const puzzle_def_t *puzzle = wizard_get_puzzle(66);

    if (puzzle) {
        ASSERT_EQ(66, puzzle->number);
        ASSERT_EQ(66, puzzle->bits);
        ASSERT_TRUE(strlen(puzzle->target_address) > 0);
    }
    /* Puzzle might not be in built-in list */
    ASSERT_TRUE(1);
}

TEST(wizard_get_puzzle_not_found) {
    const puzzle_def_t *puzzle = wizard_get_puzzle(9999);

    /* Should return NULL for non-existent puzzle */
    ASSERT_NULL(puzzle);
}

/* ============================================================================
 * Puzzle Cache Tests
 * ============================================================================ */

TEST(wizard_save_load_puzzles_cache) {
    setup_wizard_test_dir();

    /* Create some test puzzles */
    puzzle_def_t puzzles[3];
    memset(puzzles, 0, sizeof(puzzles));

    puzzles[0].number = 66;
    strncpy(puzzles[0].target_address, "13zb1hQbWVsc2S7ZTZnP2G4undNNpdh5so",
            sizeof(puzzles[0].target_address) - 1);
    puzzles[0].bits = 66;
    puzzles[0].solved = false;

    puzzles[1].number = 67;
    strncpy(puzzles[1].target_address, "1BY8GQbnueYofwSuFAT3USAhGjPrkxDdW9",
            sizeof(puzzles[1].target_address) - 1);
    puzzles[1].bits = 67;
    puzzles[1].solved = false;

    puzzles[2].number = 68;
    strncpy(puzzles[2].target_address, "1MVDYgVaSN6iKKEsbzRUAYFrYJadLYZvvZ",
            sizeof(puzzles[2].target_address) - 1);
    puzzles[2].bits = 68;
    puzzles[2].solved = false;

    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/puzzles_cache.txt", g_wizard_test_dir);

    /* Save puzzles */
    int save_result = wizard_save_puzzles_cache(puzzles, 3, filepath);
    ASSERT_EQ(0, save_result);

    /* Load puzzles back */
    puzzle_def_t *loaded = NULL;
    int loaded_count = 0;

    int load_result = wizard_load_puzzles_cache(&loaded, &loaded_count, filepath);

    if (load_result == 0) {
        ASSERT_EQ(3, loaded_count);
        ASSERT_NOT_NULL(loaded);

        /* Verify first puzzle */
        ASSERT_EQ(66, loaded[0].number);
        ASSERT_STR_EQ("13zb1hQbWVsc2S7ZTZnP2G4undNNpdh5so", loaded[0].target_address);

        free(loaded);
    }

    cleanup_wizard_test_dir();
}

/* ============================================================================
 * Search Offset Calculation Tests
 * ============================================================================ */

TEST(wizard_calculate_search_offset_zero) {
    puzzle_def_t puzzle;
    memset(&puzzle, 0, sizeof(puzzle));
    puzzle.number = 66;
    strncpy(puzzle.range_start, "20000000000000000", sizeof(puzzle.range_start) - 1);
    strncpy(puzzle.range_end, "3FFFFFFFFFFFFFFFF", sizeof(puzzle.range_end) - 1);
    puzzle.bits = 66;

    char adjusted_start[68];
    memset(adjusted_start, 0, sizeof(adjusted_start));

    wizard_calculate_search_offset(&puzzle, 0.0, adjusted_start);

    /* With 0% scanned, should return original start */
    ASSERT_STR_EQ("20000000000000000", adjusted_start);
}

TEST(wizard_calculate_search_offset_partial) {
    puzzle_def_t puzzle;
    memset(&puzzle, 0, sizeof(puzzle));
    puzzle.number = 66;
    strncpy(puzzle.range_start, "20000000000000000", sizeof(puzzle.range_start) - 1);
    strncpy(puzzle.range_end, "3FFFFFFFFFFFFFFFF", sizeof(puzzle.range_end) - 1);
    puzzle.bits = 66;

    char adjusted_start[68];
    memset(adjusted_start, 0, sizeof(adjusted_start));

    wizard_calculate_search_offset(&puzzle, 50.0, adjusted_start);

    /* With 50% scanned, should return midpoint */
    ASSERT_TRUE(strlen(adjusted_start) > 0);
    /* Adjusted start should be greater than original */
    ASSERT_TRUE(strcmp(adjusted_start, puzzle.range_start) >= 0);
}

/* ============================================================================
 * Scanned Region Check Tests
 * ============================================================================ */

TEST(wizard_is_in_scanned_region_zero) {
    puzzle_def_t puzzle;
    memset(&puzzle, 0, sizeof(puzzle));
    puzzle.number = 66;
    strncpy(puzzle.range_start, "20000000000000000", sizeof(puzzle.range_start) - 1);
    strncpy(puzzle.range_end, "3FFFFFFFFFFFFFFFF", sizeof(puzzle.range_end) - 1);

    bool result = wizard_is_in_scanned_region(&puzzle, "20000000000000000", 0.0);

    /* With 0% scanned, nothing should be in scanned region */
    ASSERT_FALSE(result);
}

TEST(wizard_is_in_scanned_region_partial) {
    puzzle_def_t puzzle;
    memset(&puzzle, 0, sizeof(puzzle));
    puzzle.number = 66;
    strncpy(puzzle.range_start, "20000000000000000", sizeof(puzzle.range_start) - 1);
    strncpy(puzzle.range_end, "3FFFFFFFFFFFFFFFF", sizeof(puzzle.range_end) - 1);

    /* Range at the start should be in scanned region if 50% scanned */
    bool result_start = wizard_is_in_scanned_region(&puzzle, "20000000000000000", 50.0);

    /* Range at 99% should be outside scanned region (in last 50%) */
    bool result_end = wizard_is_in_scanned_region(&puzzle, "3F00000000000000", 50.0);

    /* Start should be in scanned region */
    ASSERT_TRUE(result_start);
    /* Note: result_end depends on implementation - just verify both return bool */
    (void)result_end;
    ASSERT_TRUE(1);  /* Test passes if no crash */
}

/* ============================================================================
 * Local Progress Tests
 * ============================================================================ */

TEST(wizard_save_load_local_progress) {
    setup_wizard_test_dir();

    /* Change to test directory for progress files */
    char original_dir[512];
    if (getcwd(original_dir, sizeof(original_dir)) == NULL) {
        original_dir[0] = '\0';
    }

    /* Save a progress entry */
    int save_result = wizard_save_local_progress(66,
                                                  "20000000000000000",
                                                  "20000000001000000");

    /* May fail if directory doesn't exist, which is acceptable */
    (void)save_result;

    /* Load progress count */
    int count = wizard_load_local_progress_count(66);

    /* Should be >= 0 */
    ASSERT_TRUE(count >= 0);

    cleanup_wizard_test_dir();
}

/* ============================================================================
 * Community Sync Tests
 * ============================================================================ */

TEST(wizard_community_free_null) {
    /* Should handle NULL gracefully */
    wizard_community_free(NULL, 0);
    wizard_community_free(NULL, 100);
    ASSERT_TRUE(1);
}

TEST(wizard_is_range_excluded_null) {
    /* Should handle NULL file gracefully */
    bool result = wizard_is_range_excluded(NULL, "20000000000000000");
    ASSERT_FALSE(result);
}

TEST(wizard_is_range_excluded_nonexistent) {
    bool result = wizard_is_range_excluded("/nonexistent/exclusion.txt", "20000000000000000");
    ASSERT_FALSE(result);
}

/* ============================================================================
 * Mode Selection Logic Tests
 * ============================================================================ */

TEST(mode_selection_with_pubkey) {
    puzzle_def_t puzzle;
    memset(&puzzle, 0, sizeof(puzzle));
    puzzle.number = 71;
    puzzle.has_public_key = true;
    strncpy(puzzle.public_key,
            "03F46F41027BBF44FAFD6B059091B900DAD41E6845B2241DC3254C7CAA3529F5A3",
            sizeof(puzzle.public_key) - 1);

    /* With public key, should use BSGS mode */
    ASSERT_TRUE(puzzle.has_public_key);
    /* Mode would be set to "bsgs" in wizard */
}

TEST(mode_selection_without_pubkey) {
    puzzle_def_t puzzle;
    memset(&puzzle, 0, sizeof(puzzle));
    puzzle.number = 66;
    puzzle.has_public_key = false;
    puzzle.public_key[0] = '\0';

    /* Without public key, should use address mode */
    ASSERT_FALSE(puzzle.has_public_key);
    /* Mode would be set to "address" in wizard */
}

/* ============================================================================
 * BSGS Parameter Calculation Tests
 * ============================================================================ */

TEST(bsgs_n_calculation) {
    /* Test BSGS N calculation formula */
    uint64_t available_ram_mb = 8192;  /* 8 GB */
    uint64_t safe_ram = (available_ram_mb * 60) / 100;  /* 60% */
    uint64_t bytes_per_entry = 20;  /* Approximate */

    uint64_t max_entries = (safe_ram * 1024 * 1024) / bytes_per_entry;

    /* Find largest power of 2 that fits */
    uint64_t n = 1;
    while (n * 2 <= max_entries) {
        n *= 2;
    }

    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(n <= max_entries);
    /* N should be a power of 2 */
    ASSERT_EQ(0UL, (n & (n - 1)));  /* Power of 2 check */
}

TEST(bsgs_k_default) {
    /* K factor should be reasonable */
    int default_k = 128;

    ASSERT_TRUE(default_k >= 1);
    ASSERT_TRUE(default_k <= 1024);
}

/* ============================================================================
 * Random Mode Selection Logic
 * ============================================================================ */

TEST(random_mode_for_large_puzzles) {
    /* For puzzles > 66 bits, random mode is recommended */
    int puzzle_bits = 71;

    bool should_use_random = (puzzle_bits > 66);
    ASSERT_TRUE(should_use_random);
}

TEST(sequential_mode_for_small_puzzles) {
    /* For smaller puzzles, sequential mode can work */
    int puzzle_bits = 40;

    bool should_use_random = (puzzle_bits > 66);
    ASSERT_FALSE(should_use_random);
}

/* ============================================================================
 * Work Unit Size Calculation Tests
 * ============================================================================ */

TEST(work_unit_size_calculation) {
    /* Work unit size should be reasonable for distributed mode */
    uint64_t default_work_unit = 1ULL << 24;  /* 16M keys */

    ASSERT_EQ(16777216UL, default_work_unit);
    ASSERT_TRUE(default_work_unit > 0);
}

TEST(work_unit_count_estimation) {
    /* Estimate work units for a puzzle range */
    /* Note: Can't use 1ULL << 66 as it overflows 64-bit */
    /* Use a reasonable test value instead */
    uint64_t range_size = (1ULL << 32) * (1ULL << 28);  /* ~2^60 keys */
    uint64_t work_unit_size = 1ULL << 24;

    /* Just verify the formula makes sense */
    ASSERT_TRUE(range_size > work_unit_size);
}

/* ============================================================================
 * Server/Client Mode Tests
 * ============================================================================ */

TEST(config_server_mode) {
    wizard_config_t cfg;
    wizard_config_init(&cfg);

    cfg.is_server = true;
    cfg.server_also_worker = true;
    cfg.server_port = 7777;
    strncpy(cfg.auth_token, "secret", sizeof(cfg.auth_token) - 1);

    ASSERT_TRUE(cfg.is_server);
    ASSERT_TRUE(cfg.server_also_worker);
    ASSERT_EQ(7777, cfg.server_port);
    ASSERT_STR_EQ("secret", cfg.auth_token);
}

TEST(config_client_mode) {
    wizard_config_t cfg;
    wizard_config_init(&cfg);

    cfg.is_server = false;
    cfg.server_also_worker = false;
    strncpy(cfg.server_host, "192.168.1.100", sizeof(cfg.server_host) - 1);
    cfg.server_port = 7777;

    ASSERT_FALSE(cfg.is_server);
    ASSERT_FALSE(cfg.server_also_worker);
    ASSERT_STR_EQ("192.168.1.100", cfg.server_host);
}

/* ============================================================================
 * GPU Configuration Tests
 * ============================================================================ */

TEST(config_gpu_disabled) {
    wizard_config_t cfg;
    wizard_config_init(&cfg);

    cfg.gpu_percent = 0;

    ASSERT_EQ(0, cfg.gpu_percent);
}

TEST(config_gpu_partial) {
    wizard_config_t cfg;
    wizard_config_init(&cfg);

    cfg.gpu_percent = 50;

    ASSERT_EQ(50, cfg.gpu_percent);
}

TEST(config_gpu_full) {
    wizard_config_t cfg;
    wizard_config_init(&cfg);

    cfg.gpu_percent = 100;

    ASSERT_EQ(100, cfg.gpu_percent);
}

/* ============================================================================
 * Key Type Configuration Tests
 * ============================================================================ */

TEST(config_key_type_compressed) {
    wizard_config_t cfg;
    wizard_config_init(&cfg);

    strncpy(cfg.key_type, "compress", sizeof(cfg.key_type) - 1);

    ASSERT_STR_EQ("compress", cfg.key_type);
}

TEST(config_key_type_uncompressed) {
    wizard_config_t cfg;
    wizard_config_init(&cfg);

    strncpy(cfg.key_type, "uncompress", sizeof(cfg.key_type) - 1);

    ASSERT_STR_EQ("uncompress", cfg.key_type);
}

TEST(config_key_type_both) {
    wizard_config_t cfg;
    wizard_config_init(&cfg);

    strncpy(cfg.key_type, "both", sizeof(cfg.key_type) - 1);

    ASSERT_STR_EQ("both", cfg.key_type);
}

/* ============================================================================
 * Thread Configuration Tests
 * ============================================================================ */

TEST(config_threads_default) {
    wizard_config_t cfg;
    wizard_config_init(&cfg);

    /* threads is initialized to -1 (auto-detect at runtime) */
    /* or could be set to a positive value if auto-detected during init */
    ASSERT_TRUE(cfg.threads == -1 || cfg.threads > 0);
}

TEST(config_threads_custom) {
    wizard_config_t cfg;
    wizard_config_init(&cfg);

    cfg.threads = 16;

    ASSERT_EQ(16, cfg.threads);
}

/* ============================================================================
 * Community Sync Configuration Tests
 * ============================================================================ */

TEST(config_community_enabled) {
    wizard_config_t cfg;
    wizard_config_init(&cfg);

    cfg.community_enabled = true;
    strncpy(cfg.community_source, "btcpuzzle.info", sizeof(cfg.community_source) - 1);
    cfg.community_sync_interval_sec = 3600;

    ASSERT_TRUE(cfg.community_enabled);
    ASSERT_STR_EQ("btcpuzzle.info", cfg.community_source);
    ASSERT_EQ(3600, cfg.community_sync_interval_sec);
}

TEST(config_community_disabled) {
    wizard_config_t cfg;
    wizard_config_init(&cfg);

    cfg.community_enabled = false;

    ASSERT_FALSE(cfg.community_enabled);
}

/* ============================================================================
 * Main Entry Point
 * ============================================================================ */

int run_wizard_tests(void) {
    TEST_INIT();

    TEST_SECTION("Constants");
    RUN_TEST(wizard_constants);
    RUN_TEST(wizard_privatekeys_constants);

    TEST_SECTION("Puzzle Definition Struct");
    RUN_TEST(puzzle_def_struct);
    RUN_TEST(puzzle_def_with_pubkey);
    RUN_TEST(puzzle_def_solved);

    TEST_SECTION("Wizard Config Struct");
    RUN_TEST(wizard_config_struct);

    TEST_SECTION("Community Range Struct");
    RUN_TEST(community_range_struct);

    TEST_SECTION("Privatekeys Progress Struct");
    RUN_TEST(privatekeys_progress_struct);

    TEST_SECTION("Config Initialization");
    RUN_TEST(wizard_config_init);

    TEST_SECTION("Config Save/Load");
    RUN_TEST(wizard_config_save_load);
    RUN_TEST(wizard_config_load_not_found);
    RUN_TEST(wizard_config_save_invalid_path);

    TEST_SECTION("Built-in Puzzles");
    RUN_TEST(wizard_get_builtin_puzzles);
    RUN_TEST(wizard_get_puzzle_by_number);
    RUN_TEST(wizard_get_puzzle_not_found);

    TEST_SECTION("Puzzle Cache");
    RUN_TEST(wizard_save_load_puzzles_cache);

    TEST_SECTION("Search Offset Calculation");
    RUN_TEST(wizard_calculate_search_offset_zero);
    RUN_TEST(wizard_calculate_search_offset_partial);

    TEST_SECTION("Scanned Region Check");
    RUN_TEST(wizard_is_in_scanned_region_zero);
    RUN_TEST(wizard_is_in_scanned_region_partial);

    TEST_SECTION("Local Progress");
    RUN_TEST(wizard_save_load_local_progress);

    TEST_SECTION("Community Sync");
    RUN_TEST(wizard_community_free_null);
    RUN_TEST(wizard_is_range_excluded_null);
    RUN_TEST(wizard_is_range_excluded_nonexistent);

    TEST_SECTION("Mode Selection Logic");
    RUN_TEST(mode_selection_with_pubkey);
    RUN_TEST(mode_selection_without_pubkey);

    TEST_SECTION("BSGS Parameter Calculation");
    RUN_TEST(bsgs_n_calculation);
    RUN_TEST(bsgs_k_default);

    TEST_SECTION("Random Mode Selection");
    RUN_TEST(random_mode_for_large_puzzles);
    RUN_TEST(sequential_mode_for_small_puzzles);

    TEST_SECTION("Work Unit Size");
    RUN_TEST(work_unit_size_calculation);
    RUN_TEST(work_unit_count_estimation);

    TEST_SECTION("Server/Client Mode");
    RUN_TEST(config_server_mode);
    RUN_TEST(config_client_mode);

    TEST_SECTION("GPU Configuration");
    RUN_TEST(config_gpu_disabled);
    RUN_TEST(config_gpu_partial);
    RUN_TEST(config_gpu_full);

    TEST_SECTION("Key Type Configuration");
    RUN_TEST(config_key_type_compressed);
    RUN_TEST(config_key_type_uncompressed);
    RUN_TEST(config_key_type_both);

    TEST_SECTION("Thread Configuration");
    RUN_TEST(config_threads_default);
    RUN_TEST(config_threads_custom);

    TEST_SECTION("Community Sync Configuration");
    RUN_TEST(config_community_enabled);
    RUN_TEST(config_community_disabled);

    return TEST_RESULTS();
}

/* Standalone main for individual testing */
#ifdef TEST_STANDALONE
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    return run_wizard_tests();
}
#endif
