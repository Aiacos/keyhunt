/*
 * wizard.c - Main wizard entry point
 *
 * Usage: ./keyhunt --wizard  or  ./keyhunt -W
 */

#include "wizard.h"
#include "../core/sysinfo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define PUZZLES_CACHE_FILE "puzzles_cache.txt"
#define CONFIG_FILE "keyhunt_wizard.json"

/* ============================================================================
 * Puzzle Selection
 * ============================================================================ */

static int wizard_select_puzzle(wizard_config_t *cfg) {
    /* Try to load from cache first */
    puzzle_def_t *puzzles = NULL;
    int count = 0;
    bool puzzles_allocated = false;  /* Track if we need to free puzzles */

    if (wizard_load_puzzles_from_txt(PUZZLES_CACHE_FILE, &puzzles, &count) != 0 || count == 0) {
        /* Try to download */
        printf("\n[+] Downloading puzzle database...\n");
        if (wizard_download_puzzles(&puzzles, &count) != 0 || count == 0) {
            /* Fallback to builtin (static, don't free) */
            printf("[i] Using built-in puzzle database\n");
            puzzles = (puzzle_def_t*)wizard_get_builtin_puzzles(&count);
            puzzles_allocated = false;
        } else {
            /* Save cache - puzzles were dynamically allocated */
            wizard_save_puzzles_to_txt(puzzles, count, PUZZLES_CACHE_FILE);
            printf("[+] Saved %d puzzles to %s\n", count, PUZZLES_CACHE_FILE);
            puzzles_allocated = true;
        }
    } else {
        printf("[+] Loaded %d puzzles from cache\n", count);
        puzzles_allocated = true;  /* Loaded from file, dynamically allocated */
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
        if (puzzles_allocated && puzzles) free(puzzles);
        return -1;
    }

    int choice = wizard_ask_choice("Select puzzle to solve:", options, n, 0);
    int idx = puzzle_indices[choice];

    /* Copy puzzle info to config - ensure null termination */
    cfg->puzzle_number = puzzles[idx].number;
    strncpy(cfg->target_address, puzzles[idx].target_address, sizeof(cfg->target_address) - 1);
    cfg->target_address[sizeof(cfg->target_address) - 1] = '\0';
    strncpy(cfg->range_start, puzzles[idx].range_start, sizeof(cfg->range_start) - 1);
    cfg->range_start[sizeof(cfg->range_start) - 1] = '\0';
    strncpy(cfg->range_end, puzzles[idx].range_end, sizeof(cfg->range_end) - 1);
    cfg->range_end[sizeof(cfg->range_end) - 1] = '\0';
    cfg->bits = puzzles[idx].bits;

    /* For puzzles with public key, suggest BSGS mode */
    if (puzzles[idx].has_public_key) {
        printf("\n[!] This puzzle has an exposed public key!\n");
        printf("    Consider using BSGS or Kangaroo algorithm for O(sqrt(N)) complexity.\n");
        strcpy(cfg->mode, "bsgs");
    }

    /* Free dynamically allocated puzzles array */
    if (puzzles_allocated && puzzles) {
        free(puzzles);
    }

    return 0;
}

/* ============================================================================
 * Server Configuration
 * ============================================================================ */

static int wizard_configure_server(wizard_config_t *cfg) {
    /* Auto-apply optimal server configuration */
    printf("\n\033[1;36m[AUTO-CONFIG] Server Settings\033[0m\n");

    /* Server port - use default */
    cfg->server_port = 7777;
    printf("  ✓ Server port: %d\n", cfg->server_port);

    /* Work unit size - will be set by search configuration based on puzzle analysis */
    /* (keeping default here, wizard_configure_search will override with optimal) */
    cfg->work_unit_size = 0x100000000ULL;  /* 4 billion - will be overridden */

    /* Server also acts as worker - always enabled for efficiency */
    cfg->server_also_worker = true;
    printf("  ✓ Server also worker: enabled (maximum efficiency)\n");

    /* Checkpoint interval - reasonable default */
    cfg->checkpoint_interval_sec = 300;
    printf("  ✓ Checkpoint interval: %d seconds\n", cfg->checkpoint_interval_sec);

    /* Bind to all interfaces */
    strncpy(cfg->server_host, "0.0.0.0", sizeof(cfg->server_host) - 1);
    cfg->server_host[sizeof(cfg->server_host) - 1] = '\0';
    printf("  ✓ Bind address: %s (all interfaces)\n", cfg->server_host);

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

        /* Work units for BSGS - larger for continuous execution
         * Target: ~60+ seconds per unit to minimize subprocess overhead */
        rec->recommended_work_unit = 0x100000000ULL;  /* 4G - ~60+ seconds per unit */

    } else {
        rec->recommended_mode = "address";

        /* For address mode, random is better for large ranges */
        rec->recommended_random = (bits >= 66);

        /* Work unit size: LARGE for continuous CPU utilization
         *
         * IMPORTANT: Each work unit spawns a subprocess (fork/exec).
         * The subprocess startup/shutdown overhead is ~0.5-1 second.
         * To maintain >99% CPU utilization:
         *   - Target at least 60 seconds per work unit
         *   - At 50 Mkeys/s: 60s * 50M = 3B keys minimum
         *   - At 100 Mkeys/s (GPU): need 6B keys minimum
         *
         * Larger units = fewer subprocess spawns = more stable CPU usage.
         * The timeout is 10 minutes so units up to 30B keys are safe. */
        if (bits <= 50) {
            rec->recommended_work_unit = 0x100000000ULL;  /* 4G - ~80 seconds @ 50 Mkeys/s */
        } else if (bits <= 75) {
            rec->recommended_work_unit = 0x200000000ULL;  /* 8G - ~160 seconds @ 50 Mkeys/s */
        } else {
            rec->recommended_work_unit = 0x400000000ULL;  /* 16G - ~320 seconds @ 50 Mkeys/s */
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

    /* Calculate puzzle-specific optimal configuration */
    bool has_pubkey = (strcmp(cfg->mode, "bsgs") == 0);
    puzzle_recommendation_t rec;
    calculate_puzzle_recommendation(cfg->bits, has_pubkey,
                                     sysinfo.ram_available, &rec);
    print_puzzle_recommendation(&rec, cfg->bits, has_pubkey);

    /* ==========================================================================
     * AUTO-APPLY ALL OPTIMAL VALUES (no user prompts)
     * ========================================================================== */
    printf("\n\033[1;36m[AUTO-CONFIG] Applying optimal configuration...\033[0m\n");

    /* 1. Mode - auto-select based on public key availability */
    strncpy(cfg->mode, rec.recommended_mode, sizeof(cfg->mode) - 1);
    cfg->mode[sizeof(cfg->mode) - 1] = '\0';
    printf("  ✓ Mode: %s\n", cfg->mode);

    /* 2. Work unit size */
    cfg->work_unit_size = rec.recommended_work_unit;
    printf("  ✓ Work unit: 0x%llx (%llu keys)\n",
           (unsigned long long)cfg->work_unit_size,
           (unsigned long long)cfg->work_unit_size);

    /* 3. Threads - save as "auto" (-1) so worker always uses all available cores
     * This ensures the saved config works correctly on different machines
     * or when resuming after hardware changes. */
    cfg->threads = -1;  /* -1 means "auto-detect at runtime" */
    printf("  ✓ Threads: auto (%d logical cores detected)\n", sysinfo.cpu_logical_cores);

    /* 4. GPU usage - save as 0 to trigger auto-detection at runtime
     * This allows GPU to be auto-enabled on machines that have one,
     * even if the config was created on a machine without GPU. */
    if (strcmp(cfg->mode, "bsgs") == 0) {
        cfg->gpu_percent = 0;  /* BSGS is CPU-optimized, keep disabled */
        printf("  ✓ GPU: disabled (BSGS is CPU-optimized)\n");
    } else {
        cfg->gpu_percent = 0;  /* 0 means "auto-detect at runtime" */
        if (sysinfo.has_cuda) {
            printf("  ✓ GPU: auto (%d%% detected for %s)\n",
                   sysinfo_get_hybrid_gpu_percent(&sysinfo), sysinfo.gpu_name);
        } else {
            printf("  ✓ GPU: auto (will enable if GPU detected)\n");
        }
    }

    /* 5. Key type - always compressed (2x faster, standard for puzzles) */
    strncpy(cfg->key_type, rec.recommended_key_type, sizeof(cfg->key_type) - 1);
    cfg->key_type[sizeof(cfg->key_type) - 1] = '\0';
    printf("  ✓ Key type: %s\n", cfg->key_type);

    /* 6. Random mode - based on puzzle analysis */
    cfg->random_mode = rec.recommended_random;
    printf("  ✓ Search mode: %s\n", cfg->random_mode ? "random" : "sequential");

    /* 7. BSGS-specific parameters */
    if (strcmp(cfg->mode, "bsgs") == 0) {
        cfg->bsgs_n = rec.bsgs_n;
        cfg->bsgs_k = rec.bsgs_k;
        printf("  ✓ BSGS N: 0x%llx (%.1f GB table)\n",
               (unsigned long long)cfg->bsgs_n,
               (double)(cfg->bsgs_n * 20) / (1024.0 * 1024.0 * 1024.0));
        printf("  ✓ BSGS K: %d\n", cfg->bsgs_k);
    }

    printf("\n\033[1;32m[OK] Configuration optimized for your hardware\033[0m\n");

    return 0;
}

/* ============================================================================
 * Community Configuration
 * ============================================================================ */

static int wizard_configure_community(wizard_config_t *cfg) {
    /* Auto-enable community sync for distributed coordination */
    cfg->community_enabled = true;
    cfg->community_sync_interval_sec = 3600;  /* 1 hour default */

    printf("\n\033[1;36m[AUTO-CONFIG] Community Progress Integration\033[0m\n");
    printf("  ✓ Community sync: enabled\n");
    printf("  ✓ Sync interval: %d seconds\n", cfg->community_sync_interval_sec);

    /* Fetch BTCPuzzle.info data (existing) */
    printf("\n[+] Fetching BTCPuzzle.info data for puzzle #%d...\n", cfg->puzzle_number);

    community_range_t *ranges = NULL;
    int count = 0;

    if (wizard_community_fetch(cfg->puzzle_number, &ranges, &count) == 0) {
        cfg->community_excluded = count;
        cfg->community_last_sync = time(NULL);

        if (count > 0) {
            printf("  ✓ BTCPuzzle.info: %d ranges already scanned\n", count);
            wizard_community_merge_exclusions(cfg->exclusion_file, ranges, count);
            wizard_community_free(ranges, count);
        } else {
            printf("  ✓ BTCPuzzle.info: no community data yet\n");
        }
    } else {
        printf("  ! BTCPuzzle.info: could not fetch (network error?)\n");
    }

    /* Fetch privatekeys.pw cloud search progress (NEW) */
    printf("\n[+] Fetching privatekeys.pw cloud search progress...\n");

    privatekeys_progress_t pk_progress;
    if (wizard_privatekeys_get_progress(cfg->puzzle_number, &pk_progress) == 0) {
        cfg->privatekeys_percent = pk_progress.percent_scanned;

        printf("  ✓ privatekeys.pw: %.4f%% scanned by community\n", pk_progress.percent_scanned);

        /* Apply based on mode */
        if (!cfg->random_mode && pk_progress.percent_scanned > 0.0) {
            /* Sequential mode: adjust range_start */
            const puzzle_def_t *puzzle = wizard_get_puzzle(cfg->puzzle_number);
            if (puzzle) {
                char original_start[68];
                strcpy(original_start, cfg->range_start);

                wizard_calculate_search_offset(puzzle, pk_progress.percent_scanned, cfg->range_start);

                printf("  ✓ Sequential mode: adjusted start from %s to %s\n",
                       original_start, cfg->range_start);
            }
        } else if (cfg->random_mode) {
            printf("  ✓ Random mode: will exclude first %.4f%% from random selection\n",
                   pk_progress.percent_scanned);
        }
    } else {
        cfg->privatekeys_percent = 0.0;
        printf("  ! privatekeys.pw: no data available (searching full range)\n");
    }

    /* Load local progress count */
    int local_count = wizard_load_local_progress_count(cfg->puzzle_number);
    if (local_count > 0) {
        printf("  ✓ Local progress: %d ranges previously completed\n", local_count);
    }

    /* === Webhook Notifications === */
    printf("\n");
    wizard_print_separator();
    printf("\n\033[1mOptional: Webhook Notifications\033[0m\n");
    printf("Get instant notifications when a key is found!\n\n");

    /* Discord webhook */
    bool enable_discord = wizard_ask_yesno("Configure Discord webhook?", false);
    if (enable_discord) {
        printf("\n\033[1;36m[i] Discord Webhook Setup\033[0m\n");
        printf("To create a webhook:\n");
        printf("  1. Open your Discord server settings\n");
        printf("  2. Go to Integrations → Webhooks → New Webhook\n");
        printf("  3. Copy the webhook URL\n\n");

        wizard_ask_string(
            "Discord webhook URL",
            cfg->webhook_discord_url,
            sizeof(cfg->webhook_discord_url),
            "");

        if (cfg->webhook_discord_url[0] != '\0') {
            printf("\033[1;32m  ✓ Discord notifications enabled\033[0m\n");
        }
    } else {
        cfg->webhook_discord_url[0] = '\0';
    }

    /* Telegram webhook */
    bool enable_telegram = wizard_ask_yesno("Configure Telegram webhook?", false);
    if (enable_telegram) {
        printf("\n\033[1;36m[i] Telegram Webhook Setup\033[0m\n");
        printf("To create a bot:\n");
        printf("  1. Message @BotFather on Telegram\n");
        printf("  2. Send /newbot and follow instructions\n");
        printf("  3. Copy your bot token\n");
        printf("  4. Format: https://api.telegram.org/bot<token>/sendMessage?chat_id=<chat_id>\n\n");

        wizard_ask_string(
            "Telegram bot URL",
            cfg->webhook_telegram_url,
            sizeof(cfg->webhook_telegram_url),
            "");

        if (cfg->webhook_telegram_url[0] != '\0') {
            printf("\033[1;32m  ✓ Telegram notifications enabled\033[0m\n");
        }
    } else {
        cfg->webhook_telegram_url[0] = '\0';
    }

    if (cfg->webhook_discord_url[0] == '\0' && cfg->webhook_telegram_url[0] == '\0') {
        printf("\n\033[1;33m[i] Webhook notifications disabled\033[0m\n");
        printf("    You can enable them later by editing %s\n", CONFIG_FILE);
    }

    /* === Progress Reporting Opt-in === */
    printf("\n");
    wizard_print_separator();
    printf("\n\033[1mOptional: Progress Reporting\033[0m\n");

    /* Display privacy warning */
    wizard_print_privacy_warning();

    /* Ask for opt-in */
    printf("\n");
    cfg->report_progress_enabled = wizard_ask_yesno(
        "Enable progress reporting to community API?", false);

    if (cfg->report_progress_enabled) {
        printf("\n\033[1;32m[+] Progress reporting enabled\033[0m\n");

        /* Ask for reporting URL */
        const char *default_url = "https://btcpuzzle.info/api/report";
        wizard_ask_string(
            "Progress reporting API endpoint",
            cfg->report_progress_url,
            sizeof(cfg->report_progress_url),
            default_url);

        printf("  ✓ Reports will be sent to: %s\n", cfg->report_progress_url);
        printf("  ✓ Data includes: puzzle number, range, keys checked, worker ID\n");
        printf("  ✓ Frequency: Every checkpoint (%d seconds)\n",
               cfg->checkpoint_interval_sec);
    } else {
        printf("\n\033[1;33m[i] Progress reporting disabled\033[0m\n");
        printf("    You can enable it later by editing %s\n", CONFIG_FILE);
        cfg->report_progress_url[0] = '\0';  /* Clear URL */
    }

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
    printf("\n\033[1;33m═══════════════════════════════════════════════════════════════\033[0m\n");
    printf("\033[1;33m                    FINAL CONFIGURATION\033[0m\n");
    printf("\033[1;33m═══════════════════════════════════════════════════════════════\033[0m\n");
    wizard_print_config_summary(&cfg);

    /* Auto-save configuration (no prompt needed - all settings are optimal) */
    if (wizard_config_save(&cfg, CONFIG_FILE) == 0) {
        printf("\n\033[1;32m[✓] Configuration auto-saved to %s\033[0m\n", CONFIG_FILE);
    }

    /* Give user option to abort before starting */
    printf("\n[i] Press Ctrl+C now to abort, or wait 3 seconds to start...\n");
    sleep(3);

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
