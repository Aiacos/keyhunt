// src/error/enhanced_error.h
#ifndef ENHANCED_ERROR_H
#define ENHANCED_ERROR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Error categories
typedef enum {
    ERROR_CAT_MEMORY = 0,      // Memory allocation failures
    ERROR_CAT_GPU = 1,         // GPU/CUDA errors
    ERROR_CAT_FILE = 2,        // File I/O errors
    ERROR_CAT_PARAMETER = 3,   // Invalid parameters
    ERROR_CAT_SYSTEM = 4,      // System resource errors
    ERROR_CAT_CRYPTO = 5,      // Cryptographic operation errors
    ERROR_CAT_NETWORK = 6      // Network/communication errors
} error_category_t;

// Error severity levels
typedef enum {
    ERROR_SEV_INFO = 0,        // Informational (not actually an error)
    ERROR_SEV_WARNING = 1,     // Warning (can continue)
    ERROR_SEV_ERROR = 2,       // Error (operation failed)
    ERROR_SEV_FATAL = 3        // Fatal (must exit)
} error_severity_t;

// Error context structure
typedef struct {
    error_category_t category;
    error_severity_t severity;
    const char *operation;     // What operation failed (e.g., "bloom filter allocation")
    const char *details;       // Technical details
    uint64_t value1;           // Context-specific value (e.g., required memory)
    uint64_t value2;           // Context-specific value (e.g., available memory)
    const char *file;          // Source file where error occurred
    int line;                  // Line number
} error_context_t;

// Error resolution suggestion
typedef struct {
    char message[512];         // Main error message
    char resolution[512];      // Suggested resolution
    char technical[256];       // Technical details for debugging
    char command[128];         // Example command to try (if applicable)
} error_report_t;

// Initialize error system
void error_init(bool enable_colors);

// Report error with context and get formatted message
void error_report(const error_context_t *ctx, error_report_t *report);

// Print error report to stderr
void error_print(const error_report_t *report);

// Print error report to stderr and exit
void error_fatal(const error_report_t *report) __attribute__((noreturn));

// Convenience macros for error reporting
#define ERROR_CONTEXT(cat, sev, op, det) \
    ((error_context_t){ \
        .category = (cat), \
        .severity = (sev), \
        .operation = (op), \
        .details = (det), \
        .value1 = 0, \
        .value2 = 0, \
        .file = __FILE__, \
        .line = __LINE__ \
    })

#define ERROR_CONTEXT_VALUES(cat, sev, op, det, v1, v2) \
    ((error_context_t){ \
        .category = (cat), \
        .severity = (sev), \
        .operation = (op), \
        .details = (det), \
        .value1 = (v1), \
        .value2 = (v2), \
        .file = __FILE__, \
        .line = __LINE__ \
    })

// Specialized error reporting functions

// Memory allocation error with available vs required
void error_memory_allocation(uint64_t required_mb, uint64_t available_mb,
                             const char *context, error_report_t *report);

// Out of memory error with suggestions
void error_oom(const char *context, error_report_t *report);

// GPU initialization error
void error_gpu_init_failed(const char *gpu_name, const char *details,
                           error_report_t *report);

// CUDA error with driver/version info
void error_cuda_error(int cuda_error_code, const char *operation,
                     const char *cuda_version, const char *driver_version,
                     error_report_t *report);

// GPU out of memory error
void error_gpu_oom(uint64_t required_mb, uint64_t available_mb,
                  const char *gpu_name, error_report_t *report);

// File I/O error
void error_file_io(const char *filename, const char *operation,
                  const char *system_error, error_report_t *report);

// Invalid parameter error
void error_invalid_parameter(const char *param_name, const char *value,
                             const char *expected, error_report_t *report);

// System resource error (threads, file descriptors, etc.)
void error_system_resource(const char *resource, uint64_t requested,
                           uint64_t available, error_report_t *report);

// Suggest memory fix based on mode and parameters
void error_suggest_memory_fix(const char *mode, uint64_t n_value, int k_factor,
                              uint64_t available_mb, char *suggestion, size_t size);

#ifdef __cplusplus
}
#endif

#endif // ENHANCED_ERROR_H
