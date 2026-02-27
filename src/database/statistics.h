// src/database/statistics.h
#ifndef STATISTICS_H
#define STATISTICS_H

#include <stdint.h>
#include <stdbool.h>
#include "perfdb.h"

#ifdef __cplusplus
extern "C" {
#endif

// Trend data point for visualization
typedef struct {
    time_t timestamp;
    char date_str[32];          // Human-readable date (YYYY-MM-DD HH:MM)
    double cpu_speed_mkeys;
    double gpu_speed_mkeys;
    double hybrid_speed_mkeys;
    char mode[32];
} statistics_trend_point_t;

// Trend analysis result
typedef struct {
    int count;                          // Number of data points
    statistics_trend_point_t *points;   // Array of trend points (allocated)

    // Summary statistics
    double avg_cpu_speed;
    double avg_gpu_speed;
    double avg_hybrid_speed;
    double min_cpu_speed;
    double max_cpu_speed;
    double median_cpu_speed;
    double std_dev_cpu_speed;

    // Trend direction
    double cpu_trend_percent;           // Positive = improving, Negative = degrading
    bool is_improving;                  // Overall trend assessment
} statistics_trend_t;

// Regression analysis result
typedef struct {
    bool regression_detected;
    double change_percent;              // Negative = slower
    double recent_speed;
    double historical_avg;
    int comparison_count;               // Number of historical benchmarks compared
    char analysis[256];                 // Human-readable analysis
} statistics_regression_t;

// Speed statistics summary
typedef struct {
    double avg_speed;
    double min_speed;
    double max_speed;
    double median_speed;
    double std_dev;
    int sample_count;
} statistics_speed_summary_t;

// Get trend data for last N benchmark runs
// Returns: 0 on success, -1 on error
// Caller must call statistics_free_trend() to free allocated memory
int statistics_get_trend(statistics_trend_t *trend, const char *mode,
                        const char *hardware_hash, int last_n_runs);

// Free trend data allocated by statistics_get_trend()
void statistics_free_trend(statistics_trend_t *trend);

// Get average speed for a specific mode and hardware
// Returns: 0 on success, -1 on error
int statistics_get_avg_speed(double *avg_cpu_speed, double *avg_gpu_speed,
                             double *avg_hybrid_speed, const char *mode,
                             const char *hardware_hash);

// Detect performance regression with detailed analysis
// threshold_percent: negative value (e.g., -10.0 for 10% slowdown)
// Returns: 0 on success, -1 on error
int statistics_detect_regression(statistics_regression_t *regression,
                                 const char *mode, const char *hardware_hash,
                                 double threshold_percent);

// Get speed summary statistics
// Returns: 0 on success, -1 on error
int statistics_get_speed_summary(statistics_speed_summary_t *summary,
                                const char *mode, const char *hardware_hash,
                                const char *speed_type);  // "cpu", "gpu", or "hybrid"

// Calculate percentile rank (e.g., "faster than X% of benchmarks")
// Returns: percentile (0.0 to 100.0), or -1.0 on error
double statistics_percentile_rank(double speed, const char *mode,
                                  const char *hardware_hash,
                                  const char *speed_type);

// Compare current hardware to historical performance
// Returns: 0 on success, -1 on error
int statistics_compare_to_history(const perfdb_benchmark_t *current,
                                  double *cpu_percentile,
                                  double *gpu_percentile);

// Detect outliers in performance data (for data quality)
// Returns: true if speed is an outlier (>3 standard deviations from mean)
bool statistics_is_outlier(double speed, const char *mode,
                          const char *hardware_hash,
                          const char *speed_type);

// Get performance change rate (improvement/degradation per day)
// Returns: change rate (Mkeys/s per day), or 0.0 if insufficient data
double statistics_get_change_rate(const char *mode, const char *hardware_hash,
                                 const char *speed_type, int days);

#ifdef __cplusplus
}
#endif

#endif // STATISTICS_H
