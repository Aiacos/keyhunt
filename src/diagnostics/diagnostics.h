#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include <stdint.h>
#include <stdbool.h>
#include "../core/sysinfo.h"

#ifdef __cplusplus
extern "C" {
#endif

// Diagnostic check result
typedef enum {
    DIAG_OK = 0,       // Check passed, no issues
    DIAG_WARNING = 1,  // Potential issue, system may work with degraded performance
    DIAG_ERROR = 2,    // Critical issue, system may not work correctly
    DIAG_INFO = 3      // Informational check, no pass/fail
} diag_status_t;

// Individual diagnostic check result
typedef struct {
    diag_status_t status;
    char check_name[64];
    char message[256];
    char recommendation[256];
} diag_check_result_t;

// Full diagnostic report
typedef struct {
    system_info_t sysinfo;
    int check_count;
    diag_check_result_t checks[32];  // Max 32 diagnostic checks
    int error_count;
    int warning_count;
} diagnostic_report_t;

// Run full system diagnostics
// Performs comprehensive hardware and configuration checks
// Returns diagnostic report with all findings
void diagnostics_run(diagnostic_report_t *report);

// Print diagnostic report to stdout
// Uses colored output to highlight issues (green/yellow/red)
void diagnostics_print_report(const diagnostic_report_t *report);

// Individual diagnostic checks (used internally by diagnostics_run)
void diag_check_cpu_features(diagnostic_report_t *report);
void diag_check_memory(diagnostic_report_t *report);
void diag_check_cache(diagnostic_report_t *report);
void diag_check_gpu(diagnostic_report_t *report);
void diag_check_system_limits(diagnostic_report_t *report);

// Helper: Add a check result to the report
void diag_add_check(
    diagnostic_report_t *report,
    diag_status_t status,
    const char *check_name,
    const char *message,
    const char *recommendation
);

#ifdef __cplusplus
}
#endif

#endif // DIAGNOSTICS_H
