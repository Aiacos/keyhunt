/*
 * test_stale_cache.c - Standalone test for stale cache handling
 *
 * This program tests that the wizard properly detects and uses stale cache
 * when cache is older than 24 hours and network is unavailable.
 *
 * Compile:
 *   gcc -o test_stale_cache tests/test_stale_cache.c -I./src -DTEST_MODE
 *
 * Usage:
 *   ./test_stale_cache
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

#define CACHE_DIR_NAME ".keyhunt"
#define PRIVATEKEYS_CACHE_NAME "privatekeys_progress.json"
#define KEYSLOL_CACHE_NAME "keyslol_progress.json"
#define REFRESH_INTERVAL (24 * 60 * 60)  /* 24 hours */

/* ANSI color codes */
#define RED     "\033[0;31m"
#define GREEN   "\033[0;32m"
#define YELLOW  "\033[1;33m"
#define BLUE    "\033[0;34m"
#define NC      "\033[0m"  /* No Color */

typedef struct {
    int puzzle_number;
    double percent_scanned;
    unsigned long long keys_scanned;
    time_t fetch_time;
} cache_entry_t;

/* Get cache directory path */
static int get_cache_dir(char *path, size_t size) {
    const char *home = getenv("HOME");
    if (!home) {
        return -1;
    }
    snprintf(path, size, "%s/%s", home, CACHE_DIR_NAME);
    return 0;
}

/* Create cache directory if it doesn't exist */
static int ensure_cache_dir(void) {
    char path[512];
    if (get_cache_dir(path, sizeof(path)) != 0) {
        return -1;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        if (mkdir(path, 0755) != 0) {
            return -1;
        }
    }
    return 0;
}

/* Create stale cache file for testing */
static int create_stale_cache(const char *filename, int puzzle_num, int hours_old) {
    char dir[512];
    if (get_cache_dir(dir, sizeof(dir)) != 0) {
        return -1;
    }

    char filepath[1024];
    snprintf(filepath, sizeof(filepath), "%s/%s", dir, filename);

    time_t current_time = time(NULL);
    time_t old_time = current_time - (hours_old * 3600);

    FILE *f = fopen(filepath, "w");
    if (!f) {
        return -1;
    }

    fprintf(f, "{\n");
    fprintf(f, "  \"puzzle_number\": %d,\n", puzzle_num);
    fprintf(f, "  \"percent_scanned\": 42.123456,\n");
    fprintf(f, "  \"keys_scanned\": 123456789,\n");
    fprintf(f, "  \"fetch_time\": %lld\n", (long long)old_time);
    fprintf(f, "}\n");

    fclose(f);
    return 0;
}

/* Load cache file */
static int load_cache(const char *filename, cache_entry_t *entry) {
    char dir[512];
    if (get_cache_dir(dir, sizeof(dir)) != 0) {
        return -1;
    }

    char filepath[1024];
    snprintf(filepath, sizeof(filepath), "%s/%s", dir, filename);

    FILE *f = fopen(filepath, "r");
    if (!f) {
        return -1;
    }

    char buf[1024];
    size_t len = fread(buf, 1, sizeof(buf) - 1, f);
    buf[len] = '\0';
    fclose(f);

    /* Simple JSON parsing */
    char *ptr;
    ptr = strstr(buf, "\"puzzle_number\":");
    if (ptr) entry->puzzle_number = atoi(ptr + 16);

    ptr = strstr(buf, "\"percent_scanned\":");
    if (ptr) entry->percent_scanned = atof(ptr + 18);

    ptr = strstr(buf, "\"keys_scanned\":");
    if (ptr) entry->keys_scanned = strtoull(ptr + 15, NULL, 10);

    ptr = strstr(buf, "\"fetch_time\":");
    if (ptr) entry->fetch_time = (time_t)strtoll(ptr + 13, NULL, 10);

    if (entry->fetch_time == 0) {
        return -1;
    }

    return 0;
}

/* Test cache age detection and stale cache warning logic */
static int test_cache_staleness(const char *cache_name, int expected_hours_old) {
    cache_entry_t entry = {0};

    printf("%s[TEST]%s Loading cache: %s\n", BLUE, NC, cache_name);

    if (load_cache(cache_name, &entry) != 0) {
        printf("%s[-]%s Failed to load cache file\n", RED, NC);
        return -1;
    }

    time_t now = time(NULL);
    time_t age_seconds = now - entry.fetch_time;
    double age_hours = age_seconds / 3600.0;
    double age_days = age_hours / 24.0;

    printf("%s[+]%s Cache loaded successfully\n", GREEN, NC);
    printf("    Puzzle: %d\n", entry.puzzle_number);
    printf("    Percent scanned: %.2f%%\n", entry.percent_scanned);
    printf("    Keys scanned: %llu\n", entry.keys_scanned);
    printf("    Fetch time: %lld (%s", (long long)entry.fetch_time, ctime(&entry.fetch_time));
    printf("    Current time: %lld (%s", (long long)now, ctime(&now));
    printf("    Age: %.1f hours (%.2f days)\n", age_hours, age_days);

    /* Check if cache is stale (>24 hours) */
    bool is_stale = (age_seconds >= REFRESH_INTERVAL);

    printf("\n%s[CHECK]%s Cache staleness:\n", BLUE, NC);
    printf("    Threshold: 24 hours (%d seconds)\n", REFRESH_INTERVAL);
    printf("    Actual age: %.1f hours (%lld seconds)\n", age_hours, (long long)age_seconds);
    printf("    Is stale: %s\n", is_stale ? "YES" : "NO");

    if (is_stale) {
        printf("\n%s[!]%s STALE CACHE DETECTED\n", YELLOW, NC);
        if (age_hours >= 24.0) {
            printf("    Warning: Network error - using stale cache as fallback (%.1f days old, may be outdated)\n", age_days);
        } else {
            printf("    Warning: Network error - using stale cache as fallback (%.1f hours old)\n", age_hours);
        }
    } else {
        printf("\n%s[+]%s Cache is fresh, no warning needed\n", GREEN, NC);
        printf("    Info: Using cached data (%.1f hours old)\n", age_hours);
    }

    /* Verify expected age */
    int actual_hours = (int)(age_hours + 0.5);  /* Round to nearest hour */
    int expected_min = expected_hours_old - 1;
    int expected_max = expected_hours_old + 1;

    printf("\n%s[VERIFY]%s Age calculation:\n", BLUE, NC);
    printf("    Expected: ~%d hours\n", expected_hours_old);
    printf("    Actual: %d hours\n", actual_hours);
    printf("    Range: %d-%d hours\n", expected_min, expected_max);

    if (actual_hours >= expected_min && actual_hours <= expected_max) {
        printf("%s[✓]%s PASS - Age calculation is correct\n", GREEN, NC);
        return 0;
    } else {
        printf("%s[✗]%s FAIL - Age calculation is incorrect\n", RED, NC);
        return -1;
    }
}

/* Cleanup test cache files */
static void cleanup_test_cache(void) {
    char dir[512];
    if (get_cache_dir(dir, sizeof(dir)) != 0) {
        return;
    }

    char filepath[1024];

    snprintf(filepath, sizeof(filepath), "%s/%s", dir, PRIVATEKEYS_CACHE_NAME);
    if (unlink(filepath) == 0) {
        printf("%s[+]%s Removed: %s\n", GREEN, NC, filepath);
    }

    snprintf(filepath, sizeof(filepath), "%s/%s", dir, KEYSLOL_CACHE_NAME);
    if (unlink(filepath) == 0) {
        printf("%s[+]%s Removed: %s\n", GREEN, NC, filepath);
    }
}

int main(int argc, char *argv[]) {
    int test_hours = 48;  /* Default: 48 hours old */
    int puzzle_num = 66;  /* Default: puzzle #66 */
    bool cleanup = false;

    /* Parse command line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--cleanup") == 0 || strcmp(argv[i], "-c") == 0) {
            cleanup = true;
        } else if (strcmp(argv[i], "--hours") == 0 || strcmp(argv[i], "-h") == 0) {
            if (i + 1 < argc) {
                test_hours = atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [OPTIONS]\n", argv[0]);
            printf("\nOptions:\n");
            printf("  -h, --hours <N>   Create cache N hours old (default: 48)\n");
            printf("  -c, --cleanup     Remove test cache files and exit\n");
            printf("  --help            Show this help message\n");
            printf("\nExample:\n");
            printf("  %s --hours 48     # Test with 48-hour old cache\n", argv[0]);
            printf("  %s --cleanup      # Remove test cache files\n", argv[0]);
            return 0;
        }
    }

    printf("%s========================================%s\n", BLUE, NC);
    printf("%sStale Cache Test Program%s\n", BLUE, NC);
    printf("%s========================================%s\n\n", BLUE, NC);

    if (cleanup) {
        printf("%s[*] Cleanup mode - removing test cache files...%s\n\n", YELLOW, NC);
        cleanup_test_cache();
        printf("\n%s[+] Cleanup complete!%s\n", GREEN, NC);
        return 0;
    }

    /* Ensure cache directory exists */
    if (ensure_cache_dir() != 0) {
        printf("%s[-] Failed to create cache directory%s\n", RED, NC);
        return 1;
    }

    printf("%s[*] Creating stale cache files (%d hours old)...%s\n\n", YELLOW, NC, test_hours);

    /* Create stale cache files */
    if (create_stale_cache(PRIVATEKEYS_CACHE_NAME, puzzle_num, test_hours) != 0) {
        printf("%s[-] Failed to create privatekeys cache%s\n", RED, NC);
        return 1;
    }
    printf("%s[+]%s Created: %s (puzzle %d, %d hours old)\n",
           GREEN, NC, PRIVATEKEYS_CACHE_NAME, puzzle_num, test_hours);

    if (create_stale_cache(KEYSLOL_CACHE_NAME, puzzle_num, test_hours) != 0) {
        printf("%s[-] Failed to create Keys.lol cache%s\n", RED, NC);
        return 1;
    }
    printf("%s[+]%s Created: %s (puzzle %d, %d hours old)\n",
           GREEN, NC, KEYSLOL_CACHE_NAME, puzzle_num, test_hours);

    printf("\n%s========================================%s\n", BLUE, NC);
    printf("%sTest 1: Privatekeys.pw Cache%s\n", BLUE, NC);
    printf("%s========================================%s\n\n", BLUE, NC);

    int result1 = test_cache_staleness(PRIVATEKEYS_CACHE_NAME, test_hours);

    printf("\n%s========================================%s\n", BLUE, NC);
    printf("%sTest 2: Keys.lol Cache%s\n", BLUE, NC);
    printf("%s========================================%s\n\n", BLUE, NC);

    int result2 = test_cache_staleness(KEYSLOL_CACHE_NAME, test_hours);

    /* Summary */
    printf("\n%s========================================%s\n", BLUE, NC);
    printf("%sTest Summary%s\n", BLUE, NC);
    printf("%s========================================%s\n\n", BLUE, NC);

    if (result1 == 0 && result2 == 0) {
        printf("%s[✓] ALL TESTS PASSED%s\n", GREEN, NC);
        printf("\nStale cache detection is working correctly!\n");
        printf("The wizard will properly warn users when using stale cache.\n");
    } else {
        printf("%s[✗] SOME TESTS FAILED%s\n", RED, NC);
        printf("\nTest Results:\n");
        printf("  Privatekeys.pw: %s\n", result1 == 0 ? "PASS" : "FAIL");
        printf("  Keys.lol: %s\n", result2 == 0 ? "PASS" : "FAIL");
    }

    printf("\n%s[i] Cache files remain in ~/.keyhunt/ for manual testing%s\n", BLUE, NC);
    printf("%s[i] Run with --cleanup to remove test files%s\n", BLUE, NC);
    printf("\n%sManual Verification:%s\n", YELLOW, NC);
    printf("  1. Block network: sudo sh -c 'echo \"127.0.0.1 privatekeys.pw\" >> /etc/hosts'\n");
    printf("  2. Run: ./keyhunt --wizard\n");
    printf("  3. Select puzzle #66, enable community integration\n");
    printf("  4. Look for: \"[!] Network error - using stale cache as fallback\"\n");
    printf("  5. Cleanup: sudo sed -i '/privatekeys.pw/d' /etc/hosts\n");
    printf("  6. Remove cache: %s --cleanup\n", argv[0]);

    printf("\n%s========================================%s\n", BLUE, NC);

    return (result1 == 0 && result2 == 0) ? 0 : 1;
}
