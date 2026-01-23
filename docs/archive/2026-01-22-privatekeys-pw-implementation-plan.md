# Privatekeys.pw Cloud Search Integration - Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Fetch community scanning progress from privatekeys.pw with 24h caching and use it to skip already-scanned ranges.

**Architecture:** HTML scraper extracts percentage from privatekeys.pw, caches locally in JSON, sequential mode calculates offset, random mode excludes scanned region.

**Tech Stack:** C, curl (via popen), JSON manual parsing, 128-bit integer arithmetic

---

## Task 1: Add Data Structures to wizard.h

**Files:**
- Modify: `wizard/wizard.h:25-28` (add constants)
- Modify: `wizard/wizard.h:97-98` (add struct after community_range_t)

**Step 1: Add constants after existing limits**

In `wizard/wizard.h`, after line 27 (`#define WIZARD_MAX_EXCLUSIONS 1000000`), add:

```c
/* Privatekeys.pw cloud search integration */
#define PRIVATEKEYS_CLOUD_URL "https://privatekeys.pw/cloud-search"
#define PRIVATEKEYS_CACHE_DIR ".keyhunt"
#define PRIVATEKEYS_CACHE_FILE "privatekeys_progress.json"
#define PRIVATEKEYS_REFRESH_INTERVAL (24 * 60 * 60)  /* 24 hours */
```

**Step 2: Add progress struct after community_range_t**

After line 97 (closing brace of `community_range_t`), add:

```c
/* Privatekeys.pw progress data */
typedef struct {
    int puzzle_number;
    double percent_scanned;      /* e.g., 0.022477 */
    uint64_t keys_scanned;       /* Absolute count if available */
    time_t fetch_time;           /* When data was fetched */
} privatekeys_progress_t;
```

**Step 3: Add new field to wizard_config_t**

In `wizard_config_t`, after line 81 (`uint64_t community_excluded;`), add:

```c
    double privatekeys_percent;      /* Progress from privatekeys.pw */
```

**Step 4: Verify build**

Run: `make -j$(nproc) 2>&1 | grep -i error`
Expected: No errors

**Step 5: Commit**

```bash
git add wizard/wizard.h
git commit -m "feat(wizard): add privatekeys.pw data structures"
```

---

## Task 2: Add Function Declarations to wizard.h

**Files:**
- Modify: `wizard/wizard.h:193-198` (add after wizard_is_range_excluded)

**Step 1: Add function declarations**

After `wizard_is_range_excluded` declaration (around line 198), add:

```c
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
```

**Step 2: Verify build**

Run: `make -j$(nproc) 2>&1 | grep -i error`
Expected: No errors (declarations only, no implementation yet)

**Step 3: Commit**

```bash
git add wizard/wizard.h
git commit -m "feat(wizard): add privatekeys.pw function declarations"
```

---

## Task 3: Implement HTML Scraper

**Files:**
- Modify: `wizard/wizard_community.c` (add after existing functions, before EOF)

**Step 1: Add URL constant at top of file**

After line 13 (`#define PRIVATEKEYS_URL`), update to:

```c
#define PRIVATEKEYS_CLOUD_URL "https://privatekeys.pw/cloud-search"
```

**Step 2: Implement fetch function**

Add at end of file (before final closing if any):

```c
/* ============================================================================
 * Privatekeys.pw Cloud Search Integration
 * ============================================================================ */

int wizard_privatekeys_fetch_progress(int puzzle_number, privatekeys_progress_t *progress) {
    if (!progress) return -1;

    memset(progress, 0, sizeof(*progress));
    progress->puzzle_number = puzzle_number;

    printf("[+] Fetching privatekeys.pw cloud search progress...\n");

    char *html = NULL;
    size_t html_len = 0;

    if (fetch_url(PRIVATEKEYS_CLOUD_URL, &html, &html_len) != 0) {
        printf("[-] Failed to fetch privatekeys.pw\n");
        return -1;
    }

    if (html_len < 1000) {
        printf("[-] Invalid response from privatekeys.pw\n");
        free(html);
        return -1;
    }

    /*
     * Parse percentage from HTML
     * Looking for patterns like:
     * - "0.022477%"
     * - "Completion percentage" followed by number
     * - "Keys scanned (total):" followed by number
     */

    double percent = 0.0;
    uint64_t keys_scanned = 0;

    /* Method 1: Look for completion percentage pattern */
    char *pct_ptr = strstr(html, "completion");
    if (!pct_ptr) pct_ptr = strstr(html, "Completion");
    if (!pct_ptr) pct_ptr = strstr(html, "progress");

    if (pct_ptr) {
        /* Scan forward for a percentage value (X.XXXXX%) */
        char *scan = pct_ptr;
        char *end = pct_ptr + 500;  /* Search within 500 chars */
        if (end > html + html_len) end = html + html_len;

        while (scan < end) {
            /* Look for pattern: digit(s).digit(s)% */
            if ((*scan >= '0' && *scan <= '9') || *scan == '.') {
                double val = 0.0;
                int consumed = 0;
                if (sscanf(scan, "%lf%n", &val, &consumed) == 1) {
                    /* Check if followed by % */
                    if (scan[consumed] == '%' && val < 100.0 && val >= 0.0) {
                        percent = val;
                        break;
                    }
                }
            }
            scan++;
        }
    }

    /* Method 2: Look for keys scanned count */
    char *keys_ptr = strstr(html, "keys scanned");
    if (!keys_ptr) keys_ptr = strstr(html, "Keys scanned");
    if (!keys_ptr) keys_ptr = strstr(html, "total keys");

    if (keys_ptr) {
        char *scan = keys_ptr;
        char *end = keys_ptr + 200;
        if (end > html + html_len) end = html + html_len;

        while (scan < end) {
            if (*scan >= '0' && *scan <= '9') {
                /* Try to parse large number (may have commas) */
                uint64_t val = 0;
                while (scan < end && (*scan >= '0' && *scan <= '9')) {
                    val = val * 10 + (*scan - '0');
                    scan++;
                    /* Skip commas in numbers like 265,371,063 */
                    if (*scan == ',' || *scan == ' ' || *scan == '.') {
                        if (scan[1] >= '0' && scan[1] <= '9') {
                            scan++;
                        }
                    }
                }
                if (val > 1000000) {  /* Sanity check: at least 1M keys */
                    keys_scanned = val;
                    break;
                }
            }
            scan++;
        }
    }

    free(html);

    /* Validate we got something useful */
    if (percent <= 0.0 && keys_scanned == 0) {
        printf("[-] Could not parse progress data from privatekeys.pw\n");
        return -1;
    }

    progress->percent_scanned = percent;
    progress->keys_scanned = keys_scanned;
    progress->fetch_time = time(NULL);

    printf("[+] Parsed: %.6f%% scanned", percent);
    if (keys_scanned > 0) {
        printf(" (%llu keys)", (unsigned long long)keys_scanned);
    }
    printf("\n");

    return 0;
}
```

**Step 3: Verify build**

Run: `make -j$(nproc) 2>&1 | grep -i error`
Expected: No errors

**Step 4: Quick manual test**

Run: Create a small test in scratchpad to verify parsing works.

**Step 5: Commit**

```bash
git add wizard/wizard_community.c
git commit -m "feat(wizard): implement privatekeys.pw HTML scraper"
```

---

## Task 4: Implement Cache Load/Save

**Files:**
- Modify: `wizard/wizard_community.c` (add after fetch function)

**Step 1: Add cache helper functions**

```c
/* Get cache directory path (creates if needed) */
static int get_cache_dir(char *path, size_t size) {
    const char *home = getenv("HOME");
    if (!home) home = ".";

    snprintf(path, size, "%s/.keyhunt", home);

    /* Create directory if it doesn't exist */
    struct stat st;
    if (stat(path, &st) != 0) {
        if (mkdir(path, 0755) != 0) {
            return -1;
        }
    }
    return 0;
}

/* Get cache file path */
static void get_cache_filepath(char *path, size_t size) {
    char dir[512];
    if (get_cache_dir(dir, sizeof(dir)) != 0) {
        snprintf(path, size, ".keyhunt_privatekeys_cache.json");
        return;
    }
    snprintf(path, size, "%s/privatekeys_progress.json", dir);
}

/* Save progress to cache file */
static int save_privatekeys_cache(const privatekeys_progress_t *progress) {
    char filepath[512];
    get_cache_filepath(filepath, sizeof(filepath));

    FILE *f = fopen(filepath, "w");
    if (!f) return -1;

    fprintf(f, "{\n");
    fprintf(f, "  \"puzzle_number\": %d,\n", progress->puzzle_number);
    fprintf(f, "  \"percent_scanned\": %.8f,\n", progress->percent_scanned);
    fprintf(f, "  \"keys_scanned\": %llu,\n", (unsigned long long)progress->keys_scanned);
    fprintf(f, "  \"fetch_time\": %lld\n", (long long)progress->fetch_time);
    fprintf(f, "}\n");

    fclose(f);
    return 0;
}

/* Load progress from cache file */
static int load_privatekeys_cache(privatekeys_progress_t *progress) {
    char filepath[512];
    get_cache_filepath(filepath, sizeof(filepath));

    FILE *f = fopen(filepath, "r");
    if (!f) return -1;

    char buf[1024];
    size_t len = fread(buf, 1, sizeof(buf) - 1, f);
    buf[len] = '\0';
    fclose(f);

    /* Simple JSON parsing */
    memset(progress, 0, sizeof(*progress));

    char *ptr;

    ptr = strstr(buf, "\"puzzle_number\":");
    if (ptr) progress->puzzle_number = atoi(ptr + 16);

    ptr = strstr(buf, "\"percent_scanned\":");
    if (ptr) progress->percent_scanned = atof(ptr + 18);

    ptr = strstr(buf, "\"keys_scanned\":");
    if (ptr) progress->keys_scanned = strtoull(ptr + 15, NULL, 10);

    ptr = strstr(buf, "\"fetch_time\":");
    if (ptr) progress->fetch_time = (time_t)strtoll(ptr + 13, NULL, 10);

    /* Validate */
    if (progress->fetch_time == 0) return -1;

    return 0;
}
```

**Step 2: Add include for mkdir**

At top of file, add:

```c
#include <sys/stat.h>
```

**Step 3: Verify build**

Run: `make -j$(nproc) 2>&1 | grep -i error`
Expected: No errors

**Step 4: Commit**

```bash
git add wizard/wizard_community.c
git commit -m "feat(wizard): add privatekeys.pw cache load/save"
```

---

## Task 5: Implement Caching Logic

**Files:**
- Modify: `wizard/wizard_community.c` (add after cache functions)

**Step 1: Implement main getter with caching**

```c
int wizard_privatekeys_get_progress(int puzzle_number, privatekeys_progress_t *progress) {
    if (!progress) return -1;

    /* Try to load from cache first */
    int cache_result = load_privatekeys_cache(progress);
    bool have_cache = (cache_result == 0 && progress->puzzle_number == puzzle_number);

    /* Check if cache is still fresh (< 24 hours) */
    time_t now = time(NULL);
    bool needs_refresh = !have_cache ||
                         (now - progress->fetch_time >= PRIVATEKEYS_REFRESH_INTERVAL);

    if (!needs_refresh) {
        /* Cache is fresh, use it */
        double age_hours = (now - progress->fetch_time) / 3600.0;
        printf("[+] Using cached privatekeys.pw data (%.1f hours old)\n", age_hours);
        return 0;
    }

    /* Need to fetch fresh data */
    printf("[+] Refreshing privatekeys.pw progress (daily update)...\n");

    privatekeys_progress_t fresh;
    if (wizard_privatekeys_fetch_progress(puzzle_number, &fresh) == 0) {
        /* Save to cache */
        save_privatekeys_cache(&fresh);
        *progress = fresh;
        return 0;
    }

    /* Fetch failed - try to use stale cache */
    if (have_cache) {
        double age_hours = (now - progress->fetch_time) / 3600.0;
        printf("[!] Fetch failed, using stale cache (%.1f hours old)\n", age_hours);
        return 0;
    }

    /* No cache available */
    printf("[!] No community progress data available\n");
    memset(progress, 0, sizeof(*progress));
    return -1;
}
```

**Step 2: Verify build**

Run: `make -j$(nproc) 2>&1 | grep -i error`
Expected: No errors

**Step 3: Commit**

```bash
git add wizard/wizard_community.c
git commit -m "feat(wizard): implement privatekeys.pw 24h caching logic"
```

---

## Task 6: Implement Offset Calculation (Sequential Mode)

**Files:**
- Modify: `wizard/wizard_community.c` (add after caching functions)

**Step 1: Add 128-bit integer helpers**

```c
/* Parse hex string to 128-bit integer */
static __uint128_t parse_hex_128(const char *hex) {
    __uint128_t result = 0;
    const char *p = hex;

    /* Skip 0x prefix if present */
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
    }

    while (*p) {
        char c = *p++;
        int digit;

        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else break;

        result = (result << 4) | digit;
    }

    return result;
}

/* Convert 128-bit integer to hex string */
static void uint128_to_hex(__uint128_t value, char *out) {
    if (value == 0) {
        strcpy(out, "0");
        return;
    }

    char buf[33];
    int i = 32;
    buf[i] = '\0';

    while (value > 0 && i > 0) {
        int digit = value & 0xF;
        buf[--i] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
        value >>= 4;
    }

    strcpy(out, &buf[i]);
}
```

**Step 2: Implement offset calculation**

```c
void wizard_calculate_search_offset(const puzzle_def_t *puzzle,
                                    double percent_scanned,
                                    char *adjusted_start) {
    if (!puzzle || !adjusted_start || percent_scanned <= 0.0) {
        if (puzzle && adjusted_start) {
            strcpy(adjusted_start, puzzle->range_start);
        }
        return;
    }

    __uint128_t range_start = parse_hex_128(puzzle->range_start);
    __uint128_t range_end = parse_hex_128(puzzle->range_end);
    __uint128_t range_size = range_end - range_start + 1;

    /* Calculate offset: range_size * (percent / 100) */
    /* Use floating point for the multiplication to avoid overflow */
    double offset_d = (double)range_size * (percent_scanned / 100.0);
    __uint128_t offset = (__uint128_t)offset_d;

    /* Calculate adjusted start */
    __uint128_t adjusted = range_start + offset;

    /* Ensure we don't exceed range_end */
    if (adjusted > range_end) {
        adjusted = range_end;
    }

    uint128_to_hex(adjusted, adjusted_start);
}
```

**Step 3: Implement scanned region check (for random mode)**

```c
bool wizard_is_in_scanned_region(const puzzle_def_t *puzzle,
                                 const char *range_start,
                                 double percent_scanned) {
    if (!puzzle || !range_start || percent_scanned <= 0.0) {
        return false;
    }

    __uint128_t puzzle_start = parse_hex_128(puzzle->range_start);
    __uint128_t puzzle_end = parse_hex_128(puzzle->range_end);
    __uint128_t range_size = puzzle_end - puzzle_start + 1;

    /* Calculate scanned boundary */
    double boundary_d = (double)puzzle_start + (double)range_size * (percent_scanned / 100.0);
    __uint128_t scanned_boundary = (__uint128_t)boundary_d;

    /* Check if range_start is below the scanned boundary */
    __uint128_t check_pos = parse_hex_128(range_start);

    return (check_pos < scanned_boundary);
}
```

**Step 4: Verify build**

Run: `make -j$(nproc) 2>&1 | grep -i error`
Expected: No errors

**Step 5: Commit**

```bash
git add wizard/wizard_community.c
git commit -m "feat(wizard): implement search offset calculation for sequential mode"
```

---

## Task 7: Implement Local Progress Tracking

**Files:**
- Modify: `wizard/wizard_community.c` (add after offset functions)

**Step 1: Implement save local progress**

```c
int wizard_save_local_progress(int puzzle_number,
                               const char *range_start,
                               const char *range_end) {
    if (!range_start || !range_end) return -1;

    char dir[512];
    if (get_cache_dir(dir, sizeof(dir)) != 0) {
        return -1;
    }

    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/puzzle_%d_progress.dat", dir, puzzle_number);

    /* Append to file */
    FILE *f = fopen(filepath, "a");
    if (!f) return -1;

    fprintf(f, "%s:%s\n", range_start, range_end);
    fclose(f);

    return 0;
}

int wizard_load_local_progress_count(int puzzle_number) {
    char dir[512];
    if (get_cache_dir(dir, sizeof(dir)) != 0) {
        return 0;
    }

    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/puzzle_%d_progress.dat", dir, puzzle_number);

    FILE *f = fopen(filepath, "r");
    if (!f) return 0;

    int count = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] != '#' && line[0] != '\n' && strchr(line, ':')) {
            count++;
        }
    }

    fclose(f);
    return count;
}
```

**Step 2: Verify build**

Run: `make -j$(nproc) 2>&1 | grep -i error`
Expected: No errors

**Step 3: Commit**

```bash
git add wizard/wizard_community.c
git commit -m "feat(wizard): implement local progress tracking"
```

---

## Task 8: Integrate into Wizard Startup Flow

**Files:**
- Modify: `wizard/wizard.c:359-392` (wizard_configure_community function)

**Step 1: Update community configuration function**

Replace the `wizard_configure_community` function with:

```c
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

    return 0;
}
```

**Step 2: Verify build**

Run: `make -j$(nproc) 2>&1 | grep -i error`
Expected: No errors

**Step 3: Commit**

```bash
git add wizard/wizard.c
git commit -m "feat(wizard): integrate privatekeys.pw into startup flow"
```

---

## Task 9: Save Progress After Work Unit Completion

**Files:**
- Modify: `wizard/wizard_server.c` (in local_worker_thread, after work completion)
- Modify: `wizard/wizard_client.c` (after dist_worker_report_done)

**Step 1: Update wizard_server.c**

In `local_worker_thread` function, after line ~198 (`unit->status = WORK_STATUS_COMPLETED;`), add:

```c
        /* Save local progress */
        wizard_save_local_progress(cfg->puzzle_number, unit->range_start, unit->range_end);
```

**Step 2: Update wizard_client.c**

After `dist_worker_report_done` call (around line 242), add:

```c
        /* Save local progress */
        wizard_save_local_progress(cfg->puzzle_number, range_start, range_end);
```

**Step 3: Verify build**

Run: `make -j$(nproc) 2>&1 | grep -i error`
Expected: No errors

**Step 4: Commit**

```bash
git add wizard/wizard_server.c wizard/wizard_client.c
git commit -m "feat(wizard): save local progress after each work unit"
```

---

## Task 10: Update UI Display

**Files:**
- Modify: `wizard/wizard_ui.c` (in wizard_print_config_summary)

**Step 1: Find config summary function and add privatekeys.pw info**

In `wizard_print_config_summary`, after community enabled line, add display for privatekeys percent:

```c
    if (cfg->privatekeys_percent > 0.0) {
        printf("  │ %sCommunity:%s %.4f%% (privatekeys.pw)            │\n",
               CYAN, RESET, cfg->privatekeys_percent);
    }
```

**Step 2: Verify build**

Run: `make -j$(nproc) 2>&1 | grep -i error`
Expected: No errors

**Step 3: Commit**

```bash
git add wizard/wizard_ui.c
git commit -m "feat(wizard): display privatekeys.pw progress in UI"
```

---

## Task 11: Full Integration Test

**Step 1: Build and run wizard**

```bash
make clean && make -j$(nproc)
./keyhunt -W
```

**Step 2: Test community fetch**

- Select puzzle #71
- Verify privatekeys.pw progress is fetched and displayed
- Verify cache file is created: `~/.keyhunt/privatekeys_progress.json`

**Step 3: Test caching**

- Run wizard again within 24h
- Verify cached data is used (no network fetch)

**Step 4: Test sequential mode**

- Configure as server with sequential mode (random_mode = false)
- Verify range_start is adjusted based on community progress

**Step 5: Test random mode**

- Configure with random_mode = true
- Verify first X% is excluded from random selection (check logs)

**Step 6: Test local progress**

- Run a short search
- Verify `~/.keyhunt/puzzle_71_progress.dat` is created and updated

**Step 7: Final commit**

```bash
git add -A
git commit -m "feat(wizard): complete privatekeys.pw cloud search integration

- Fetch community progress from privatekeys.pw
- 24-hour caching to avoid excessive requests
- Sequential mode: skip already-scanned portion
- Random mode: exclude scanned region from selection
- Track local progress for resume capability"
```

---

## Summary

| Task | Description | Files |
|------|-------------|-------|
| 1 | Add data structures | wizard.h |
| 2 | Add function declarations | wizard.h |
| 3 | Implement HTML scraper | wizard_community.c |
| 4 | Implement cache load/save | wizard_community.c |
| 5 | Implement caching logic | wizard_community.c |
| 6 | Implement offset calculation | wizard_community.c |
| 7 | Implement local progress tracking | wizard_community.c |
| 8 | Integrate into startup flow | wizard.c |
| 9 | Save progress after work units | wizard_server.c, wizard_client.c |
| 10 | Update UI display | wizard_ui.c |
| 11 | Full integration test | - |

Total: 11 tasks, ~11 commits
