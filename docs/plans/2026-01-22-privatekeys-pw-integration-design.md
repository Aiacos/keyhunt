# Privatekeys.pw Cloud Search Integration Design

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Fetch community scanning progress from privatekeys.pw and use it to avoid re-scanning already searched ranges.

**Architecture:** Percentage-based estimation with daily caching. Sequential mode skips scanned portion; random mode excludes scanned regions from selection.

**Tech Stack:** C, curl for HTTP, JSON for cache storage

---

## 1. Data Structures

```c
/* In wizard.h */

#define PRIVATEKEYS_CACHE_FILE "~/.keyhunt/privatekeys_progress.json"
#define PRIVATEKEYS_REFRESH_INTERVAL (24 * 60 * 60)  /* 24 hours */

typedef struct {
    int puzzle_number;
    double percent_scanned;      /* e.g., 0.022477 */
    uint64_t keys_scanned;       /* Absolute count if available */
    time_t fetch_time;
} privatekeys_progress_t;
```

## 2. API Functions

```c
/* Fetch fresh progress from privatekeys.pw (scrapes HTML) */
int wizard_privatekeys_fetch_progress(int puzzle_number, privatekeys_progress_t *progress);

/* Get progress with 24h caching logic */
int wizard_privatekeys_get_progress(int puzzle_number, privatekeys_progress_t *progress);

/* Calculate starting offset for sequential mode */
void wizard_calculate_search_offset(const puzzle_def_t *puzzle,
                                    double percent_scanned,
                                    char *adjusted_start);

/* Generate random range excluding scanned regions */
bool wizard_generate_random_range(wizard_config_t *cfg,
                                  char *range_start,
                                  char *range_end);

/* Save locally scanned range to progress file */
int wizard_save_local_progress(const char *filepath,
                               const char *range_start,
                               const char *range_end);
```

## 3. Sequential vs Random Mode Logic

### Sequential Mode (random_mode = false)
```
[====SCANNED====][────────── SEARCH HERE ──────────────]
0%          0.02%                                    100%
                ↑
        Start from offset
```

- Calculate offset = range_size * (percent_scanned / 100)
- Set range_start = original_start + offset
- Search continues linearly from there

### Random Mode (random_mode = true)
```
[XXXX][    ][XXX][      ][XX][        ][XXXX][    ][   ]
  ↑           ↑           ↑              ↑
excluded   excluded    excluded       excluded
```

- Generate random range within puzzle bounds
- Reject if in community-scanned region (first X%)
- Reject if in local exclusion file (our own progress)
- Repeat until valid range found

## 4. Caching Strategy

Cache file: `~/.keyhunt/privatekeys_progress.json`
```json
{
    "puzzle_number": 71,
    "percent_scanned": 0.022477,
    "keys_scanned": 265371063478951808,
    "fetch_time": 1737561600
}
```

Refresh logic:
1. Load cache → check age
2. If < 24h old → use cached data
3. If >= 24h old → fetch fresh, update cache
4. If fetch fails → use stale cache with warning
5. If no cache exists → search full range with warning

## 5. Local Progress Tracking

File: `~/.keyhunt/puzzle_71_progress.dat`
```
# Ranges scanned locally (hex start:end)
4000000000000000:4000000100000000
4000000100000000:4000000200000000
4000000500000000:4000000600000000
```

Updated after each work unit completes. Used for:
- Random mode exclusion
- Resume after restart
- Prevent duplicate local work

## 6. Integration Flow

```
Wizard Startup
     │
     ├─► Load config (keyhunt_wizard.json)
     │
     ├─► Community Sync Enabled?
     │        │
     │        ├─► YES:
     │        │    ├─► Fetch BTCPuzzle.info ranges (existing)
     │        │    └─► Fetch privatekeys.pw progress (NEW)
     │        │              │
     │        │              ├─► random_mode?
     │        │              │      YES → store for exclusion
     │        │              │      NO  → calculate offset, adjust range_start
     │        │              │
     │        │              └─► Load local progress file
     │        │
     │        └─► NO: Use full range
     │
     └─► Start Server/Client mode
```

## 7. UI Display

```
[+] Community Sync:
    BTCPuzzle.info: 1,234 ranges excluded
    privatekeys.pw: 0.0225% scanned (updated 2h ago)
    Local progress: 847 ranges completed

[+] Search Strategy: RANDOM MODE
    → Excluding first 0.0225% (community scanned)
    → Excluding 847 local ranges

[+] Search Strategy: SEQUENTIAL MODE
    → Starting from offset: 0x400005A3B8C7E0000
    → Skipping 0.0225% already scanned
```

## 8. Error Handling

| Scenario | Action |
|----------|--------|
| Network error on fetch | Use stale cache if available |
| No cache, fetch fails | Search full range, warn user |
| Invalid percentage parsed | Ignore, use 0% |
| Cache file corrupted | Delete and re-fetch |
| Local progress file missing | Create empty, start fresh |

## 9. Implementation Tasks

1. Add `privatekeys_progress_t` struct to wizard.h
2. Implement `wizard_privatekeys_fetch_progress()` - HTML scraper
3. Implement cache load/save functions
4. Implement `wizard_privatekeys_get_progress()` with 24h refresh
5. Implement `wizard_calculate_search_offset()` for sequential mode
6. Implement `wizard_generate_random_range()` for random mode
7. Implement `wizard_save_local_progress()` for tracking our work
8. Integrate into wizard startup flow
9. Update UI to show combined progress
10. Test with both sequential and random modes

## 10. Files to Modify

- `wizard/wizard.h` - Add new structs and function declarations
- `wizard/wizard_community.c` - Add privatekeys.pw functions
- `wizard/wizard.c` - Integrate into startup flow
- `wizard/wizard_server.c` - Save local progress after each unit
- `wizard/wizard_client.c` - Save local progress after each unit
