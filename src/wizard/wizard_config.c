/*
 * wizard_config.c - Configuration and puzzle database
 *
 * Note: Uses platform abstraction layer (platform.h) for cross-platform
 *       file operations. Directory operations (platform_dir_*) available
 *       for configuration file management.
 */

#include "wizard.h"
#include "../platform/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#if !PLATFORM_WINDOWS
#include <sys/file.h>  /* flock() - POSIX only */
#endif

/* ============================================================================
 * Built-in Puzzle Database (fallback when offline)
 * ============================================================================ */

static puzzle_def_t g_builtin_puzzles[] = {
    /* Unsolved puzzles */
    {71, "1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU",
     "400000000000000000", "7fffffffffffffffff", 71, 7.10, false, "", false, "", ""},
    {72, "1JTK7s9YVYywfm5XUH7RNhHJH1LshCaRFR",
     "800000000000000000", "ffffffffffffffffff", 72, 7.20, false, "", false, "", ""},
    {73, "128z5d7nN7PkCuX5qoA4Ys6pmxUYnEy86k",
     "1000000000000000000", "1fffffffffffffffff", 73, 7.30, false, "", false, "", ""},
    {74, "12jbtzBb54r97TCwW3G1gCFoumpckRAPdY",
     "2000000000000000000", "3fffffffffffffffff", 74, 7.40, false, "", false, "", ""},
    {75, "1MVDYgVaSN6iKKEsbzRUAYFrYJadLYZvvZ",
     "4000000000000000000", "7fffffffffffffffff", 75, 7.50, false, "", false, "", ""},

    /* Puzzles with exposed public keys (much easier!) */
    {135, "16jY7qLJnxb7CHZyqBP8qca9d51gAjyXQN",
     "40000000000000000000000000000000000", "7ffffffffffffffffffffffffffffffffff",
     135, 13.50, true, "02145d2611c823a396ef6712ce0f712f09b9b4f3135e3e0aa3230fb9b6d08d1e16",
     false, "", ""},
    {140, "1NBXpwfruvhxYKLonB8xHsqNnQdgwjxXKT",
     "800000000000000000000000000000000000", "ffffffffffffffffffffffffffffffffffffff",
     140, 14.00, true, "031f6a332d3c5c4f2de2378c012f429cd109ba07d69690c6c701b6bb87860d6640",
     false, "", ""},
    {145, "1Mh3eroyB3gyD9JFPjdfyPxP4h2g9wRe5Z",
     "10000000000000000000000000000000000000", "1ffffffffffffffffffffffffffffffffffff",
     145, 14.50, true, "03afdda497369e219a2c1c369954a930e4d3740968e5e4352475bcffce3140dae5",
     false, "", ""},
    {150, "1L2GM8eE7mJWLdo3HZS6su1832NX2txaac",
     "200000000000000000000000000000000000000", "3fffffffffffffffffffffffffffffffffffff",
     150, 15.00, true, "03137807790ea7dc6e97901c2bc87411f45ed74a5629315c4e4b03a0a102250c49",
     false, "", ""},
    {155, "1rSnXMr63jdCuegJFuidJqWxUPV7AtUf7",
     "4000000000000000000000000000000000000000", "7fffffffffffffffffffffffffffffffffffff",
     155, 15.50, true, "035cd1854cae45391ca4ec428cc7e6c7d9984424b954209a8eea197b9e364c05f6",
     false, "", ""},
    {160, "15JhYXn6Mx3oF4Y7PcTAv2wVVAuCFFQNiP",
     "8000000000000000000000000000000000000000", "fffffffffffffffffffffffffffffffffffffffF",
     160, 16.00, true, "02e0a8b039282faf6fe0fd769cfbc4b6b4cf8758ba68220eac420e32b91ddfa673",
     false, "", ""},

    /* Terminator */
    {0, "", "", "", 0, 0, false, "", false, "", ""}
};

static puzzle_def_t *g_puzzles = NULL;
static int g_puzzle_count = 0;

const puzzle_def_t* wizard_get_builtin_puzzles(int *count) {
    int n = 0;
    while (g_builtin_puzzles[n].number != 0) n++;
    if (count) *count = n;
    return g_builtin_puzzles;
}

const puzzle_def_t* wizard_get_puzzle(int number) {
    /* First check loaded puzzles */
    if (g_puzzles) {
        for (int i = 0; i < g_puzzle_count; i++) {
            if (g_puzzles[i].number == number) return &g_puzzles[i];
        }
    }
    /* Fallback to builtin */
    for (int i = 0; g_builtin_puzzles[i].number != 0; i++) {
        if (g_builtin_puzzles[i].number == number) return &g_builtin_puzzles[i];
    }
    return NULL;
}

/* ============================================================================
 * Configuration Functions
 * ============================================================================ */

void wizard_config_init(wizard_config_t *cfg) {
    memset(cfg, 0, sizeof(wizard_config_t));
    cfg->version = 1;
    cfg->puzzle_number = 71;
    cfg->server_port = 7777;
    cfg->work_unit_size = 0x100000000ULL;  /* 4G keys */
    cfg->checkpoint_interval_sec = 60;  /* Reduced from 300s for better crash recovery */
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
    cfg->webhook_discord_url[0] = '\0';
    cfg->webhook_telegram_url[0] = '\0';
    cfg->report_progress_enabled = false;  /* Opt-in, disabled by default */
    cfg->report_progress_url[0] = '\0';
    cfg->server_also_worker = true;
    strcpy(cfg->server_host, "0.0.0.0");
}

/* ============================================================================
 * JSON Writer (no external deps)
 * ============================================================================ */

int wizard_config_save(const wizard_config_t *cfg, const char *filepath) {
    FILE *f = fopen(filepath, "w");
    if (!f) {
        fprintf(stderr, "[-] Cannot save config to %s: %s\n", filepath, strerror(errno));
        return -1;
    }

    /* Acquire exclusive lock for writing */
    int fd = fileno(f);
    if (flock(fd, LOCK_EX) != 0) {
        fprintf(stderr, "[-] Cannot lock config file %s: %s\n", filepath, strerror(errno));
        fclose(f);
        return -1;
    }

    fprintf(f, "{\n");
    fprintf(f, "  \"version\": %d,\n", cfg->version);
    fprintf(f, "  \"is_server\": %s,\n", cfg->is_server ? "true" : "false");

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
    fprintf(f, "    \"work_unit_size\": \"%llx\",\n", (unsigned long long)cfg->work_unit_size);
    fprintf(f, "    \"checkpoint_interval\": %d,\n", cfg->checkpoint_interval_sec);
    fprintf(f, "    \"also_worker\": %s,\n", cfg->server_also_worker ? "true" : "false");
    fprintf(f, "    \"auth_token\": \"%s\"\n", cfg->auth_token);
    fprintf(f, "  },\n");

    fprintf(f, "  \"search\": {\n");
    fprintf(f, "    \"mode\": \"%s\",\n", cfg->mode);
    fprintf(f, "    \"key_type\": \"%s\",\n", cfg->key_type);
    fprintf(f, "    \"random_mode\": %s,\n", cfg->random_mode ? "true" : "false");
    fprintf(f, "    \"threads\": %d,\n", cfg->threads);
    fprintf(f, "    \"gpu_percent\": %d,\n", cfg->gpu_percent);
    fprintf(f, "    \"bsgs_n\": \"%llx\",\n", (unsigned long long)cfg->bsgs_n);
    fprintf(f, "    \"bsgs_k\": %d\n", cfg->bsgs_k);
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
    fprintf(f, "  },\n");

    fprintf(f, "  \"webhooks\": {\n");
    fprintf(f, "    \"discord_url\": \"%s\",\n", cfg->webhook_discord_url);
    fprintf(f, "    \"telegram_url\": \"%s\"\n", cfg->webhook_telegram_url);
    fprintf(f, "  },\n");

    fprintf(f, "  \"progress_reporting\": {\n");
    fprintf(f, "    \"report_progress_enabled\": %s,\n", cfg->report_progress_enabled ? "true" : "false");
    fprintf(f, "    \"report_progress_url\": \"%s\"\n", cfg->report_progress_url);
    fprintf(f, "  }\n");

    fprintf(f, "}\n");

    fclose(f);
    return 0;
}

/* ============================================================================
 * JSON Parser (minimal, handles our format)
 * ============================================================================ */

static char* skip_whitespace(char *p) {
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

static char* json_find_key(char *json, const char *key) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    char *pos = strstr(json, search);
    if (!pos) return NULL;
    pos = strchr(pos, ':');
    if (!pos) return NULL;
    return skip_whitespace(pos + 1);
}

static int json_get_int(char *json, const char *key, int def) {
    char *val = json_find_key(json, key);
    if (!val) return def;
    return atoi(val);
}

static long long json_get_llong(char *json, const char *key, long long def) {
    char *val = json_find_key(json, key);
    if (!val) return def;
    return atoll(val);
}

static uint64_t json_get_hex(char *json, const char *key, uint64_t def) {
    char *val = json_find_key(json, key);
    if (!val) return def;
    if (*val == '"') val++;
    return strtoull(val, NULL, 16);
}

static bool json_get_bool(char *json, const char *key, bool def) {
    char *val = json_find_key(json, key);
    if (!val) return def;
    return (strncmp(val, "true", 4) == 0);
}

static void json_get_string(char *json, const char *key, char *buf, size_t bufsize, const char *def) {
    char *val = json_find_key(json, key);
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
    memcpy(buf, val, len);
    buf[len] = '\0';
}

int wizard_config_load(wizard_config_t *cfg, const char *filepath) {
    FILE *f = fopen(filepath, "r");
    if (!f) return -1;

    /* Acquire shared lock for reading */
    int fd = fileno(f);
    if (flock(fd, LOCK_SH) != 0) {
        fclose(f);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > 1048576) {  /* Max 1MB */
        fclose(f);
        return -1;
    }

    char *json = malloc(size + 1);
    if (!json) { fclose(f); return -1; }

    size_t read_size = fread(json, 1, size, f);
    json[read_size] = '\0';
    fclose(f);  /* Releases lock */

    wizard_config_init(cfg);

    cfg->version = json_get_int(json, "version", 1);
    cfg->is_server = json_get_bool(json, "is_server", true);  /* Default to server mode */
    cfg->puzzle_number = json_get_int(json, "number", 71);
    json_get_string(json, "target_address", cfg->target_address, sizeof(cfg->target_address), "");
    json_get_string(json, "range_start", cfg->range_start, sizeof(cfg->range_start), "");
    json_get_string(json, "range_end", cfg->range_end, sizeof(cfg->range_end), "");
    cfg->bits = json_get_int(json, "bits", 71);

    json_get_string(json, "host", cfg->server_host, sizeof(cfg->server_host), "0.0.0.0");
    cfg->server_port = json_get_int(json, "port", 7777);
    cfg->work_unit_size = json_get_hex(json, "work_unit_size", 0x100000000ULL);
    cfg->checkpoint_interval_sec = json_get_int(json, "checkpoint_interval", 60);
    cfg->server_also_worker = json_get_bool(json, "also_worker", true);
    json_get_string(json, "auth_token", cfg->auth_token, sizeof(cfg->auth_token), "");

    json_get_string(json, "mode", cfg->mode, sizeof(cfg->mode), "address");
    json_get_string(json, "key_type", cfg->key_type, sizeof(cfg->key_type), "compress");
    cfg->random_mode = json_get_bool(json, "random_mode", true);
    cfg->threads = json_get_int(json, "threads", -1);
    cfg->gpu_percent = json_get_int(json, "gpu_percent", 0);
    cfg->bsgs_n = json_get_hex(json, "bsgs_n", 0x10000000ULL);
    cfg->bsgs_k = json_get_int(json, "bsgs_k", 1);

    cfg->community_enabled = json_get_bool(json, "enabled", true);
    json_get_string(json, "source", cfg->community_source, sizeof(cfg->community_source), "btcpuzzle.info");
    cfg->community_sync_interval_sec = json_get_int(json, "sync_interval", 3600);
    cfg->community_last_sync = json_get_llong(json, "last_sync", 0);

    cfg->total_ranges = json_get_llong(json, "total_ranges", 0);
    cfg->local_completed = json_get_llong(json, "local_completed", 0);
    cfg->community_excluded = json_get_llong(json, "community_excluded", 0);
    json_get_string(json, "progress_file", cfg->progress_file, sizeof(cfg->progress_file), "wizard_progress.dat");
    json_get_string(json, "exclusion_file", cfg->exclusion_file, sizeof(cfg->exclusion_file), "wizard_excluded.dat");

    json_get_string(json, "discord_url", cfg->webhook_discord_url, sizeof(cfg->webhook_discord_url), "");
    json_get_string(json, "telegram_url", cfg->webhook_telegram_url, sizeof(cfg->webhook_telegram_url), "");

    cfg->report_progress_enabled = json_get_bool(json, "report_progress_enabled", false);
    json_get_string(json, "report_progress_url", cfg->report_progress_url, sizeof(cfg->report_progress_url), "");

    free(json);
    return 0;
}

/* ============================================================================
 * Puzzle Cache Functions
 * ============================================================================ */

int wizard_save_puzzles_cache(const puzzle_def_t *puzzles, int count, const char *filepath) {
    FILE *f = fopen(filepath, "w");
    if (!f) return -1;

    /* Acquire exclusive lock for writing */
    int fd = fileno(f);
    if (flock(fd, LOCK_EX) != 0) {
        fclose(f);
        return -1;
    }

    fprintf(f, "[\n");
    for (int i = 0; i < count; i++) {
        const puzzle_def_t *p = &puzzles[i];
        fprintf(f, "  {\n");
        fprintf(f, "    \"number\": %d,\n", p->number);
        fprintf(f, "    \"target_address\": \"%s\",\n", p->target_address);
        fprintf(f, "    \"range_start\": \"%s\",\n", p->range_start);
        fprintf(f, "    \"range_end\": \"%s\",\n", p->range_end);
        fprintf(f, "    \"bits\": %d,\n", p->bits);
        fprintf(f, "    \"reward_btc\": %.2f,\n", p->reward_btc);
        fprintf(f, "    \"has_public_key\": %s,\n", p->has_public_key ? "true" : "false");
        fprintf(f, "    \"public_key\": \"%s\",\n", p->public_key);
        fprintf(f, "    \"solved\": %s,\n", p->solved ? "true" : "false");
        fprintf(f, "    \"solved_date\": \"%s\",\n", p->solved_date);
        fprintf(f, "    \"solver\": \"%s\"\n", p->solver);
        fprintf(f, "  }%s\n", (i < count - 1) ? "," : "");
    }
    fprintf(f, "]\n");

    fclose(f);
    return 0;
}

int wizard_load_puzzles_cache(puzzle_def_t **puzzles, int *count, const char *filepath) {
    FILE *f = fopen(filepath, "r");
    if (!f) return -1;

    /* Acquire shared lock for reading */
    int fd = fileno(f);
    if (flock(fd, LOCK_SH) != 0) {
        fclose(f);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > 10485760) {  /* Max 10MB */
        fclose(f);
        return -1;
    }

    char *json = malloc(size + 1);
    if (!json) { fclose(f); return -1; }

    size_t read_size = fread(json, 1, size, f);
    json[read_size] = '\0';
    fclose(f);  /* Releases lock */

    /* Count puzzles by counting "number": */
    int n = 0;
    char *scan = json;
    while ((scan = strstr(scan, "\"number\":")) != NULL) {
        n++;
        scan++;
    }

    if (n == 0) {
        free(json);
        *puzzles = NULL;
        *count = 0;
        return 0;
    }

    *puzzles = calloc(n, sizeof(puzzle_def_t));
    if (!*puzzles) {
        free(json);
        return -1;
    }

    /* Parse each puzzle */
    int idx = 0;
    scan = json;
    while ((scan = strstr(scan, "{")) != NULL && idx < n) {
        char *obj_end = strchr(scan, '}');
        if (!obj_end) break;

        /* Temporarily null-terminate this object */
        char saved = *obj_end;
        *obj_end = '\0';

        puzzle_def_t *p = &(*puzzles)[idx];
        p->number = json_get_int(scan, "number", 0);
        if (p->number > 0) {
            json_get_string(scan, "target_address", p->target_address, sizeof(p->target_address), "");
            json_get_string(scan, "range_start", p->range_start, sizeof(p->range_start), "");
            json_get_string(scan, "range_end", p->range_end, sizeof(p->range_end), "");
            p->bits = json_get_int(scan, "bits", 0);
            p->reward_btc = json_get_int(scan, "reward_btc", 0) / 100.0;  /* Stored as cents */
            p->has_public_key = json_get_bool(scan, "has_public_key", false);
            json_get_string(scan, "public_key", p->public_key, sizeof(p->public_key), "");
            p->solved = json_get_bool(scan, "solved", false);
            json_get_string(scan, "solved_date", p->solved_date, sizeof(p->solved_date), "");
            json_get_string(scan, "solver", p->solver, sizeof(p->solver), "");
            idx++;
        }

        *obj_end = saved;
        scan = obj_end + 1;
    }

    *count = idx;
    g_puzzles = *puzzles;
    g_puzzle_count = idx;

    free(json);
    return 0;
}
