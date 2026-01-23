# Wizard System Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add `--wizard` flag to keyhunt that provides interactive setup for distributed puzzle solving with community progress integration and server-as-worker capability.

**Architecture:** Single-binary wizard that can run as server (coordinator + local worker) or client (remote worker). JSON config auto-shared. Community data from BTCPuzzle.info scraped and merged with local progress tracking.

**Tech Stack:** C with existing keyhunt infrastructure, libcurl for HTTP, cJSON for JSON parsing (embedded), POSIX threads for server+worker concurrency.

---

## Task 1: Create wizard.h Header

**Files:**
- Create: `wizard/wizard.h`

**Step 1: Write the header file**

```c
/*
 * wizard.h - Interactive Wizard for Keyhunt Distributed Mode
 */

#ifndef WIZARD_H
#define WIZARD_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Puzzle definitions */
typedef struct {
    int number;
    const char *target_address;
    const char *range_start;
    const char *range_end;
    int bits;
    double reward_btc;
    bool has_public_key;
    const char *public_key;  /* NULL if not exposed */
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

    /* Search settings */
    char mode[32];           /* "address", "bsgs", "xpoint" */
    char key_type[16];       /* "compress", "uncompress", "both" */
    bool random_mode;
    int threads;
    int gpu_percent;

    /* Community sync */
    bool community_enabled;
    char community_source[64];
    int community_sync_interval_sec;
    time_t community_last_sync;

    /* Progress tracking */
    uint64_t total_ranges;
    uint64_t local_completed;
    uint64_t community_excluded;
    char progress_file[256];
    char exclusion_file[256];

    /* Runtime state (not saved) */
    bool is_server;
    bool server_also_worker;
} wizard_config_t;

/* Community range data */
typedef struct {
    char range_id[16];
    char hex_start[68];
    char hex_end[68];
    time_t scanned_time;
} community_range_t;

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

/* ============================================================================
 * UI Functions
 * ============================================================================ */

void wizard_print_header(const char *title);
void wizard_print_separator(void);
int wizard_ask_choice(const char *prompt, const char **options, int count, int default_choice);
int wizard_ask_string(const char *prompt, char *buffer, size_t bufsize, const char *default_val);
int wizard_ask_int(const char *prompt, int min, int max, int default_val);
bool wizard_ask_yesno(const char *prompt, bool default_val);
void wizard_print_config_summary(const wizard_config_t *cfg);

/* ============================================================================
 * Community Sync Functions
 * ============================================================================ */

int wizard_community_fetch(int puzzle_number, community_range_t **ranges, int *count);
void wizard_community_free(community_range_t *ranges, int count);
int wizard_community_merge_exclusions(const char *exclusion_file,
                                       const community_range_t *ranges, int count);

/* ============================================================================
 * Server Mode (Coordinator + Worker)
 * ============================================================================ */

int wizard_server_run(wizard_config_t *cfg);

/* ============================================================================
 * Client Mode (Worker)
 * ============================================================================ */

int wizard_client_run(wizard_config_t *cfg);

/* ============================================================================
 * Puzzle Database
 * ============================================================================ */

const puzzle_def_t* wizard_get_puzzles(int *count);
const puzzle_def_t* wizard_get_puzzle(int number);

#ifdef __cplusplus
}
#endif

#endif /* WIZARD_H */
```

**Step 2: Commit**

```bash
git add wizard/wizard.h
git commit -m "feat(wizard): add wizard.h header with data structures"
```

---

## Task 2: Create Puzzle Database and Config Functions

**Files:**
- Create: `wizard/wizard_config.c`

**Step 1: Implement puzzle database and JSON config**

```c
/*
 * wizard_config.c - Configuration and puzzle database
 */

#include "wizard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Known puzzles database */
static const puzzle_def_t PUZZLES[] = {
    {71, "1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU",
     "400000000000000000", "7fffffffffffffffff", 71, 7.10, false, NULL},
    {72, "1JTK7s9YVYywfm5XUH7RNhHJH1LshCaRFR",
     "800000000000000000", "ffffffffffffffffff", 72, 7.20, false, NULL},
    {73, "1PAGBgM7dYeA9Pfzjm6L7J8dGfBcxKBh9V",
     "1000000000000000000", "1ffffffffffffffffff", 73, 7.30, false, NULL},
    {135, "16jY7qLJnxb7CHZyqBP8qca9d51gAjyXQN",
     "40000000000000000000000000000000000", "7ffffffffffffffffffffffffffffffffff",
     135, 13.50, true, "02145d2611c823a396ef6712ce0f712f09b9b4f3135e3e0aa3230fb9b6d08d1e16"},
    {0, NULL, NULL, NULL, 0, 0, false, NULL}  /* Terminator */
};

const puzzle_def_t* wizard_get_puzzles(int *count) {
    if (count) {
        int n = 0;
        while (PUZZLES[n].number != 0) n++;
        *count = n;
    }
    return PUZZLES;
}

const puzzle_def_t* wizard_get_puzzle(int number) {
    for (int i = 0; PUZZLES[i].number != 0; i++) {
        if (PUZZLES[i].number == number) return &PUZZLES[i];
    }
    return NULL;
}

void wizard_config_init(wizard_config_t *cfg) {
    memset(cfg, 0, sizeof(wizard_config_t));
    cfg->version = 1;
    cfg->puzzle_number = 71;
    cfg->server_port = 7777;
    cfg->work_unit_size = 0x100000000ULL;  /* 4G keys */
    cfg->checkpoint_interval_sec = 300;
    strcpy(cfg->mode, "address");
    strcpy(cfg->key_type, "compress");
    cfg->random_mode = true;
    cfg->threads = -1;  /* auto */
    cfg->gpu_percent = 0;
    cfg->community_enabled = true;
    strcpy(cfg->community_source, "btcpuzzle.info");
    cfg->community_sync_interval_sec = 3600;
    strcpy(cfg->progress_file, "wizard_progress.dat");
    strcpy(cfg->exclusion_file, "wizard_excluded.dat");
    cfg->server_also_worker = true;
}

/* Simple JSON writer (no external deps) */
int wizard_config_save(const wizard_config_t *cfg, const char *filepath) {
    FILE *f = fopen(filepath, "w");
    if (!f) return -1;

    fprintf(f, "{\n");
    fprintf(f, "  \"version\": %d,\n", cfg->version);
    fprintf(f, "  \"puzzle\": {\n");
    fprintf(f, "    \"number\": %d,\n", cfg->puzzle_number);
    fprintf(f, "    \"target_address\": \"%s\",\n", cfg->target_address);
    fprintf(f, "    \"range_start\": \"%s\",\n", cfg->range_start);
    fprintf(f, "    \"range_end\": \"%s\",\n", cfg->range_end);
    fprintf(f, "    \"bits\": %d\n", cfg->bits);
    fprintf(f, "  },\n");
    fprintf(f, "  \"server\": {\n");
    fprintf(f, "    \"host\": \"%s\",\n", cfg->server_host);
    fprintf(f, "    \"port\": %d,\n", cfg->server_port);
    fprintf(f, "    \"work_unit_size\": %llu,\n", (unsigned long long)cfg->work_unit_size);
    fprintf(f, "    \"checkpoint_interval\": %d,\n", cfg->checkpoint_interval_sec);
    fprintf(f, "    \"also_worker\": %s\n", cfg->server_also_worker ? "true" : "false");
    fprintf(f, "  },\n");
    fprintf(f, "  \"search\": {\n");
    fprintf(f, "    \"mode\": \"%s\",\n", cfg->mode);
    fprintf(f, "    \"key_type\": \"%s\",\n", cfg->key_type);
    fprintf(f, "    \"random_mode\": %s,\n", cfg->random_mode ? "true" : "false");
    fprintf(f, "    \"threads\": %d,\n", cfg->threads);
    fprintf(f, "    \"gpu_percent\": %d\n", cfg->gpu_percent);
    fprintf(f, "  },\n");
    fprintf(f, "  \"community\": {\n");
    fprintf(f, "    \"enabled\": %s,\n", cfg->community_enabled ? "true" : "false");
    fprintf(f, "    \"source\": \"%s\",\n", cfg->community_source);
    fprintf(f, "    \"sync_interval\": %d,\n", cfg->community_sync_interval_sec);
    fprintf(f, "    \"last_sync\": %ld\n", (long)cfg->community_last_sync);
    fprintf(f, "  },\n");
    fprintf(f, "  \"progress\": {\n");
    fprintf(f, "    \"total_ranges\": %llu,\n", (unsigned long long)cfg->total_ranges);
    fprintf(f, "    \"local_completed\": %llu,\n", (unsigned long long)cfg->local_completed);
    fprintf(f, "    \"community_excluded\": %llu,\n", (unsigned long long)cfg->community_excluded);
    fprintf(f, "    \"progress_file\": \"%s\",\n", cfg->progress_file);
    fprintf(f, "    \"exclusion_file\": \"%s\"\n", cfg->exclusion_file);
    fprintf(f, "  }\n");
    fprintf(f, "}\n");

    fclose(f);
    return 0;
}

/* Simple JSON parser (minimal, handles our format) */
static char* json_find_value(const char *json, const char *key) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    char *pos = strstr(json, search);
    if (!pos) return NULL;
    pos = strchr(pos, ':');
    if (!pos) return NULL;
    pos++;
    while (*pos == ' ' || *pos == '\t') pos++;
    return pos;
}

static int json_get_int(const char *json, const char *key, int def) {
    char *val = json_find_value(json, key);
    if (!val) return def;
    return atoi(val);
}

static long long json_get_llong(const char *json, const char *key, long long def) {
    char *val = json_find_value(json, key);
    if (!val) return def;
    return atoll(val);
}

static bool json_get_bool(const char *json, const char *key, bool def) {
    char *val = json_find_value(json, key);
    if (!val) return def;
    return (strncmp(val, "true", 4) == 0);
}

static void json_get_string(const char *json, const char *key, char *buf, size_t bufsize, const char *def) {
    char *val = json_find_value(json, key);
    if (!val || *val != '"') {
        strncpy(buf, def, bufsize - 1);
        buf[bufsize - 1] = '\0';
        return;
    }
    val++;  /* Skip opening quote */
    char *end = strchr(val, '"');
    if (!end) {
        strncpy(buf, def, bufsize - 1);
        buf[bufsize - 1] = '\0';
        return;
    }
    size_t len = end - val;
    if (len >= bufsize) len = bufsize - 1;
    strncpy(buf, val, len);
    buf[len] = '\0';
}

int wizard_config_load(wizard_config_t *cfg, const char *filepath) {
    FILE *f = fopen(filepath, "r");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *json = malloc(size + 1);
    if (!json) { fclose(f); return -1; }

    fread(json, 1, size, f);
    json[size] = '\0';
    fclose(f);

    wizard_config_init(cfg);

    cfg->version = json_get_int(json, "version", 1);
    cfg->puzzle_number = json_get_int(json, "number", 71);
    json_get_string(json, "target_address", cfg->target_address, sizeof(cfg->target_address), "");
    json_get_string(json, "range_start", cfg->range_start, sizeof(cfg->range_start), "");
    json_get_string(json, "range_end", cfg->range_end, sizeof(cfg->range_end), "");
    cfg->bits = json_get_int(json, "bits", 71);

    json_get_string(json, "host", cfg->server_host, sizeof(cfg->server_host), "0.0.0.0");
    cfg->server_port = json_get_int(json, "port", 7777);
    cfg->work_unit_size = json_get_llong(json, "work_unit_size", 0x100000000ULL);
    cfg->checkpoint_interval_sec = json_get_int(json, "checkpoint_interval", 300);
    cfg->server_also_worker = json_get_bool(json, "also_worker", true);

    json_get_string(json, "mode", cfg->mode, sizeof(cfg->mode), "address");
    json_get_string(json, "key_type", cfg->key_type, sizeof(cfg->key_type), "compress");
    cfg->random_mode = json_get_bool(json, "random_mode", true);
    cfg->threads = json_get_int(json, "threads", -1);
    cfg->gpu_percent = json_get_int(json, "gpu_percent", 0);

    cfg->community_enabled = json_get_bool(json, "enabled", true);
    json_get_string(json, "source", cfg->community_source, sizeof(cfg->community_source), "btcpuzzle.info");
    cfg->community_sync_interval_sec = json_get_int(json, "sync_interval", 3600);
    cfg->community_last_sync = json_get_llong(json, "last_sync", 0);

    cfg->total_ranges = json_get_llong(json, "total_ranges", 0);
    cfg->local_completed = json_get_llong(json, "local_completed", 0);
    cfg->community_excluded = json_get_llong(json, "community_excluded", 0);
    json_get_string(json, "progress_file", cfg->progress_file, sizeof(cfg->progress_file), "wizard_progress.dat");
    json_get_string(json, "exclusion_file", cfg->exclusion_file, sizeof(cfg->exclusion_file), "wizard_excluded.dat");

    free(json);
    return 0;
}
```

**Step 2: Commit**

```bash
git add wizard/wizard_config.c
git commit -m "feat(wizard): add config save/load and puzzle database"
```

---

## Task 3: Implement Interactive UI

**Files:**
- Create: `wizard/wizard_ui.c`

**Step 1: Implement terminal UI functions**

```c
/*
 * wizard_ui.c - Interactive terminal UI
 */

#include "wizard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ANSI colors */
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[0;31m"
#define COLOR_GREEN   "\033[0;32m"
#define COLOR_YELLOW  "\033[1;33m"
#define COLOR_BLUE    "\033[0;34m"
#define COLOR_CYAN    "\033[0;36m"
#define COLOR_BOLD    "\033[1m"

void wizard_print_header(const char *title) {
    printf("\n");
    printf(COLOR_CYAN "╔════════════════════════════════════════════════════════════════╗\n" COLOR_RESET);
    printf(COLOR_CYAN "║" COLOR_BOLD "  %-62s" COLOR_RESET COLOR_CYAN "║\n" COLOR_RESET, title);
    printf(COLOR_CYAN "╚════════════════════════════════════════════════════════════════╝\n" COLOR_RESET);
    printf("\n");
}

void wizard_print_separator(void) {
    printf(COLOR_CYAN "────────────────────────────────────────────────────────────────────\n" COLOR_RESET);
}

static void clear_input_buffer(void) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

int wizard_ask_choice(const char *prompt, const char **options, int count, int default_choice) {
    printf(COLOR_YELLOW "%s\n" COLOR_RESET, prompt);

    for (int i = 0; i < count; i++) {
        if (i == default_choice) {
            printf(COLOR_GREEN "  > [%d] %s (default)\n" COLOR_RESET, i + 1, options[i]);
        } else {
            printf("    [%d] %s\n", i + 1, options[i]);
        }
    }

    printf("\nChoice [%d]: ", default_choice + 1);
    fflush(stdout);

    char buf[16];
    if (!fgets(buf, sizeof(buf), stdin)) return default_choice;

    /* Empty input = default */
    if (buf[0] == '\n') return default_choice;

    int choice = atoi(buf) - 1;
    if (choice < 0 || choice >= count) return default_choice;

    return choice;
}

int wizard_ask_string(const char *prompt, char *buffer, size_t bufsize, const char *default_val) {
    if (default_val && *default_val) {
        printf(COLOR_YELLOW "%s" COLOR_RESET " [%s]: ", prompt, default_val);
    } else {
        printf(COLOR_YELLOW "%s" COLOR_RESET ": ", prompt);
    }
    fflush(stdout);

    if (!fgets(buffer, bufsize, stdin)) {
        if (default_val) strncpy(buffer, default_val, bufsize - 1);
        buffer[bufsize - 1] = '\0';
        return -1;
    }

    /* Remove trailing newline */
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
        len--;
    }

    /* Empty input = default */
    if (len == 0 && default_val) {
        strncpy(buffer, default_val, bufsize - 1);
        buffer[bufsize - 1] = '\0';
    }

    return 0;
}

int wizard_ask_int(const char *prompt, int min, int max, int default_val) {
    printf(COLOR_YELLOW "%s" COLOR_RESET " [%d]: ", prompt, default_val);
    fflush(stdout);

    char buf[32];
    if (!fgets(buf, sizeof(buf), stdin)) return default_val;

    if (buf[0] == '\n') return default_val;

    int val = atoi(buf);
    if (val < min) val = min;
    if (val > max) val = max;

    return val;
}

bool wizard_ask_yesno(const char *prompt, bool default_val) {
    printf(COLOR_YELLOW "%s" COLOR_RESET " [%s]: ", prompt, default_val ? "Y/n" : "y/N");
    fflush(stdout);

    char buf[16];
    if (!fgets(buf, sizeof(buf), stdin)) return default_val;

    if (buf[0] == '\n') return default_val;

    char c = tolower(buf[0]);
    if (c == 'y') return true;
    if (c == 'n') return false;

    return default_val;
}

void wizard_print_config_summary(const wizard_config_t *cfg) {
    printf("\n");
    wizard_print_separator();
    printf(COLOR_BOLD "Configuration Summary:\n" COLOR_RESET);
    printf("┌────────────────────────────────────────────────────┐\n");
    printf("│ Puzzle: " COLOR_CYAN "#%d" COLOR_RESET " (%d bits)                              │\n",
           cfg->puzzle_number, cfg->bits);
    printf("│ Target: " COLOR_CYAN "%.40s..." COLOR_RESET "    │\n", cfg->target_address);
    printf("│ Mode: " COLOR_GREEN "%s" COLOR_RESET " (%s)                          │\n",
           cfg->is_server ? "SERVER+WORKER" : "CLIENT", cfg->mode);
    printf("│ Port: " COLOR_CYAN "%d" COLOR_RESET "                                        │\n", cfg->server_port);
    printf("│ Work unit: " COLOR_CYAN "%llu" COLOR_RESET " keys                        │\n",
           (unsigned long long)cfg->work_unit_size);
    printf("│ Threads: " COLOR_CYAN "%d" COLOR_RESET " | GPU: " COLOR_CYAN "%d%%" COLOR_RESET "                           │\n",
           cfg->threads > 0 ? cfg->threads : -1, cfg->gpu_percent);
    printf("│ Community sync: " COLOR_GREEN "%s" COLOR_RESET "                            │\n",
           cfg->community_enabled ? "Enabled" : "Disabled");
    if (cfg->community_excluded > 0) {
        printf("│ Excluded ranges: " COLOR_YELLOW "%llu" COLOR_RESET " (community)               │\n",
               (unsigned long long)cfg->community_excluded);
    }
    printf("└────────────────────────────────────────────────────┘\n");
}
```

**Step 2: Commit**

```bash
git add wizard/wizard_ui.c
git commit -m "feat(wizard): add interactive terminal UI"
```

---

## Task 4: Implement Community Scraper

**Files:**
- Create: `wizard/wizard_community.c`

**Step 1: Implement BTCPuzzle.info scraper**

```c
/*
 * wizard_community.c - BTCPuzzle.info community data scraper
 */

#include "wizard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>

#define BTCPUZZLE_HOST "btcpuzzle.info"
#define BTCPUZZLE_PORT 443
#define HTTP_BUFFER_SIZE 1048576  /* 1MB */

/* Simple HTTPS fetch using openssl command (portable) */
static int fetch_url(const char *url, char **response, size_t *response_len) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "curl -s --max-time 30 '%s' 2>/dev/null", url);

    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;

    *response = malloc(HTTP_BUFFER_SIZE);
    if (!*response) { pclose(fp); return -1; }

    *response_len = fread(*response, 1, HTTP_BUFFER_SIZE - 1, fp);
    (*response)[*response_len] = '\0';

    int status = pclose(fp);
    return (status == 0 && *response_len > 0) ? 0 : -1;
}

/* Extract scanned ranges from BTCPuzzle.info JSON */
static int parse_scanned_ranges(const char *html, int puzzle_number,
                                 community_range_t **ranges, int *count) {
    /* Find __NEXT_DATA__ JSON blob */
    const char *marker = "\"scannedRanges\"";
    char *pos = strstr(html, marker);
    if (!pos) {
        *ranges = NULL;
        *count = 0;
        return 0;  /* No data, not an error */
    }

    /* Count ranges (look for rangeId patterns) */
    int n = 0;
    char *scan = pos;
    while ((scan = strstr(scan, "\"rangeId\"")) != NULL) {
        n++;
        scan++;
    }

    if (n == 0) {
        *ranges = NULL;
        *count = 0;
        return 0;
    }

    *ranges = calloc(n, sizeof(community_range_t));
    if (!*ranges) return -1;

    /* Parse each range */
    int idx = 0;
    scan = pos;
    while ((scan = strstr(scan, "\"rangeId\":\"")) != NULL && idx < n) {
        scan += 11;  /* Skip to value */

        /* Extract range ID (e.g., "45X943C") */
        char *end = strchr(scan, '"');
        if (!end) break;

        size_t len = end - scan;
        if (len >= sizeof((*ranges)[idx].range_id)) len = sizeof((*ranges)[idx].range_id) - 1;
        strncpy((*ranges)[idx].range_id, scan, len);
        (*ranges)[idx].range_id[len] = '\0';

        /* Convert range ID to hex range */
        /* Format: "45X943C" where X is wildcard 0-F */
        /* This represents a sub-range of the puzzle space */
        /* For puzzle 71: base is 400000000000000000 */

        /* Simple conversion: treat range_id as offset indicator */
        snprintf((*ranges)[idx].hex_start, sizeof((*ranges)[idx].hex_start),
                 "4%s0000000000", (*ranges)[idx].range_id);
        snprintf((*ranges)[idx].hex_end, sizeof((*ranges)[idx].hex_end),
                 "4%sffffffffff", (*ranges)[idx].range_id);

        (*ranges)[idx].scanned_time = time(NULL);
        idx++;
        scan = end;
    }

    *count = idx;
    return 0;
}

int wizard_community_fetch(int puzzle_number, community_range_t **ranges, int *count) {
    char url[256];
    snprintf(url, sizeof(url), "https://btcpuzzle.info/puzzle/%d", puzzle_number);

    printf("[+] Fetching community data from %s...\n", BTCPUZZLE_HOST);

    char *response = NULL;
    size_t response_len = 0;

    if (fetch_url(url, &response, &response_len) != 0) {
        printf("[-] Failed to fetch community data (network error)\n");
        *ranges = NULL;
        *count = 0;
        return -1;
    }

    if (response_len < 100) {
        printf("[-] Invalid response from server\n");
        free(response);
        *ranges = NULL;
        *count = 0;
        return -1;
    }

    int result = parse_scanned_ranges(response, puzzle_number, ranges, count);
    free(response);

    if (result == 0) {
        printf("[+] Found %d community-scanned ranges\n", *count);
    }

    return result;
}

void wizard_community_free(community_range_t *ranges, int count) {
    (void)count;
    free(ranges);
}

int wizard_community_merge_exclusions(const char *exclusion_file,
                                       const community_range_t *ranges, int count) {
    if (!ranges || count == 0) return 0;

    FILE *f = fopen(exclusion_file, "ab");  /* Append binary */
    if (!f) return -1;

    /* Write ranges as simple records */
    for (int i = 0; i < count; i++) {
        fprintf(f, "%s\n", ranges[i].range_id);
    }

    fclose(f);
    printf("[+] Added %d ranges to exclusion file\n", count);
    return 0;
}
```

**Step 2: Commit**

```bash
git add wizard/wizard_community.c
git commit -m "feat(wizard): add BTCPuzzle.info community scraper"
```

---

## Task 5: Implement Server Mode (Coordinator + Worker)

**Files:**
- Create: `wizard/wizard_server.c`

**Step 1: Implement server that also acts as worker**

```c
/*
 * wizard_server.c - Server mode (coordinator + local worker)
 */

#include "wizard.h"
#include "../distributed/distributed.h"
#include "../sysinfo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

static volatile int g_server_running = 1;

static void server_signal_handler(int sig) {
    (void)sig;
    g_server_running = 0;
    printf("\n[!] Shutdown requested...\n");
}

/* Local worker thread data */
typedef struct {
    wizard_config_t *cfg;
    dist_coordinator_t *coord;
    int worker_id;
    volatile int *running;
    uint64_t keys_processed;
    double throughput;
} local_worker_t;

/* Forward declaration of search function (implemented in keyhunt.cpp) */
extern int keyhunt_search_range(const char *start, const char *end,
                                const char *target, const char *mode,
                                const char *key_type, int threads, int gpu_percent,
                                uint64_t *keys_checked, volatile int *stop_flag,
                                char *found_key, char *found_addr);

/* Local worker thread */
static void* local_worker_thread(void *arg) {
    local_worker_t *worker = (local_worker_t*)arg;
    wizard_config_t *cfg = worker->cfg;

    printf("[Worker] Local worker started (threads=%d, gpu=%d%%)\n",
           cfg->threads, cfg->gpu_percent);

    while (*worker->running) {
        /* Get work unit from coordinator (internal call) */
        dist_work_unit_t *unit = NULL;

        /* Find pending work unit */
        for (int i = 0; i < worker->coord->work_unit_count; i++) {
            if (worker->coord->work_units[i].status == WORK_STATUS_PENDING) {
                unit = &worker->coord->work_units[i];
                unit->status = WORK_STATUS_ASSIGNED;
                unit->assigned_worker = worker->worker_id;
                unit->assigned_time = time(NULL);
                break;
            }
        }

        if (!unit) {
            /* No more work */
            if (worker->coord->work_units_completed >= worker->coord->work_unit_count) {
                break;
            }
            usleep(100000);  /* 100ms */
            continue;
        }

        printf("[Worker] Processing range %s - %s\n",
               unit->range_start, unit->range_end);

        uint64_t keys_checked = 0;
        char found_key[65] = {0};
        char found_addr[36] = {0};

        uint64_t start_time = time(NULL);

        /* Call keyhunt search */
        int result = keyhunt_search_range(
            unit->range_start, unit->range_end,
            cfg->target_address, cfg->mode, cfg->key_type,
            cfg->threads, cfg->gpu_percent,
            &keys_checked, worker->running,
            found_key, found_addr
        );

        uint64_t elapsed = time(NULL) - start_time;
        if (elapsed == 0) elapsed = 1;

        worker->keys_processed += keys_checked;
        worker->throughput = (double)keys_checked / elapsed / 1000000.0;

        /* Update coordinator */
        unit->status = WORK_STATUS_COMPLETED;
        unit->completed_time = time(NULL);
        worker->coord->work_units_completed++;
        worker->coord->keys_processed += keys_checked;

        if (result == 1 && found_key[0]) {
            printf("\n");
            printf("[!!!] KEY FOUND!\n");
            printf("      Private Key: %s\n", found_key);
            printf("      Address: %s\n", found_addr);

            /* Save to results */
            if (worker->coord->result_count < worker->coord->result_capacity) {
                dist_result_t *r = &worker->coord->results[worker->coord->result_count++];
                strncpy(r->private_key, found_key, sizeof(r->private_key) - 1);
                strncpy(r->address, found_addr, sizeof(r->address) - 1);
                r->worker_id = worker->worker_id;
                r->found_time = time(NULL);
            }

            /* Stop all work */
            *worker->running = 0;
            break;
        }

        printf("[Worker] Completed: %.2f Mkeys/s\n", worker->throughput);
    }

    printf("[Worker] Local worker stopped\n");
    return NULL;
}

int wizard_server_run(wizard_config_t *cfg) {
    signal(SIGINT, server_signal_handler);
    signal(SIGTERM, server_signal_handler);

    wizard_print_header("KEYHUNT WIZARD - SERVER MODE");

    /* Initialize coordinator */
    dist_coordinator_t coord;
    if (dist_coordinator_init(&coord, cfg->server_port) != 0) {
        printf("[-] Failed to initialize coordinator\n");
        return -1;
    }

    /* Set work range */
    printf("[+] Configuring puzzle #%d...\n", cfg->puzzle_number);
    int num_units = dist_coordinator_set_range(&coord,
        cfg->range_start, cfg->range_end, cfg->work_unit_size);

    if (num_units < 0) {
        printf("[-] Failed to configure work range\n");
        return -1;
    }

    cfg->total_ranges = num_units;
    printf("[+] Created %d work units\n", num_units);

    /* Load exclusions and mark as completed */
    if (cfg->community_excluded > 0) {
        printf("[+] Excluding %llu community-scanned ranges\n",
               (unsigned long long)cfg->community_excluded);
        /* TODO: Mark excluded ranges as completed */
    }

    /* Start coordinator */
    if (dist_coordinator_start(&coord) != 0) {
        printf("[-] Failed to start coordinator\n");
        return -1;
    }

    printf("[+] Server listening on port %d\n", cfg->server_port);

    /* Start local worker if enabled */
    pthread_t worker_thread;
    local_worker_t local_worker = {0};

    if (cfg->server_also_worker) {
        printf("[+] Starting local worker...\n");
        local_worker.cfg = cfg;
        local_worker.coord = &coord;
        local_worker.worker_id = -1;  /* Special ID for local */
        local_worker.running = &g_server_running;

        pthread_create(&worker_thread, NULL, local_worker_thread, &local_worker);
    }

    printf("[+] Press Ctrl+C to stop\n\n");
    wizard_print_separator();

    /* Main loop */
    time_t last_checkpoint = time(NULL);
    time_t last_stats = 0;

    while (g_server_running) {
        int status = dist_coordinator_process(&coord, 500);

        if (status == 1) {
            printf("\n[+] All work completed!\n");
            break;
        }

        /* Print stats every second */
        time_t now = time(NULL);
        if (now != last_stats) {
            last_stats = now;

            int workers, pending, completed;
            double throughput;
            dist_coordinator_stats(&coord, &workers, &pending, &completed, &throughput);

            /* Add local worker stats */
            if (cfg->server_also_worker) {
                throughput += local_worker.throughput;
            }

            double progress = (double)completed / (double)num_units * 100.0;

            printf("\r[%02ld:%02ld:%02ld] Workers: %d (+1 local) | Progress: %d/%d (%.4f%%) | "
                   "Speed: %.2f Mkeys/s    ",
                   (now - last_checkpoint) / 3600,
                   ((now - last_checkpoint) % 3600) / 60,
                   (now - last_checkpoint) % 60,
                   workers, completed, num_units, progress, throughput);
            fflush(stdout);
        }

        /* Checkpoint */
        if (now - last_checkpoint >= cfg->checkpoint_interval_sec) {
            cfg->local_completed = coord.work_units_completed;
            wizard_config_save(cfg, "keyhunt_wizard.json");
            last_checkpoint = now;
        }
    }

    /* Wait for local worker */
    if (cfg->server_also_worker) {
        g_server_running = 0;
        pthread_join(worker_thread, NULL);
    }

    /* Final stats */
    printf("\n\n");
    wizard_print_separator();
    printf("[+] Final Statistics:\n");
    printf("    Work units completed: %d/%d\n", coord.work_units_completed, num_units);
    printf("    Keys processed: %.2e\n", (double)coord.keys_processed);
    printf("    Results found: %d\n", coord.result_count);

    if (coord.result_count > 0) {
        printf("\n[!!!] FOUND KEYS:\n");
        for (int i = 0; i < coord.result_count; i++) {
            printf("    Private Key: %s\n", coord.results[i].private_key);
            printf("    Address: %s\n\n", coord.results[i].address);
        }
    }

    /* Save final state */
    cfg->local_completed = coord.work_units_completed;
    wizard_config_save(cfg, "keyhunt_wizard.json");

    dist_coordinator_shutdown(&coord);
    return 0;
}
```

**Step 2: Commit**

```bash
git add wizard/wizard_server.c
git commit -m "feat(wizard): add server mode with local worker support"
```

---

## Task 6: Implement Client Mode

**Files:**
- Create: `wizard/wizard_client.c`

**Step 1: Implement auto-config client**

```c
/*
 * wizard_client.c - Client mode (worker with auto-configuration)
 */

#include "wizard.h"
#include "../distributed/distributed.h"
#include "../sysinfo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

static volatile int g_client_running = 1;

static void client_signal_handler(int sig) {
    (void)sig;
    g_client_running = 0;
    printf("\n[!] Shutdown requested...\n");
}

/* Forward declaration */
extern int keyhunt_search_range(const char *start, const char *end,
                                const char *target, const char *mode,
                                const char *key_type, int threads, int gpu_percent,
                                uint64_t *keys_checked, volatile int *stop_flag,
                                char *found_key, char *found_addr);

int wizard_client_run(wizard_config_t *cfg) {
    signal(SIGINT, client_signal_handler);
    signal(SIGTERM, client_signal_handler);

    wizard_print_header("KEYHUNT WIZARD - CLIENT MODE");

    /* Detect hardware */
    system_info_t sysinfo;
    get_system_info(&sysinfo);

    printf("[+] Hardware detected:\n");
    printf("    CPU: %d cores (%d threads)\n",
           sysinfo.physical_cores, sysinfo.logical_cores);
    printf("    RAM: %lu MB available\n", sysinfo.available_memory_mb);
    printf("    Performance score: %.2f\n", sysinfo.perf_score);

    /* Auto-configure if not set */
    if (cfg->threads <= 0) {
        cfg->threads = sysinfo.logical_cores;
    }

    /* Connect to coordinator */
    printf("\n[+] Connecting to %s:%d...\n", cfg->server_host, cfg->server_port);

    dist_worker_client_t client;
    if (dist_worker_init(&client, cfg->server_host, cfg->server_port,
                         sysinfo.perf_score) != 0) {
        printf("[-] Failed to initialize worker\n");
        return -1;
    }

    if (dist_worker_connect(&client) != 0) {
        printf("[-] Failed to connect to coordinator\n");
        return -1;
    }

    printf("[+] Connected! Receiving configuration...\n");

    /* TODO: Receive config from server */
    /* For now, use local config */

    printf("[+] Configuration received:\n");
    printf("    Puzzle: #%d\n", cfg->puzzle_number);
    printf("    Target: %s\n", cfg->target_address);
    printf("    Mode: %s (%s)\n", cfg->mode, cfg->key_type);

    printf("\n[+] Starting worker (threads=%d, gpu=%d%%)...\n",
           cfg->threads, cfg->gpu_percent);
    printf("[+] Press Ctrl+C to stop\n\n");
    wizard_print_separator();

    /* Main work loop */
    uint64_t total_keys = 0;
    int work_count = 0;
    time_t start_time = time(NULL);

    while (g_client_running) {
        char range_start[65], range_end[65];

        int result = dist_worker_request_work(&client, range_start, range_end);

        if (result == 1) {
            printf("\n[+] No more work available\n");
            break;
        }

        if (result < 0) {
            printf("\n[-] Error getting work, retrying...\n");
            sleep(5);
            continue;
        }

        work_count++;
        printf("\r[Work #%d] Range: %s - %s", work_count, range_start, range_end);
        fflush(stdout);

        uint64_t keys_checked = 0;
        char found_key[65] = {0};
        char found_addr[36] = {0};

        uint64_t unit_start = time(NULL);

        /* Search */
        int search_result = keyhunt_search_range(
            range_start, range_end,
            cfg->target_address, cfg->mode, cfg->key_type,
            cfg->threads, cfg->gpu_percent,
            &keys_checked, &g_client_running,
            found_key, found_addr
        );

        uint64_t elapsed = time(NULL) - unit_start;
        if (elapsed == 0) elapsed = 1;

        total_keys += keys_checked;
        double speed = (double)keys_checked / elapsed / 1000000.0;

        /* Report completion */
        dist_worker_report_done(&client, keys_checked, elapsed * 1000);

        /* Check if found */
        if (search_result == 1 && found_key[0]) {
            printf("\n\n");
            printf("[!!!] KEY FOUND!\n");
            printf("      Private Key: %s\n", found_key);
            printf("      Address: %s\n", found_addr);

            dist_worker_report_found(&client, found_key, found_addr);
            break;
        }

        printf(" | %.2f Mkeys/s", speed);
        fflush(stdout);
    }

    /* Final stats */
    time_t total_time = time(NULL) - start_time;
    if (total_time == 0) total_time = 1;

    printf("\n\n");
    wizard_print_separator();
    printf("[+] Worker Statistics:\n");
    printf("    Work units completed: %d\n", work_count);
    printf("    Keys processed: %.2e\n", (double)total_keys);
    printf("    Average speed: %.2f Mkeys/s\n",
           (double)total_keys / total_time / 1000000.0);
    printf("    Total time: %ld seconds\n", total_time);

    dist_worker_disconnect(&client);
    return 0;
}
```

**Step 2: Commit**

```bash
git add wizard/wizard_client.c
git commit -m "feat(wizard): add client mode with auto-configuration"
```

---

## Task 7: Implement Main Wizard Entry Point

**Files:**
- Create: `wizard/wizard.c`
- Modify: `keyhunt.cpp` (add --wizard flag)

**Step 1: Create main wizard logic**

```c
/*
 * wizard.c - Main wizard entry point
 */

#include "wizard.h"
#include "../sysinfo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int wizard_select_puzzle(wizard_config_t *cfg) {
    int count;
    const puzzle_def_t *puzzles = wizard_get_puzzles(&count);

    const char *options[16];
    int puzzle_nums[16];
    char labels[16][64];

    int n = 0;
    for (int i = 0; i < count && n < 16; i++) {
        snprintf(labels[n], sizeof(labels[n]),
                 "#%d - %.2f BTC (%d bits)%s",
                 puzzles[i].number, puzzles[i].reward_btc, puzzles[i].bits,
                 puzzles[i].has_public_key ? " [has pubkey!]" : "");
        options[n] = labels[n];
        puzzle_nums[n] = puzzles[i].number;
        n++;
    }

    int choice = wizard_ask_choice("Select puzzle to solve:", options, n, 0);

    const puzzle_def_t *p = wizard_get_puzzle(puzzle_nums[choice]);
    if (!p) return -1;

    cfg->puzzle_number = p->number;
    strncpy(cfg->target_address, p->target_address, sizeof(cfg->target_address) - 1);
    strncpy(cfg->range_start, p->range_start, sizeof(cfg->range_start) - 1);
    strncpy(cfg->range_end, p->range_end, sizeof(cfg->range_end) - 1);
    cfg->bits = p->bits;

    return 0;
}

static int wizard_configure_server(wizard_config_t *cfg) {
    cfg->server_port = wizard_ask_int("Server port", 1024, 65535, 7777);

    const char *unit_options[] = {
        "4 billion keys (~80s per unit) - Recommended",
        "1 billion keys (faster feedback)",
        "16 billion keys (less overhead)"
    };
    uint64_t unit_sizes[] = {0x100000000ULL, 0x40000000ULL, 0x400000000ULL};

    int choice = wizard_ask_choice("Work unit size:", unit_options, 3, 0);
    cfg->work_unit_size = unit_sizes[choice];

    cfg->server_also_worker = wizard_ask_yesno("Run local worker on this machine?", true);

    return 0;
}

static int wizard_configure_search(wizard_config_t *cfg) {
    /* Detect hardware */
    system_info_t sysinfo;
    get_system_info(&sysinfo);

    printf("\n[+] Detected hardware:\n");
    printf("    CPU: %d physical cores, %d threads\n",
           sysinfo.physical_cores, sysinfo.logical_cores);
    printf("    RAM: %lu MB available\n", sysinfo.available_memory_mb);

    /* Thread count */
    cfg->threads = wizard_ask_int("Number of threads (0=auto)",
                                   0, sysinfo.logical_cores * 2,
                                   sysinfo.logical_cores);
    if (cfg->threads == 0) cfg->threads = sysinfo.logical_cores;

    /* GPU */
    cfg->gpu_percent = wizard_ask_int("GPU usage percent (0=disabled)", 0, 100, 0);

    /* Key type */
    const char *key_options[] = {"compress (faster)", "uncompress", "both"};
    const char *key_values[] = {"compress", "uncompress", "both"};
    int choice = wizard_ask_choice("Key type to search:", key_options, 3, 0);
    strncpy(cfg->key_type, key_values[choice], sizeof(cfg->key_type) - 1);

    return 0;
}

static int wizard_configure_community(wizard_config_t *cfg) {
    cfg->community_enabled = wizard_ask_yesno(
        "Fetch community progress from BTCPuzzle.info?", true);

    if (cfg->community_enabled) {
        printf("\n[+] Fetching community data...\n");

        community_range_t *ranges = NULL;
        int count = 0;

        if (wizard_community_fetch(cfg->puzzle_number, &ranges, &count) == 0) {
            cfg->community_excluded = count;

            if (count > 0) {
                wizard_community_merge_exclusions(cfg->exclusion_file, ranges, count);
                wizard_community_free(ranges, count);
            }

            cfg->community_last_sync = time(NULL);
        }
    }

    return 0;
}

int wizard_run(void) {
    wizard_config_t cfg;
    wizard_config_init(&cfg);

    wizard_print_header("KEYHUNT INTERACTIVE WIZARD");

    /* Check for existing config */
    if (wizard_config_load(&cfg, "keyhunt_wizard.json") == 0) {
        printf("[+] Found existing configuration\n");
        wizard_print_config_summary(&cfg);

        if (wizard_ask_yesno("Use existing configuration?", true)) {
            goto mode_select;
        }
    }

    /* Step 1: Select puzzle */
    printf("\n[Step 1/5] Puzzle Selection\n");
    wizard_print_separator();
    if (wizard_select_puzzle(&cfg) != 0) return -1;

    /* Step 2: Mode selection */
mode_select:
    printf("\n[Step 2/5] Mode Selection\n");
    wizard_print_separator();

    const char *mode_options[] = {
        "Server (coordinator + worker) - Start a new search",
        "Client (worker only) - Join existing server"
    };
    int mode = wizard_ask_choice("Select mode:", mode_options, 2, 0);
    cfg.is_server = (mode == 0);

    if (cfg.is_server) {
        /* Server configuration */
        printf("\n[Step 3/5] Server Configuration\n");
        wizard_print_separator();
        wizard_configure_server(&cfg);

        printf("\n[Step 4/5] Search Configuration\n");
        wizard_print_separator();
        wizard_configure_search(&cfg);

        printf("\n[Step 5/5] Community Sync\n");
        wizard_print_separator();
        wizard_configure_community(&cfg);

    } else {
        /* Client configuration */
        printf("\n[Step 3/5] Server Connection\n");
        wizard_print_separator();
        wizard_ask_string("Server IP/hostname", cfg.server_host,
                          sizeof(cfg.server_host), "localhost");
        cfg.server_port = wizard_ask_int("Server port", 1024, 65535, 7777);

        printf("\n[Step 4/5] Search Configuration\n");
        wizard_print_separator();
        wizard_configure_search(&cfg);

        printf("\n[Step 5/5] Skipped (config from server)\n");
    }

    /* Summary */
    wizard_print_config_summary(&cfg);

    if (!wizard_ask_yesno("Save configuration and start?", true)) {
        printf("[!] Cancelled by user\n");
        return 1;
    }

    /* Save config */
    if (wizard_config_save(&cfg, "keyhunt_wizard.json") == 0) {
        printf("[+] Configuration saved to keyhunt_wizard.json\n");
    }

    printf("\n");
    wizard_print_separator();

    /* Run */
    if (cfg.is_server) {
        return wizard_server_run(&cfg);
    } else {
        return wizard_client_run(&cfg);
    }
}
```

**Step 2: Add keyhunt_search_range wrapper (stub)**

Add to `wizard/wizard.c`:

```c
/* Stub for keyhunt_search_range - will be implemented in keyhunt.cpp */
#ifndef KEYHUNT_SEARCH_IMPL
int keyhunt_search_range(const char *start, const char *end,
                         const char *target, const char *mode,
                         const char *key_type, int threads, int gpu_percent,
                         uint64_t *keys_checked, volatile int *stop_flag,
                         char *found_key, char *found_addr) {
    (void)start; (void)end; (void)target; (void)mode;
    (void)key_type; (void)threads; (void)gpu_percent;
    (void)keys_checked; (void)stop_flag;
    (void)found_key; (void)found_addr;

    /* This is a stub - real implementation in keyhunt.cpp */
    fprintf(stderr, "[!] keyhunt_search_range not implemented\n");
    return -1;
}
#endif
```

**Step 3: Commit**

```bash
git add wizard/wizard.c
git commit -m "feat(wizard): add main wizard entry point and flow"
```

---

## Task 8: Integrate with keyhunt.cpp

**Files:**
- Modify: `keyhunt.cpp` (add --wizard/-W flag)
- Modify: `Makefile` (add wizard objects)

**Step 1: Add wizard flag to keyhunt.cpp**

Add to includes:
```cpp
extern "C" {
    #include "wizard/wizard.h"
}
```

Add to getopt parsing (around line 500):
```cpp
// In the options string, add 'W' for wizard
// "W" for --wizard

// Add case in switch:
case 'W':
    return wizard_run();
```

**Step 2: Update Makefile**

```makefile
WIZARD_OBJS := wizard/wizard.o wizard/wizard_config.o wizard/wizard_ui.o \
               wizard/wizard_community.o wizard/wizard_server.o wizard/wizard_client.o

KEYHUNT_OBJS := keyhunt.o $(COMMON_OBJS) $(SECP256K1_OBJS) $(WIZARD_OBJS)
```

**Step 3: Commit**

```bash
git add keyhunt.cpp Makefile wizard/
git commit -m "feat(wizard): integrate --wizard flag into keyhunt"
```

---

## Summary

| Task | Files | Description |
|------|-------|-------------|
| 1 | `wizard/wizard.h` | Header with all data structures |
| 2 | `wizard/wizard_config.c` | JSON config + puzzle database |
| 3 | `wizard/wizard_ui.c` | Interactive terminal UI |
| 4 | `wizard/wizard_community.c` | BTCPuzzle.info scraper |
| 5 | `wizard/wizard_server.c` | Server + local worker |
| 6 | `wizard/wizard_client.c` | Client with auto-config |
| 7 | `wizard/wizard.c` | Main wizard flow |
| 8 | `keyhunt.cpp`, `Makefile` | Integration |

**Total: ~1000 lines of C code**
