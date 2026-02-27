// src/database/perfdb.c
#include "perfdb.h"
#include "platform/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "database/sqlite3.h"

#if !PLATFORM_WINDOWS
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

// Get expanded database directory path (replaces ~ with $HOME)
static int get_perfdb_dir(char *path, size_t path_size) {
    if (!path || path_size == 0) return -1;

    const char *home = getenv("HOME");
    if (!home || !home[0]) {
        home = "/tmp";
    }

    int ret = snprintf(path, path_size, "%s/.keyhunt", home);
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

// Get database file path
void perfdb_get_filepath(char *path, size_t path_size) {
    if (!path || path_size == 0) return;

    char dir[512];
    if (get_perfdb_dir(dir, sizeof(dir)) != 0) {
        path[0] = '\0';
        return;
    }

    snprintf(path, path_size, "%s/%s", dir, PERFDB_FILENAME);
}

// Check if database exists
bool perfdb_exists(void) {
    char filepath[512];
    perfdb_get_filepath(filepath, sizeof(filepath));

    if (filepath[0] == '\0') return false;

    struct stat st;
    return (stat(filepath, &st) == 0);
}

// Simple hash function for hardware fingerprint
static unsigned int simple_hash(const char *str) {
    if (!str) return 0;
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

// Compute hardware fingerprint (simplified SHA256 replacement)
void perfdb_compute_hardware_hash(const system_info_t *hardware, char *hash_out, size_t hash_size) {
    if (!hardware || !hash_out || hash_size < 65) return;

    // Create a unique fingerprint from hardware characteristics
    char fingerprint[512];
    snprintf(fingerprint, sizeof(fingerprint),
             "%s_%d_%d_%d_%d_%d_%d_%s",
             hardware->cpu_model,
             hardware->cpu_physical_cores,
             hardware->has_avx2 ? 1 : 0,
             hardware->has_avx512 ? 1 : 0,
             hardware->has_avx512f ? 1 : 0,
             hardware->has_sha_ni ? 1 : 0,
             hardware->gpu_count,
             hardware->gpu_name[0] ? hardware->gpu_name : "no_gpu");

    // Generate a simple hash (for now, use a simple hash function)
    // In production, this should use SHA256
    unsigned int h1 = simple_hash(fingerprint);
    unsigned int h2 = simple_hash(fingerprint + strlen(fingerprint) / 2);
    unsigned int h3 = simple_hash(hardware->cpu_model);
    unsigned int h4 = simple_hash(hardware->gpu_name);

    snprintf(hash_out, hash_size, "%08x%08x%08x%08x%08x%08x%08x%08x",
             h1, h2, h3, h4,
             (unsigned int)hardware->cpu_physical_cores,
             (unsigned int)(hardware->has_avx2 << 24 | hardware->has_avx512 << 16),
             (unsigned int)hardware->gpu_count,
             (unsigned int)hardware->gpu_compute_capability);
}

// Execute schema.sql to create tables
int perfdb_create_schema(void) {
    char dbpath[512];
    perfdb_get_filepath(dbpath, sizeof(dbpath));
    if (dbpath[0] == '\0') return -1;

    sqlite3 *db = NULL;
    int rc = sqlite3_open(dbpath, &db);
    if (rc != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return -1;
    }

    // Read and execute schema.sql
    // For simplicity, we'll embed the essential schema here
    // In a production system, you'd read from the file

    const char *schema_sql =
        "CREATE TABLE IF NOT EXISTS schema_version ("
        "  version INTEGER PRIMARY KEY,"
        "  applied_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "  description TEXT"
        ");"
        ""
        "INSERT OR IGNORE INTO schema_version (version, description) VALUES"
        "  (1, 'Initial schema: performance_history table');"
        ""
        "CREATE TABLE IF NOT EXISTS performance_history ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "  run_date DATE DEFAULT (DATE('now')),"
        "  mode TEXT NOT NULL,"
        "  bits INTEGER,"
        "  key_type TEXT,"
        "  cpu_speed_mkeys REAL NOT NULL,"
        "  gpu_speed_mkeys REAL DEFAULT 0.0,"
        "  hybrid_speed_mkeys REAL DEFAULT 0.0,"
        "  efficiency_ratio REAL DEFAULT 1.0,"
        "  cpu_model TEXT NOT NULL,"
        "  cpu_physical_cores INTEGER NOT NULL,"
        "  cpu_logical_cores INTEGER NOT NULL,"
        "  cpu_threads INTEGER NOT NULL,"
        "  has_avx2 INTEGER DEFAULT 0,"
        "  has_avx512 INTEGER DEFAULT 0,"
        "  has_avx512f INTEGER DEFAULT 0,"
        "  has_avx512dq INTEGER DEFAULT 0,"
        "  has_sha_ni INTEGER DEFAULT 0,"
        "  ram_total_mb INTEGER,"
        "  ram_available_mb INTEGER,"
        "  cache_l1_kb INTEGER,"
        "  cache_l2_kb INTEGER,"
        "  cache_l3_kb INTEGER,"
        "  gpu_count INTEGER DEFAULT 0,"
        "  gpu_name TEXT,"
        "  gpu_vram_mb INTEGER,"
        "  gpu_sm_count INTEGER,"
        "  gpu_compute_capability INTEGER,"
        "  hardware_hash TEXT NOT NULL,"
        "  cpu_score REAL,"
        "  gpu_score REAL,"
        "  keyhunt_version TEXT,"
        "  benchmark_duration_seconds INTEGER,"
        "  notes TEXT"
        ");"
        ""
        "CREATE INDEX IF NOT EXISTS idx_performance_timestamp"
        "  ON performance_history(timestamp DESC);"
        ""
        "CREATE INDEX IF NOT EXISTS idx_performance_mode"
        "  ON performance_history(mode);"
        ""
        "CREATE INDEX IF NOT EXISTS idx_performance_hardware"
        "  ON performance_history(hardware_hash);"
        ""
        "CREATE INDEX IF NOT EXISTS idx_performance_mode_hardware"
        "  ON performance_history(mode, hardware_hash, timestamp DESC);";

    char *err_msg = NULL;
    rc = sqlite3_exec(db, schema_sql, NULL, NULL, &err_msg);

    if (rc != SQLITE_OK) {
        if (err_msg) {
            sqlite3_free(err_msg);
        }
        sqlite3_close(db);
        return -1;
    }

    sqlite3_close(db);
    return 0;
}

// Initialize database
int perfdb_init(void) {
    // Create directory if needed
    char dir[512];
    if (get_perfdb_dir(dir, sizeof(dir)) != 0) {
        return -1;
    }

    if (mkdirp(dir) != 0) {
        return -1;
    }

    // Create database and schema if it doesn't exist
    if (!perfdb_exists()) {
        return perfdb_create_schema();
    }

    return 0;
}

// Save benchmark result to database
int perfdb_save_benchmark(const perfdb_benchmark_t *benchmark) {
    if (!benchmark) return -1;

    // Initialize database if needed
    if (perfdb_init() != 0) {
        return -1;
    }

    char dbpath[512];
    perfdb_get_filepath(dbpath, sizeof(dbpath));
    if (dbpath[0] == '\0') return -1;

    sqlite3 *db = NULL;
    int rc = sqlite3_open(dbpath, &db);
    if (rc != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return -1;
    }

    // Prepare INSERT statement
    const char *sql =
        "INSERT INTO performance_history ("
        "  mode, bits, key_type,"
        "  cpu_speed_mkeys, gpu_speed_mkeys, hybrid_speed_mkeys, efficiency_ratio,"
        "  cpu_model, cpu_physical_cores, cpu_logical_cores, cpu_threads,"
        "  has_avx2, has_avx512, has_avx512f, has_avx512dq, has_sha_ni,"
        "  ram_total_mb, ram_available_mb,"
        "  cache_l1_kb, cache_l2_kb, cache_l3_kb,"
        "  gpu_count, gpu_name, gpu_vram_mb, gpu_sm_count, gpu_compute_capability,"
        "  hardware_hash, cpu_score, gpu_score,"
        "  keyhunt_version, benchmark_duration_seconds, notes"
        ") VALUES ("
        "  ?, ?, ?,"
        "  ?, ?, ?, ?,"
        "  ?, ?, ?, ?,"
        "  ?, ?, ?, ?, ?,"
        "  ?, ?,"
        "  ?, ?, ?,"
        "  ?, ?, ?, ?, ?,"
        "  ?, ?, ?,"
        "  ?, ?, ?"
        ");";

    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        sqlite3_close(db);
        return -1;
    }

    // Bind parameters
    int idx = 1;
    sqlite3_bind_text(stmt, idx++, benchmark->mode, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, idx++, benchmark->bits);
    sqlite3_bind_text(stmt, idx++, benchmark->key_type, -1, SQLITE_TRANSIENT);

    sqlite3_bind_double(stmt, idx++, benchmark->cpu_speed_mkeys);
    sqlite3_bind_double(stmt, idx++, benchmark->gpu_speed_mkeys);
    sqlite3_bind_double(stmt, idx++, benchmark->hybrid_speed_mkeys);
    sqlite3_bind_double(stmt, idx++, benchmark->efficiency_ratio);

    sqlite3_bind_text(stmt, idx++, benchmark->hardware.cpu_model, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, idx++, benchmark->hardware.cpu_physical_cores);
    sqlite3_bind_int(stmt, idx++, benchmark->hardware.cpu_logical_cores);
    sqlite3_bind_int(stmt, idx++, benchmark->hardware.cpu_threads_optimal);

    sqlite3_bind_int(stmt, idx++, benchmark->hardware.has_avx2 ? 1 : 0);
    sqlite3_bind_int(stmt, idx++, benchmark->hardware.has_avx512 ? 1 : 0);
    sqlite3_bind_int(stmt, idx++, benchmark->hardware.has_avx512f ? 1 : 0);
    sqlite3_bind_int(stmt, idx++, benchmark->hardware.has_avx512dq ? 1 : 0);
    sqlite3_bind_int(stmt, idx++, benchmark->hardware.has_sha_ni ? 1 : 0);

    sqlite3_bind_int64(stmt, idx++, (sqlite3_int64)benchmark->hardware.ram_total);
    sqlite3_bind_int64(stmt, idx++, (sqlite3_int64)benchmark->hardware.ram_available);

    sqlite3_bind_int64(stmt, idx++, (sqlite3_int64)benchmark->hardware.cache_l1_size);
    sqlite3_bind_int64(stmt, idx++, (sqlite3_int64)benchmark->hardware.cache_l2_size);
    sqlite3_bind_int64(stmt, idx++, (sqlite3_int64)benchmark->hardware.cache_l3_size);

    sqlite3_bind_int(stmt, idx++, benchmark->hardware.gpu_count);
    sqlite3_bind_text(stmt, idx++, benchmark->hardware.gpu_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, idx++, (sqlite3_int64)benchmark->hardware.gpu_vram_mb);
    sqlite3_bind_int(stmt, idx++, benchmark->hardware.gpu_sm_count);
    sqlite3_bind_int(stmt, idx++, benchmark->hardware.gpu_compute_capability);

    sqlite3_bind_text(stmt, idx++, benchmark->hardware_hash, -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, idx++, benchmark->hardware.cpu_score);
    sqlite3_bind_double(stmt, idx++, benchmark->hardware.gpu_score);

    sqlite3_bind_text(stmt, idx++, benchmark->keyhunt_version, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, idx++, benchmark->benchmark_duration_seconds);
    sqlite3_bind_text(stmt, idx++, benchmark->notes, -1, SQLITE_TRANSIENT);

    // Execute
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return (rc == SQLITE_DONE) ? 0 : -1;
}

// Query recent benchmarks
int perfdb_query_recent(perfdb_result_t *results, int max_results, const char *mode) {
    if (!results || max_results <= 0) return -1;

    if (!perfdb_exists()) return 0;

    char dbpath[512];
    perfdb_get_filepath(dbpath, sizeof(dbpath));
    if (dbpath[0] == '\0') return -1;

    sqlite3 *db = NULL;
    int rc = sqlite3_open(dbpath, &db);
    if (rc != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return -1;
    }

    const char *sql;
    if (mode && mode[0] != '\0') {
        sql = "SELECT id, strftime('%s', timestamp), mode, cpu_speed_mkeys, "
              "gpu_speed_mkeys, hybrid_speed_mkeys, cpu_model, gpu_name "
              "FROM performance_history "
              "WHERE mode = ? "
              "ORDER BY timestamp DESC "
              "LIMIT ?;";
    } else {
        sql = "SELECT id, strftime('%s', timestamp), mode, cpu_speed_mkeys, "
              "gpu_speed_mkeys, hybrid_speed_mkeys, cpu_model, gpu_name "
              "FROM performance_history "
              "ORDER BY timestamp DESC "
              "LIMIT ?;";
    }

    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        sqlite3_close(db);
        return -1;
    }

    // Bind parameters
    if (mode && mode[0] != '\0') {
        sqlite3_bind_text(stmt, 1, mode, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, max_results);
    } else {
        sqlite3_bind_int(stmt, 1, max_results);
    }

    // Fetch results
    int count = 0;
    while (count < max_results && sqlite3_step(stmt) == SQLITE_ROW) {
        results[count].id = sqlite3_column_int(stmt, 0);
        results[count].timestamp = (time_t)sqlite3_column_int64(stmt, 1);

        const char *str;
        str = (const char *)sqlite3_column_text(stmt, 2);
        if (str) strncpy(results[count].mode, str, sizeof(results[count].mode) - 1);

        results[count].cpu_speed_mkeys = sqlite3_column_double(stmt, 3);
        results[count].gpu_speed_mkeys = sqlite3_column_double(stmt, 4);
        results[count].hybrid_speed_mkeys = sqlite3_column_double(stmt, 5);

        str = (const char *)sqlite3_column_text(stmt, 6);
        if (str) strncpy(results[count].cpu_model, str, sizeof(results[count].cpu_model) - 1);

        str = (const char *)sqlite3_column_text(stmt, 7);
        if (str) strncpy(results[count].gpu_name, str, sizeof(results[count].gpu_name) - 1);

        count++;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return count;
}

// Get statistics for a specific mode and hardware
int perfdb_get_stats(perfdb_stats_t *stats, const char *mode, const char *hardware_hash) {
    if (!stats) return -1;

    memset(stats, 0, sizeof(perfdb_stats_t));

    if (!perfdb_exists()) return -1;

    char dbpath[512];
    perfdb_get_filepath(dbpath, sizeof(dbpath));
    if (dbpath[0] == '\0') return -1;

    sqlite3 *db = NULL;
    int rc = sqlite3_open(dbpath, &db);
    if (rc != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return -1;
    }

    // Get aggregate statistics
    const char *sql =
        "SELECT COUNT(*), "
        "  AVG(cpu_speed_mkeys), AVG(gpu_speed_mkeys), AVG(hybrid_speed_mkeys),"
        "  MIN(cpu_speed_mkeys), MAX(cpu_speed_mkeys) "
        "FROM performance_history "
        "WHERE mode = ? AND hardware_hash = ?;";

    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        sqlite3_close(db);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, mode, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, hardware_hash, -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        stats->count = sqlite3_column_int(stmt, 0);
        stats->avg_cpu_speed = sqlite3_column_double(stmt, 1);
        stats->avg_gpu_speed = sqlite3_column_double(stmt, 2);
        stats->avg_hybrid_speed = sqlite3_column_double(stmt, 3);
        stats->min_cpu_speed = sqlite3_column_double(stmt, 4);
        stats->max_cpu_speed = sqlite3_column_double(stmt, 5);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return 0;
}

// Detect performance regression
int perfdb_detect_regression(const char *mode, const char *hardware_hash,
                             double threshold_percent, double *actual_change) {
    if (!mode || !hardware_hash) return -1;

    if (!perfdb_exists()) return 0;  // No data, no regression

    char dbpath[512];
    perfdb_get_filepath(dbpath, sizeof(dbpath));
    if (dbpath[0] == '\0') return -1;

    sqlite3 *db = NULL;
    int rc = sqlite3_open(dbpath, &db);
    if (rc != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return -1;
    }

    // Get most recent result and historical average
    const char *sql =
        "SELECT "
        "  (SELECT cpu_speed_mkeys FROM performance_history "
        "   WHERE mode = ? AND hardware_hash = ? "
        "   ORDER BY timestamp DESC LIMIT 1) as recent_speed,"
        "  AVG(cpu_speed_mkeys) as historical_avg "
        "FROM performance_history "
        "WHERE mode = ? AND hardware_hash = ? "
        "  AND timestamp < (SELECT MAX(timestamp) FROM performance_history "
        "                   WHERE mode = ? AND hardware_hash = ?);";

    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        sqlite3_close(db);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, mode, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, hardware_hash, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, mode, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, hardware_hash, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, mode, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, hardware_hash, -1, SQLITE_TRANSIENT);

    int result = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        double recent = sqlite3_column_double(stmt, 0);
        double historical = sqlite3_column_double(stmt, 1);

        if (historical > 0.0) {
            double change = ((recent - historical) / historical) * 100.0;
            if (actual_change) *actual_change = change;

            // Regression detected if change is below threshold
            if (change < threshold_percent) {
                result = 1;
            }
        }
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return result;
}

// List all benchmarks
int perfdb_list_all(perfdb_result_t *results, int max_results) {
    return perfdb_query_recent(results, max_results, NULL);
}

// Delete old benchmarks (keep only last N days)
int perfdb_cleanup_old(int keep_days) {
    if (keep_days < 0) return -1;

    if (!perfdb_exists()) return 0;

    char dbpath[512];
    perfdb_get_filepath(dbpath, sizeof(dbpath));
    if (dbpath[0] == '\0') return -1;

    sqlite3 *db = NULL;
    int rc = sqlite3_open(dbpath, &db);
    if (rc != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return -1;
    }

    char sql[256];
    snprintf(sql, sizeof(sql),
             "DELETE FROM performance_history "
             "WHERE timestamp < datetime('now', '-%d days');",
             keep_days);

    char *err_msg = NULL;
    rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);

    if (rc != SQLITE_OK) {
        if (err_msg) sqlite3_free(err_msg);
        sqlite3_close(db);
        return -1;
    }

    sqlite3_close(db);
    return 0;
}
