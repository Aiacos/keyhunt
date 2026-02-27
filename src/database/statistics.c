// src/database/statistics.c
#include "statistics.h"
#include "perfdb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "database/sqlite3.h"

// Helper function: Calculate mean
static double calculate_mean(const double *values, int count) {
    if (count == 0) return 0.0;
    double sum = 0.0;
    for (int i = 0; i < count; i++) {
        sum += values[i];
    }
    return sum / count;
}

// Helper function: Calculate standard deviation
static double calculate_std_dev(const double *values, int count, double mean) {
    if (count <= 1) return 0.0;
    double sum_sq_diff = 0.0;
    for (int i = 0; i < count; i++) {
        double diff = values[i] - mean;
        sum_sq_diff += diff * diff;
    }
    return sqrt(sum_sq_diff / (count - 1));
}

// Helper function: Calculate median (modifies array via sorting)
static double calculate_median(double *values, int count) {
    if (count == 0) return 0.0;

    // Simple bubble sort for small arrays
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (values[j] > values[j + 1]) {
                double temp = values[j];
                values[j] = values[j + 1];
                values[j + 1] = temp;
            }
        }
    }

    if (count % 2 == 0) {
        return (values[count / 2 - 1] + values[count / 2]) / 2.0;
    } else {
        return values[count / 2];
    }
}

// Helper function: Get database path
static int get_db_path(char *path, size_t path_size) {
    perfdb_get_filepath(path, path_size);
    return 0;
}

// Get trend data for last N benchmark runs
int statistics_get_trend(statistics_trend_t *trend, const char *mode,
                        const char *hardware_hash, int last_n_runs) {
    if (!trend || !mode || !hardware_hash || last_n_runs <= 0) {
        return -1;
    }

    // Initialize trend structure
    memset(trend, 0, sizeof(statistics_trend_t));

    // Get database path
    char db_path[512];
    if (get_db_path(db_path, sizeof(db_path)) != 0) {
        return -1;
    }

    // Open database
    sqlite3 *db = NULL;
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        return -1;
    }

    // Prepare query to fetch last N runs
    const char *sql = "SELECT timestamp, mode, cpu_speed_mkeys, gpu_speed_mkeys, "
                     "hybrid_speed_mkeys FROM performance_history "
                     "WHERE mode = ? AND hardware_hash = ? "
                     "ORDER BY timestamp DESC LIMIT ?";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, mode, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, hardware_hash, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, last_n_runs);

    // Allocate points array
    trend->points = (statistics_trend_point_t *)calloc(last_n_runs, sizeof(statistics_trend_point_t));
    if (!trend->points) {
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return -1;
    }

    // Arrays for statistical calculations
    double *cpu_speeds = (double *)calloc(last_n_runs, sizeof(double));
    double *gpu_speeds = (double *)calloc(last_n_runs, sizeof(double));
    double *hybrid_speeds = (double *)calloc(last_n_runs, sizeof(double));

    if (!cpu_speeds || !gpu_speeds || !hybrid_speeds) {
        free(trend->points);
        free(cpu_speeds);
        free(gpu_speeds);
        free(hybrid_speeds);
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return -1;
    }

    // Fetch results
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && count < last_n_runs) {
        statistics_trend_point_t *point = &trend->points[count];

        point->timestamp = (time_t)sqlite3_column_int64(stmt, 0);
        strncpy(point->mode, (const char *)sqlite3_column_text(stmt, 1), sizeof(point->mode) - 1);
        point->cpu_speed_mkeys = sqlite3_column_double(stmt, 2);
        point->gpu_speed_mkeys = sqlite3_column_double(stmt, 3);
        point->hybrid_speed_mkeys = sqlite3_column_double(stmt, 4);

        // Format timestamp
        struct tm *tm_info = localtime(&point->timestamp);
        strftime(point->date_str, sizeof(point->date_str), "%Y-%m-%d %H:%M", tm_info);

        // Store for statistics
        cpu_speeds[count] = point->cpu_speed_mkeys;
        gpu_speeds[count] = point->gpu_speed_mkeys;
        hybrid_speeds[count] = point->hybrid_speed_mkeys;

        count++;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    trend->count = count;

    if (count == 0) {
        free(trend->points);
        free(cpu_speeds);
        free(gpu_speeds);
        free(hybrid_speeds);
        trend->points = NULL;
        return 0;  // Not an error, just no data
    }

    // Calculate summary statistics
    trend->avg_cpu_speed = calculate_mean(cpu_speeds, count);
    trend->avg_gpu_speed = calculate_mean(gpu_speeds, count);
    trend->avg_hybrid_speed = calculate_mean(hybrid_speeds, count);

    // Find min/max
    trend->min_cpu_speed = cpu_speeds[0];
    trend->max_cpu_speed = cpu_speeds[0];
    for (int i = 1; i < count; i++) {
        if (cpu_speeds[i] < trend->min_cpu_speed) trend->min_cpu_speed = cpu_speeds[i];
        if (cpu_speeds[i] > trend->max_cpu_speed) trend->max_cpu_speed = cpu_speeds[i];
    }

    // Calculate median and standard deviation
    trend->median_cpu_speed = calculate_median(cpu_speeds, count);
    trend->std_dev_cpu_speed = calculate_std_dev(cpu_speeds, count, trend->avg_cpu_speed);

    // Calculate trend (compare most recent to oldest)
    if (count >= 2) {
        double oldest_speed = cpu_speeds[count - 1];
        double newest_speed = cpu_speeds[0];
        if (oldest_speed > 0.0) {
            trend->cpu_trend_percent = ((newest_speed - oldest_speed) / oldest_speed) * 100.0;
            trend->is_improving = (trend->cpu_trend_percent > 0.0);
        }
    }

    free(cpu_speeds);
    free(gpu_speeds);
    free(hybrid_speeds);

    return 0;
}

// Free trend data
void statistics_free_trend(statistics_trend_t *trend) {
    if (trend && trend->points) {
        free(trend->points);
        trend->points = NULL;
        trend->count = 0;
    }
}

// Get average speed
int statistics_get_avg_speed(double *avg_cpu_speed, double *avg_gpu_speed,
                             double *avg_hybrid_speed, const char *mode,
                             const char *hardware_hash) {
    if (!avg_cpu_speed || !avg_gpu_speed || !avg_hybrid_speed || !mode || !hardware_hash) {
        return -1;
    }

    // Get database path
    char db_path[512];
    if (get_db_path(db_path, sizeof(db_path)) != 0) {
        return -1;
    }

    // Open database
    sqlite3 *db = NULL;
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        return -1;
    }

    // Query average speeds
    const char *sql = "SELECT AVG(cpu_speed_mkeys), AVG(gpu_speed_mkeys), "
                     "AVG(hybrid_speed_mkeys) FROM performance_history "
                     "WHERE mode = ? AND hardware_hash = ?";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, mode, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, hardware_hash, -1, SQLITE_STATIC);

    int result = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        *avg_cpu_speed = sqlite3_column_double(stmt, 0);
        *avg_gpu_speed = sqlite3_column_double(stmt, 1);
        *avg_hybrid_speed = sqlite3_column_double(stmt, 2);
        result = 0;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return result;
}

// Detect regression with detailed analysis
int statistics_detect_regression(statistics_regression_t *regression,
                                 const char *mode, const char *hardware_hash,
                                 double threshold_percent) {
    if (!regression || !mode || !hardware_hash) {
        return -1;
    }

    memset(regression, 0, sizeof(statistics_regression_t));

    // Use perfdb function for basic regression detection
    double actual_change = 0.0;
    int detected = perfdb_detect_regression(mode, hardware_hash, threshold_percent, &actual_change);

    if (detected < 0) {
        return -1;  // Error
    }

    regression->regression_detected = (detected == 1);
    regression->change_percent = actual_change;

    // Get recent speed and historical average for detailed analysis
    char db_path[512];
    if (get_db_path(db_path, sizeof(db_path)) != 0) {
        return -1;
    }

    sqlite3 *db = NULL;
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        return -1;
    }

    // Get most recent speed
    const char *sql_recent = "SELECT cpu_speed_mkeys FROM performance_history "
                            "WHERE mode = ? AND hardware_hash = ? "
                            "ORDER BY timestamp DESC LIMIT 1";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql_recent, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, mode, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, hardware_hash, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            regression->recent_speed = sqlite3_column_double(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    // Get historical average (excluding most recent)
    const char *sql_avg = "SELECT AVG(cpu_speed_mkeys), COUNT(*) FROM performance_history "
                         "WHERE mode = ? AND hardware_hash = ? "
                         "AND timestamp < (SELECT MAX(timestamp) FROM performance_history "
                         "                 WHERE mode = ? AND hardware_hash = ?)";

    if (sqlite3_prepare_v2(db, sql_avg, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, mode, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, hardware_hash, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, mode, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, hardware_hash, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            regression->historical_avg = sqlite3_column_double(stmt, 0);
            regression->comparison_count = sqlite3_column_int(stmt, 1);
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);

    // Generate human-readable analysis
    if (regression->regression_detected) {
        snprintf(regression->analysis, sizeof(regression->analysis),
                "Performance degraded by %.1f%% (%.2f → %.2f Mkeys/s). "
                "Possible causes: thermal throttling, background processes, or configuration changes.",
                fabs(regression->change_percent), regression->historical_avg, regression->recent_speed);
    } else if (regression->change_percent > 5.0) {
        snprintf(regression->analysis, sizeof(regression->analysis),
                "Performance improved by %.1f%% (%.2f → %.2f Mkeys/s). Good job!",
                regression->change_percent, regression->historical_avg, regression->recent_speed);
    } else {
        snprintf(regression->analysis, sizeof(regression->analysis),
                "Performance is stable (%.1f%% change).",
                regression->change_percent);
    }

    return 0;
}

// Get speed summary statistics
int statistics_get_speed_summary(statistics_speed_summary_t *summary,
                                const char *mode, const char *hardware_hash,
                                const char *speed_type) {
    if (!summary || !mode || !hardware_hash || !speed_type) {
        return -1;
    }

    memset(summary, 0, sizeof(statistics_speed_summary_t));

    // Determine column name
    const char *column;
    if (strcmp(speed_type, "cpu") == 0) {
        column = "cpu_speed_mkeys";
    } else if (strcmp(speed_type, "gpu") == 0) {
        column = "gpu_speed_mkeys";
    } else if (strcmp(speed_type, "hybrid") == 0) {
        column = "hybrid_speed_mkeys";
    } else {
        return -1;
    }

    // Get database path
    char db_path[512];
    if (get_db_path(db_path, sizeof(db_path)) != 0) {
        return -1;
    }

    sqlite3 *db = NULL;
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        return -1;
    }

    // Build query
    char sql[512];
    snprintf(sql, sizeof(sql),
            "SELECT %s FROM performance_history WHERE mode = ? AND hardware_hash = ? "
            "ORDER BY %s", column, column);

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, mode, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, hardware_hash, -1, SQLITE_STATIC);

    // Fetch all values
    double *values = NULL;
    int capacity = 100;
    int count = 0;

    values = (double *)malloc(capacity * sizeof(double));
    if (!values) {
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return -1;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (count >= capacity) {
            capacity *= 2;
            double *new_values = (double *)realloc(values, capacity * sizeof(double));
            if (!new_values) {
                free(values);
                sqlite3_finalize(stmt);
                sqlite3_close(db);
                return -1;
            }
            values = new_values;
        }
        values[count++] = sqlite3_column_double(stmt, 0);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    if (count == 0) {
        free(values);
        return -1;
    }

    summary->sample_count = count;
    summary->avg_speed = calculate_mean(values, count);
    summary->min_speed = values[0];  // Already sorted by query
    summary->max_speed = values[count - 1];
    summary->median_speed = calculate_median(values, count);
    summary->std_dev = calculate_std_dev(values, count, summary->avg_speed);

    free(values);
    return 0;
}

// Calculate percentile rank
double statistics_percentile_rank(double speed, const char *mode,
                                  const char *hardware_hash,
                                  const char *speed_type) {
    if (!mode || !hardware_hash || !speed_type) {
        return -1.0;
    }

    // Get all speeds for comparison
    statistics_speed_summary_t summary;
    if (statistics_get_speed_summary(&summary, mode, hardware_hash, speed_type) != 0) {
        return -1.0;
    }

    if (summary.sample_count == 0) {
        return -1.0;
    }

    // Fetch all values again to count how many are below this speed
    char db_path[512];
    if (get_db_path(db_path, sizeof(db_path)) != 0) {
        return -1.0;
    }

    sqlite3 *db = NULL;
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        return -1.0;
    }

    const char *column;
    if (strcmp(speed_type, "cpu") == 0) {
        column = "cpu_speed_mkeys";
    } else if (strcmp(speed_type, "gpu") == 0) {
        column = "gpu_speed_mkeys";
    } else if (strcmp(speed_type, "hybrid") == 0) {
        column = "hybrid_speed_mkeys";
    } else {
        sqlite3_close(db);
        return -1.0;
    }

    char sql[512];
    snprintf(sql, sizeof(sql),
            "SELECT COUNT(*) FROM performance_history WHERE mode = ? AND hardware_hash = ? "
            "AND %s < ?", column);

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return -1.0;
    }

    sqlite3_bind_text(stmt, 1, mode, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, hardware_hash, -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 3, speed);

    int count_below = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count_below = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    // Calculate percentile: (count below / total count) * 100
    double percentile = ((double)count_below / (double)summary.sample_count) * 100.0;
    return percentile;
}

// Compare current hardware to historical performance
int statistics_compare_to_history(const perfdb_benchmark_t *current,
                                  double *cpu_percentile,
                                  double *gpu_percentile) {
    if (!current || !cpu_percentile || !gpu_percentile) {
        return -1;
    }

    *cpu_percentile = statistics_percentile_rank(current->cpu_speed_mkeys,
                                                current->mode,
                                                current->hardware_hash,
                                                "cpu");

    if (current->gpu_speed_mkeys > 0.0) {
        *gpu_percentile = statistics_percentile_rank(current->gpu_speed_mkeys,
                                                    current->mode,
                                                    current->hardware_hash,
                                                    "gpu");
    } else {
        *gpu_percentile = 0.0;
    }

    return 0;
}

// Detect outliers
bool statistics_is_outlier(double speed, const char *mode,
                          const char *hardware_hash,
                          const char *speed_type) {
    statistics_speed_summary_t summary;
    if (statistics_get_speed_summary(&summary, mode, hardware_hash, speed_type) != 0) {
        return false;
    }

    if (summary.sample_count < 3) {
        return false;  // Need at least 3 samples for outlier detection
    }

    // Outlier if more than 3 standard deviations from mean
    double z_score = fabs(speed - summary.avg_speed) / summary.std_dev;
    return (z_score > 3.0);
}

// Get performance change rate
double statistics_get_change_rate(const char *mode, const char *hardware_hash,
                                 const char *speed_type, int days) {
    if (!mode || !hardware_hash || !speed_type || days <= 0) {
        return 0.0;
    }

    // Get database path
    char db_path[512];
    if (get_db_path(db_path, sizeof(db_path)) != 0) {
        return 0.0;
    }

    sqlite3 *db = NULL;
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        return 0.0;
    }

    const char *column;
    if (strcmp(speed_type, "cpu") == 0) {
        column = "cpu_speed_mkeys";
    } else if (strcmp(speed_type, "gpu") == 0) {
        column = "gpu_speed_mkeys";
    } else if (strcmp(speed_type, "hybrid") == 0) {
        column = "hybrid_speed_mkeys";
    } else {
        sqlite3_close(db);
        return 0.0;
    }

    // Get oldest and newest speeds within the time window
    char sql[512];
    snprintf(sql, sizeof(sql),
            "SELECT MIN(timestamp), MAX(timestamp), "
            "  (SELECT %s FROM performance_history WHERE mode = ? AND hardware_hash = ? "
            "   ORDER BY timestamp ASC LIMIT 1) as oldest_speed, "
            "  (SELECT %s FROM performance_history WHERE mode = ? AND hardware_hash = ? "
            "   ORDER BY timestamp DESC LIMIT 1) as newest_speed "
            "FROM performance_history WHERE mode = ? AND hardware_hash = ? "
            "AND timestamp >= datetime('now', '-%d days')",
            column, column, days);

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return 0.0;
    }

    sqlite3_bind_text(stmt, 1, mode, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, hardware_hash, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, mode, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, hardware_hash, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, mode, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, hardware_hash, -1, SQLITE_STATIC);

    double change_rate = 0.0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        time_t min_time = (time_t)sqlite3_column_int64(stmt, 0);
        time_t max_time = (time_t)sqlite3_column_int64(stmt, 1);
        double oldest_speed = sqlite3_column_double(stmt, 2);
        double newest_speed = sqlite3_column_double(stmt, 3);

        double time_diff_days = (double)(max_time - min_time) / 86400.0;
        if (time_diff_days > 0.0) {
            change_rate = (newest_speed - oldest_speed) / time_diff_days;
        }
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return change_rate;
}
