/*
 * wizard.c - Main wizard entry point
 *
 * Usage: ./keyhunt --wizard  or  ./keyhunt -W
 */

#include "wizard.h"
#include "../sysinfo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PUZZLES_CACHE_FILE "puzzles_cache.txt"
#define CONFIG_FILE "keyhunt_wizard.json"

/* ============================================================================
 * Puzzle Selection
 * ============================================================================ */

static int wizard_select_puzzle(wizard_config_t *cfg) {
    /* Try to load from cache first */
    puzzle_def_t *puzzles = NULL;
    int count = 0;

    if (wizard_load_puzzles_from_txt(PUZZLES_CACHE_FILE, &puzzles, &count) != 0 || count == 0) {
        /* Try to download */
        printf("\n[+] Downloading puzzle database...\n");
        if (wizard_download_puzzles(&puzzles, &count) != 0 || count == 0) {
            /* Fallback to builtin */
            printf("[i] Using built-in puzzle database\n");
            puzzles = (puzzle_def_t*)wizard_get_builtin_puzzles(&count);
        } else {
            /* Save cache */
            wizard_save_puzzles_to_txt(puzzles, count, PUZZLES_CACHE_FILE);
            printf("[+] Saved %d puzzles to %s\n", count, PUZZLES_CACHE_FILE);
        }
    } else {
        printf("[+] Loaded %d puzzles from cache\n", count);
    }

    /* Filter to unsolved puzzles */
    const char *options[32];
    int puzzle_indices[32];
    char labels[32][80];
    int n = 0;

    for (int i = 0; i < count && n < 32; i++) {
        if (puzzles[i].solved) continue;  /* Skip solved */
        if (puzzles[i].number < 71) continue;  /* Skip easy ones */

        const char *note = "";
        if (puzzles[i].has_public_key) {
            note = " [HAS PUBKEY - EASIER!]";
        }

        snprintf(labels[n], sizeof(labels[n]),
                 "Puzzle #%-3d | %3d bits | %.2f BTC%s",
                 puzzles[i].number, puzzles[i].bits, puzzles[i].reward_btc, note);

        options[n] = labels[n];
        puzzle_indices[n] = i;
        n++;
    }

    if (n == 0) {
        printf("[-] No unsolved puzzles found!\n");
        return -1;
    }

    int choice = wizard_ask_choice("Select puzzle to solve:", options, n, 0);
    int idx = puzzle_indices[choice];

    /* Copy puzzle info to config */
    cfg->puzzle_number = puzzles[idx].number;
    strncpy(cfg->target_address, puzzles[idx].target_address, sizeof(cfg->target_address) - 1);
    strncpy(cfg->range_start, puzzles[idx].range_start, sizeof(cfg->range_start) - 1);
    strncpy(cfg->range_end, puzzles[idx].range_end, sizeof(cfg->range_end) - 1);
    cfg->bits = puzzles[idx].bits;

    /* For puzzles with public key, suggest BSGS mode */
    if (puzzles[idx].has_public_key) {
        printf("\n[!] This puzzle has an exposed public key!\n");
        printf("    Consider using BSGS or Kangaroo algorithm for O(sqrt(N)) complexity.\n");
        strcpy(cfg->mode, "bsgs");
    }

    return 0;
}

/* ============================================================================
 * Server Configuration
 * ============================================================================ */

static int wizard_configure_server(wizard_config_t *cfg) {
    cfg->server_port = wizard_ask_int("Server port", 1024, 65535, 7777);

    const char *unit_options[] = {
        "4 billion keys/unit (~80s CPU, ~8s GPU) - Recommended",
        "1 billion keys/unit (faster progress updates)",
        "16 billion keys/unit (less network overhead)"
    };
    uint64_t unit_sizes[] = {0x100000000ULL, 0x40000000ULL, 0x400000000ULL};

    int choice = wizard_ask_choice("Work unit size:", unit_options, 3, 0);
    cfg->work_unit_size = unit_sizes[choice];

    cfg->server_also_worker = wizard_ask_yesno(
        "Also run worker on this machine (server+worker)?", true);

    cfg->checkpoint_interval_sec = wizard_ask_int(
        "Checkpoint interval (seconds)", 60, 3600, 300);

    return 0;
}

/* ============================================================================
 * Optimal Configuration Calculator
 * ============================================================================ */

typedef struct {
    const char *recommended_mode;
    const char *recommended_key_type;
    bool recommended_random;
    uint64_t recommended_work_unit;
    uint64_t bsgs_n;
    int bsgs_k;
    double estimated_time_years;
    const char *difficulty_rating;
} puzzle_recommendation_t;

static void calculate_puzzle_recommendation(int bits, bool has_pubkey,
                                             uint64_t ram_mb,
                                             puzzle_recommendation_t *rec) {
    /* Default recommendations */
    rec->recommended_key_type = "compress";  /* Most puzzles use compressed */
    rec->bsgs_n = 0;
    rec->bsgs_k = 1;

    /* Mode selection based on public key availability */
    if (has_pubkey) {
        rec->recommended_mode = "bsgs";

        /* Calculate optimal BSGS N and K based on available RAM */
        /* Formula: RAM = (M * K * 3.5) + (M/32 * K * 16) bytes */
        /* where M = sqrt(search_space), simplified: M ~ 2^(bits/2) */
        uint64_t safe_ram = (ram_mb * 60 / 100) * 1024 * 1024;  /* 60% of RAM */

        /* For BSGS, we want M*K to fit in RAM */
        /* Each entry ~20 bytes (bloom + table) */
        uint64_t max_entries = safe_ram / 20;

        /* Calculate optimal N (power of 2) */
        int n_bits = 28;  /* Start with 2^28 = 256M entries */
        while (n_bits < 40 && ((1ULL << n_bits) * 2) <= max_entries) {
            n_bits++;
        }
        rec->bsgs_n = 1ULL << n_bits;

        /* K factor: balance between speed and RAM */
        rec->bsgs_k = (int)(max_entries / rec->bsgs_n);
        if (rec->bsgs_k < 1) rec->bsgs_k = 1;
        if (rec->bsgs_k > 8192) rec->bsgs_k = 8192;

        /* BSGS is O(sqrt(N)), so random doesn't help much */
        rec->recommended_random = false;

        /* Smaller work units for BSGS (more progress updates) */
        rec->recommended_work_unit = 0x40000000ULL;  /* 1 billion */

    } else {
        rec->recommended_mode = "address";

        /* For address mode, random is better for large ranges */
        rec->recommended_random = (bits >= 66);

        /* Work unit size based on bit range */
        if (bits <= 50) {
            rec->recommended_work_unit = 0x10000000ULL;   /* 256M - small range */
        } else if (bits <= 70) {
            rec->recommended_work_unit = 0x100000000ULL;  /* 4B - standard */
        } else {
            rec->recommended_work_unit = 0x400000000ULL;  /* 16B - large range */
        }
    }

    /* Difficulty rating and time estimate */
    /* Assuming 50 Mkeys/s for address, sqrt speedup for BSGS */
    double keys_per_year = 50e6 * 365.25 * 24 * 3600;  /* ~1.58e15 keys/year */

    if (has_pubkey) {
        /* BSGS: O(sqrt(N)) complexity */
        double sqrt_range = 1.0;
        for (int i = 0; i < bits / 2; i++) sqrt_range *= 2.0;
        rec->estimated_time_years = sqrt_range / keys_per_year;

        if (bits <= 80) rec->difficulty_rating = "EASY (has pubkey)";
        else if (bits <= 120) rec->difficulty_rating = "MODERATE (has pubkey)";
        else if (bits <= 160) rec->difficulty_rating = "HARD (has pubkey)";
        else rec->difficulty_rating = "VERY HARD";
    } else {
        /* Address mode: O(N) complexity */
        double full_range = 1.0;
        for (int i = 0; i < bits; i++) full_range *= 2.0;
        rec->estimated_time_years = full_range / keys_per_year;

        if (bits <= 50) rec->difficulty_rating = "EASY";
        else if (bits <= 66) rec->difficulty_rating = "MODERATE";
        else if (bits <= 72) rec->difficulty_rating = "HARD";
        else if (bits <= 80) rec->difficulty_rating = "VERY HARD";
        else rec->difficulty_rating = "PRACTICALLY IMPOSSIBLE";
    }
}

static void print_puzzle_recommendation(const puzzle_recommendation_t *rec,
                                         int bits, bool has_pubkey) {
    printf("\n[+] \033[1;33mOptimal Configuration for %d-bit puzzle:\033[0m\n", bits);
    printf("    ┌──────────────────────────────────────────────────────────┐\n");
    printf("    │ Difficulty: \033[1m%-45s\033[0m │\n", rec->difficulty_rating);
    printf("    │ Best Mode:  \033[1;32m%-45s\033[0m │\n", rec->recommended_mode);

    if (has_pubkey) {
        printf("    │ Strategy:   BSGS with O(√N) complexity                   │\n");
        printf("    │ BSGS N:     0x%llx (%llu M entries)%*s│\n",
               (unsigned long long)rec->bsgs_n,
               (unsigned long long)(rec->bsgs_n / 1000000),
               rec->bsgs_n >= 0x100000000ULL ? 14 : 16, "");
        printf("    │ BSGS K:     %-47d │\n", rec->bsgs_k);
    } else {
        printf("    │ Strategy:   Brute-force address search                   │\n");
        if (bits > 72) {
            printf("    │ \033[1;31mWARNING: Without pubkey, this puzzle is extremely hard!\033[0m │\n");
        }
    }

    printf("    │ Key Type:   %-47s │\n", rec->recommended_key_type);
    printf("    │ Random:     %-47s │\n", rec->recommended_random ? "Yes (better coverage)" : "No (sequential)");

    if (rec->estimated_time_years < 0.01) {
        printf("    │ Est. Time:  < 1 week (single machine)                     │\n");
    } else if (rec->estimated_time_years < 1.0) {
        printf("    │ Est. Time:  ~%.1f months (single machine)                  │\n",
               rec->estimated_time_years * 12);
    } else if (rec->estimated_time_years < 1000) {
        printf("    │ Est. Time:  ~%.0f years (single machine)                   │\n",
               rec->estimated_time_years);
    } else {
        printf("    │ Est. Time:  ~%.2e years (need distributed!)             │\n",
               rec->estimated_time_years);
    }
    printf("    └──────────────────────────────────────────────────────────┘\n");
}

/* ============================================================================
 * Search Configuration
 * ============================================================================ */

static int wizard_configure_search(wizard_config_t *cfg) {
    /* Detect hardware */
    system_info_t sysinfo;
    sysinfo_init(&sysinfo);

    printf("\n[+] Hardware Detection:\n");
    printf("    CPU: %d physical cores, %d logical threads\n",
           sysinfo.cpu_physical_cores, sysinfo.cpu_logical_cores);
    printf("    RAM: %llu MB total, %llu MB available\n",
           (unsigned long long)sysinfo.ram_total,
           (unsigned long long)sysinfo.ram_available);
    printf("    Cache: L1=%lluKB L2=%lluKB L3=%lluKB\n",
           (unsigned long long)sysinfo.cache_l1_size,
           (unsigned long long)sysinfo.cache_l2_size,
           (unsigned long long)sysinfo.cache_l3_size);
    printf("    Features: %s%s%s\n",
           sysinfo.has_avx2 ? "AVX2 " : "",
           sysinfo.has_avx512f ? "AVX512 " : "",
           sysinfo.has_sha_ni ? "SHA-NI " : "");

    if (sysinfo.has_cuda) {
        printf("    GPU: %s (%llu MB VRAM)\n",
               sysinfo.gpu_name, (unsigned long long)sysinfo.gpu_vram_mb);
    }

    sysinfo_compute_scores(&sysinfo);
    printf("    Performance Score: CPU=%.1f GPU=%.1f\n",
           sysinfo.cpu_score, sysinfo.gpu_score);

    /* Calculate and show puzzle-specific recommendations */
    bool has_pubkey = (strcmp(cfg->mode, "bsgs") == 0);
    puzzle_recommendation_t rec;
    calculate_puzzle_recommendation(cfg->bits, has_pubkey,
                                     sysinfo.ram_available, &rec);
    print_puzzle_recommendation(&rec, cfg->bits, has_pubkey);

    /* Apply recommended work unit size */
    cfg->work_unit_size = rec.recommended_work_unit;

    /* Mode selection - offer choice but highlight recommendation */
    if (has_pubkey) {
        printf("\n[!] \033[1;32mBSGS mode auto-selected\033[0m (puzzle has public key)\n");
        /* Mode already set to bsgs in wizard_select_puzzle */
    } else {
        const char *mode_options[] = {
            "address - Standard address search (Recommended)",
            "rmd160 - Search by RIPEMD160 hash",
            "xpoint - Search by X coordinate (needs pubkey file)"
        };
        const char *mode_values[] = {"address", "rmd160", "xpoint"};

        int choice = wizard_ask_choice("Search mode:", mode_options, 3, 0);
        strncpy(cfg->mode, mode_values[choice], sizeof(cfg->mode) - 1);
    }

    /* Thread count */
    int default_threads = sysinfo.cpu_logical_cores;
    cfg->threads = wizard_ask_int("Number of CPU threads",
                                   1, sysinfo.cpu_logical_cores * 2,
                                   default_threads);

    /* GPU usage - only for address/rmd160 modes */
    if (strcmp(cfg->mode, "bsgs") != 0 && sysinfo.has_cuda) {
        int suggested_gpu = sysinfo_get_hybrid_gpu_percent(&sysinfo);
        cfg->gpu_percent = wizard_ask_int("GPU usage percent (0=disabled)",
                                           0, 100, suggested_gpu);
    } else if (strcmp(cfg->mode, "bsgs") == 0) {
        printf("[i] GPU not used in BSGS mode (CPU-optimized)\n");
        cfg->gpu_percent = 0;
    } else {
        printf("[i] No CUDA GPU detected, using CPU only\n");
        cfg->gpu_percent = 0;
    }

    /* Key type - default based on recommendation */
    const char *key_options[] = {
        "compressed only (2x faster, most puzzles use this)",
        "uncompressed only",
        "both (slower but complete)"
    };
    const char *key_values[] = {"compress", "uncompress", "both"};

    int key_default = 0;  /* compressed */
    int choice = wizard_ask_choice("Key type to search:", key_options, 3, key_default);
    strncpy(cfg->key_type, key_values[choice], sizeof(cfg->key_type) - 1);

    /* Random mode - default based on puzzle analysis */
    const char *random_prompt;
    if (rec.recommended_random) {
        random_prompt = "Use random mode? (Recommended for this puzzle)";
    } else {
        random_prompt = "Use random mode? (Sequential recommended for this puzzle)";
    }
    cfg->random_mode = wizard_ask_yesno(random_prompt, rec.recommended_random);

    return 0;
}

/* ============================================================================
 * Community Configuration
 * ============================================================================ */

static int wizard_configure_community(wizard_config_t *cfg) {
    cfg->community_enabled = wizard_ask_yesno(
        "Fetch community progress from BTCPuzzle.info?", true);

    if (!cfg->community_enabled) {
        return 0;
    }

    printf("\n[+] Fetching community data for puzzle #%d...\n", cfg->puzzle_number);

    community_range_t *ranges = NULL;
    int count = 0;

    if (wizard_community_fetch(cfg->puzzle_number, &ranges, &count) == 0) {
        cfg->community_excluded = count;
        cfg->community_last_sync = time(NULL);

        if (count > 0) {
            printf("[+] Found %d ranges already scanned by community\n", count);

            if (wizard_ask_yesno("Add these to exclusion list?", true)) {
                wizard_community_merge_exclusions(cfg->exclusion_file, ranges, count);
            }

            wizard_community_free(ranges, count);
        } else {
            printf("[i] No community progress data found\n");
        }
    } else {
        printf("[-] Could not fetch community data (network error?)\n");
    }

    cfg->community_sync_interval_sec = wizard_ask_int(
        "Re-sync interval (seconds, 0=never)", 0, 86400, 3600);

    return 0;
}

/* ============================================================================
 * Main Wizard Entry Point
 * ============================================================================ */

int wizard_run(void) {
    wizard_config_t cfg;
    wizard_config_init(&cfg);

    wizard_print_header("KEYHUNT INTERACTIVE WIZARD");

    printf("Welcome to the Keyhunt Wizard!\n");
    printf("This will guide you through setting up a distributed Bitcoin puzzle search.\n");

    /* Check for existing config */
    if (wizard_config_load(&cfg, CONFIG_FILE) == 0) {
        printf("\n[+] Found existing configuration:\n");
        wizard_print_config_summary(&cfg);

        if (wizard_ask_yesno("\nResume with this configuration?", true)) {
            goto start_mode;
        }
    }

    /* === STEP 1: Puzzle Selection === */
    wizard_print_step(1, 5, "Puzzle Selection");

    if (wizard_select_puzzle(&cfg) != 0) {
        printf("[-] Failed to select puzzle\n");
        return -1;
    }

    printf("\n[+] Selected: Puzzle #%d (%d bits)\n", cfg.puzzle_number, cfg.bits);
    printf("    Target: %s\n", cfg.target_address);
    printf("    Range: %s - %s\n", cfg.range_start, cfg.range_end);

    /* === STEP 2: Mode Selection === */
    wizard_print_step(2, 5, "Mode Selection");

    const char *mode_options[] = {
        "SERVER - Start a new search (coordinator + local worker)",
        "CLIENT - Join an existing server as worker"
    };

    int mode = wizard_ask_choice("Select mode:", mode_options, 2, 0);
    cfg.is_server = (mode == 0);

    if (cfg.is_server) {
        /* === STEP 3: Server Configuration === */
        wizard_print_step(3, 5, "Server Configuration");
        wizard_configure_server(&cfg);

        /* === STEP 4: Search Configuration === */
        wizard_print_step(4, 5, "Search Configuration");
        wizard_configure_search(&cfg);

        /* === STEP 5: Community Sync === */
        wizard_print_step(5, 5, "Community Integration");
        wizard_configure_community(&cfg);

    } else {
        /* === STEP 3: Server Connection === */
        wizard_print_step(3, 5, "Server Connection");

        wizard_ask_string("Server IP or hostname", cfg.server_host,
                          sizeof(cfg.server_host), "localhost");
        cfg.server_port = wizard_ask_int("Server port", 1024, 65535, 7777);

        /* === STEP 4: Search Configuration === */
        wizard_print_step(4, 5, "Local Worker Configuration");
        wizard_configure_search(&cfg);

        /* === STEP 5: Skip for client === */
        wizard_print_step(5, 5, "Ready");
        printf("[i] Configuration will be received from server\n");
    }

    /* === Summary === */
    wizard_print_config_summary(&cfg);

    if (!wizard_ask_yesno("\nSave and start?", true)) {
        printf("\n[!] Cancelled by user\n");
        return 1;
    }

    /* Save configuration */
    if (wizard_config_save(&cfg, CONFIG_FILE) == 0) {
        printf("\n[+] Configuration saved to %s\n", CONFIG_FILE);
    }

start_mode:
    /* Start in selected mode */
    printf("\n");
    wizard_print_separator();

    if (cfg.is_server) {
        return wizard_server_run(&cfg);
    } else {
        return wizard_client_run(&cfg);
    }
}
