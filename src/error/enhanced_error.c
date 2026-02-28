// src/error/enhanced_error.c
#include "enhanced_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// ANSI color codes
#define CLR_RESET   "\033[0m"
#define CLR_BOLD    "\033[1m"
#define CLR_DIM     "\033[2m"
#define CLR_GREEN   "\033[32m"
#define CLR_YELLOW  "\033[33m"
#define CLR_BLUE    "\033[34m"
#define CLR_CYAN    "\033[36m"
#define CLR_RED     "\033[31m"
#define CLR_MAGENTA "\033[35m"

static bool g_colors_enabled = true;

void error_init(bool enable_colors) {
    g_colors_enabled = enable_colors;
}

// Helper: Get category name
static const char* get_category_name(error_category_t category) {
    switch (category) {
        case ERROR_CAT_MEMORY:    return "Memory";
        case ERROR_CAT_GPU:       return "GPU";
        case ERROR_CAT_FILE:      return "File I/O";
        case ERROR_CAT_PARAMETER: return "Parameter";
        case ERROR_CAT_SYSTEM:    return "System";
        case ERROR_CAT_CRYPTO:    return "Crypto";
        case ERROR_CAT_NETWORK:   return "Network";
        default:                  return "Unknown";
    }
}

// Helper: Format memory size
static void format_memory_size(uint64_t bytes, char *buf, size_t size) {
    if (bytes >= (1ULL << 40)) {
        snprintf(buf, size, "%.2f TB", bytes / (double)(1ULL << 40));
    } else if (bytes >= (1ULL << 30)) {
        snprintf(buf, size, "%.2f GB", bytes / (double)(1ULL << 30));
    } else if (bytes >= (1ULL << 20)) {
        snprintf(buf, size, "%.2f MB", bytes / (double)(1ULL << 20));
    } else if (bytes >= (1ULL << 10)) {
        snprintf(buf, size, "%.2f KB", bytes / (double)(1ULL << 10));
    } else {
        snprintf(buf, size, "%llu bytes", (unsigned long long)bytes);
    }
}

void error_report(const error_context_t *ctx, error_report_t *report) {
    // Build main error message
    snprintf(report->message, sizeof(report->message),
             "%s error: %s",
             get_category_name(ctx->category),
             ctx->details);

    // Build technical details
    if (ctx->value1 != 0 || ctx->value2 != 0) {
        snprintf(report->technical, sizeof(report->technical),
                 "Operation: %s | Values: %llu, %llu | Location: %s:%d",
                 ctx->operation,
                 (unsigned long long)ctx->value1,
                 (unsigned long long)ctx->value2,
                 ctx->file, ctx->line);
    } else {
        snprintf(report->technical, sizeof(report->technical),
                 "Operation: %s | Location: %s:%d",
                 ctx->operation, ctx->file, ctx->line);
    }

    // Build resolution based on category
    report->resolution[0] = '\0';
    report->command[0] = '\0';

    switch (ctx->category) {
        case ERROR_CAT_MEMORY:
            if (ctx->value1 > ctx->value2) {
                // Required > Available
                snprintf(report->resolution, sizeof(report->resolution),
                         "Reduce memory usage by:\n"
                         "  • Using a smaller -n value (BSGS mode)\n"
                         "  • Reducing -k factor\n"
                         "  • Closing other applications\n"
                         "  • Adding swap space");
                snprintf(report->command, sizeof(report->command),
                         "./keyhunt --diagnose");
            } else {
                snprintf(report->resolution, sizeof(report->resolution),
                         "Check available memory and system limits");
            }
            break;

        case ERROR_CAT_GPU:
            snprintf(report->resolution, sizeof(report->resolution),
                     "GPU troubleshooting:\n"
                     "  • Update GPU drivers to latest version\n"
                     "  • Verify CUDA toolkit is installed\n"
                     "  • Check GPU is not in use by other processes\n"
                     "  • Try running without GPU: omit -g flag");
            snprintf(report->command, sizeof(report->command),
                     "nvidia-smi");
            break;

        case ERROR_CAT_FILE:
            snprintf(report->resolution, sizeof(report->resolution),
                     "File I/O troubleshooting:\n"
                     "  • Verify file exists and path is correct\n"
                     "  • Check file permissions (read/write access)\n"
                     "  • Ensure sufficient disk space\n"
                     "  • Check file is not locked by another process");
            break;

        case ERROR_CAT_PARAMETER:
            snprintf(report->resolution, sizeof(report->resolution),
                     "Parameter validation failed:\n"
                     "  • Check parameter syntax and ranges\n"
                     "  • Use --help to see valid options\n"
                     "  • Try --diagnose to check system capabilities");
            snprintf(report->command, sizeof(report->command),
                     "./keyhunt --help");
            break;

        case ERROR_CAT_SYSTEM:
            snprintf(report->resolution, sizeof(report->resolution),
                     "System resource troubleshooting:\n"
                     "  • Reduce thread count with -t flag\n"
                     "  • Check system limits: ulimit -a\n"
                     "  • Close other resource-intensive applications\n"
                     "  • Verify system is not overloaded");
            break;

        case ERROR_CAT_CRYPTO:
            snprintf(report->resolution, sizeof(report->resolution),
                     "Cryptographic operation failed:\n"
                     "  • Verify input data format\n"
                     "  • Check for data corruption\n"
                     "  • Ensure valid key ranges");
            break;

        case ERROR_CAT_NETWORK:
            snprintf(report->resolution, sizeof(report->resolution),
                     "Network troubleshooting:\n"
                     "  • Check network connectivity\n"
                     "  • Verify firewall settings\n"
                     "  • Confirm server is accessible\n"
                     "  • Check for proxy configuration issues");
            break;
    }
}

void error_print(const error_report_t *report) {
    const char *color = g_colors_enabled ? CLR_RED : "";
    const char *reset = g_colors_enabled ? CLR_RESET : "";
    const char *bold = g_colors_enabled ? CLR_BOLD : "";
    const char *dim = g_colors_enabled ? CLR_DIM : "";

    fprintf(stderr, "\n");
    fprintf(stderr, "%s%s ERROR %s%s\n", color, bold, reset, color);
    fprintf(stderr, "%s%s\n", report->message, reset);

    if (report->resolution[0] != '\0') {
        fprintf(stderr, "\n%sResolution:%s\n", bold, reset);
        fprintf(stderr, "%s\n", report->resolution);
    }

    if (report->command[0] != '\0') {
        fprintf(stderr, "\n%sHelpful command:%s\n", bold, reset);
        fprintf(stderr, "  %s\n", report->command);
    }

    fprintf(stderr, "\n%s%s%s\n", dim, report->technical, reset);
    fprintf(stderr, "\n");
}

void error_fatal(const error_report_t *report) {
    error_print(report);
    exit(EXIT_FAILURE);
}

void error_memory_allocation(uint64_t required_mb, uint64_t available_mb,
                             const char *context, error_report_t *report) {
    char req_str[64], avail_str[64];
    format_memory_size(required_mb * 1024ULL * 1024ULL, req_str, sizeof(req_str));
    format_memory_size(available_mb * 1024ULL * 1024ULL, avail_str, sizeof(avail_str));

    snprintf(report->message, sizeof(report->message),
             "Memory allocation failed for %s: required %s, available %s",
             context, req_str, avail_str);

    uint64_t deficit = (required_mb > available_mb) ? (required_mb - available_mb) : 0;
    snprintf(report->technical, sizeof(report->technical),
             "Required: %llu MB | Available: %llu MB | Deficit: %llu MB",
             (unsigned long long)required_mb,
             (unsigned long long)available_mb,
             (unsigned long long)deficit);

    double percent_deficit = (required_mb > 0 && deficit > 0)
        ? ((double)deficit / required_mb) * 100.0
        : 0.0;

    if (percent_deficit > 50.0) {
        snprintf(report->resolution, sizeof(report->resolution),
                 "Memory requirement exceeds available by %.0f%%:\n"
                 "  • Significantly reduce -n value (try halving it)\n"
                 "  • Reduce -k factor to 1 or 2\n"
                 "  • Close all other applications\n"
                 "  • Add more RAM or swap space\n"
                 "  • Use a machine with more memory",
                 percent_deficit);
    } else {
        snprintf(report->resolution, sizeof(report->resolution),
                 "Memory requirement slightly exceeds available:\n"
                 "  • Reduce -n value by 10-20%%\n"
                 "  • Close background applications\n"
                 "  • Try again when system has more free memory");
    }

    snprintf(report->command, sizeof(report->command),
             "./keyhunt --diagnose");
}

void error_oom(const char *context, error_report_t *report) {
    snprintf(report->message, sizeof(report->message),
             "Out of memory: %s", context);

    snprintf(report->technical, sizeof(report->technical),
             "System malloc() returned NULL for: %s", context);

    snprintf(report->resolution, sizeof(report->resolution),
             "System out of memory:\n"
             "  • Close other applications to free memory\n"
             "  • Reduce keyhunt memory usage (-n or -k parameters)\n"
             "  • Check for memory leaks (run --diagnose)\n"
             "  • Add swap space or more physical RAM\n"
             "  • Check system logs: dmesg | grep -i oom");

    snprintf(report->command, sizeof(report->command),
             "free -h");
}

void error_gpu_init_failed(const char *gpu_name, const char *details,
                           error_report_t *report) {
    snprintf(report->message, sizeof(report->message),
             "GPU initialization failed: %s", details);

    snprintf(report->technical, sizeof(report->technical),
             "GPU: %s | Details: %s",
             gpu_name ? gpu_name : "Unknown", details);

    snprintf(report->resolution, sizeof(report->resolution),
             "GPU initialization failed:\n"
             "  • Check GPU is properly connected\n"
             "  • Update NVIDIA drivers: sudo apt install nvidia-driver-XXX\n"
             "  • Verify CUDA toolkit is installed\n"
             "  • Check GPU is detected: nvidia-smi\n"
             "  • Try CPU-only mode (omit -g flag)");

    snprintf(report->command, sizeof(report->command),
             "nvidia-smi");
}

void error_cuda_error(int cuda_error_code, const char *operation,
                     const char *cuda_version, const char *driver_version,
                     error_report_t *report) {
    snprintf(report->message, sizeof(report->message),
             "CUDA error %d during %s", cuda_error_code, operation);

    snprintf(report->technical, sizeof(report->technical),
             "CUDA Error: %d | Operation: %s | CUDA: %s | Driver: %s",
             cuda_error_code, operation,
             cuda_version ? cuda_version : "Unknown",
             driver_version ? driver_version : "Unknown");

    // Common CUDA error codes
    const char *error_hint = "";
    switch (cuda_error_code) {
        case 2:  error_hint = "Out of memory - reduce GPU memory usage"; break;
        case 3:  error_hint = "Driver not initialized - reinstall NVIDIA driver"; break;
        case 35: error_hint = "Driver/CUDA version mismatch - update driver"; break;
        case 46: error_hint = "No CUDA-capable device - check GPU detection"; break;
        default: error_hint = "Check CUDA error code reference"; break;
    }

    snprintf(report->resolution, sizeof(report->resolution),
             "CUDA error %d: %s\n"
             "  • Update NVIDIA drivers to match CUDA version\n"
             "  • Verify GPU is not overheating\n"
             "  • Check GPU memory with: nvidia-smi\n"
             "  • Try CPU-only mode (omit -g flag)\n"
             "  • Consult CUDA error reference online",
             cuda_error_code, error_hint);

    snprintf(report->command, sizeof(report->command),
             "nvidia-smi");
}

void error_gpu_oom(uint64_t required_mb, uint64_t available_mb,
                  const char *gpu_name, error_report_t *report) {
    char req_str[64], avail_str[64];
    format_memory_size(required_mb * 1024ULL * 1024ULL, req_str, sizeof(req_str));
    format_memory_size(available_mb * 1024ULL * 1024ULL, avail_str, sizeof(avail_str));

    snprintf(report->message, sizeof(report->message),
             "GPU out of memory on %s: required %s, available %s",
             gpu_name ? gpu_name : "Unknown GPU", req_str, avail_str);

    snprintf(report->technical, sizeof(report->technical),
             "GPU: %s | Required: %llu MB | Available: %llu MB",
             gpu_name ? gpu_name : "Unknown",
             (unsigned long long)required_mb,
             (unsigned long long)available_mb);

    snprintf(report->resolution, sizeof(report->resolution),
             "GPU memory insufficient:\n"
             "  • Reduce GPU work size (-g parameter)\n"
             "  • Close other GPU applications\n"
             "  • Check GPU memory: nvidia-smi\n"
             "  • Use CPU mode instead (omit -g flag)\n"
             "  • Try a GPU with more VRAM");

    snprintf(report->command, sizeof(report->command),
             "nvidia-smi");
}

void error_file_io(const char *filename, const char *operation,
                  const char *system_error, error_report_t *report) {
    snprintf(report->message, sizeof(report->message),
             "File I/O error during %s: %s", operation, filename);

    snprintf(report->technical, sizeof(report->technical),
             "File: %s | Operation: %s | Error: %s",
             filename, operation, system_error ? system_error : "Unknown");

    snprintf(report->resolution, sizeof(report->resolution),
             "File I/O error:\n"
             "  • Verify file path is correct\n"
             "  • Check file permissions: ls -l '%s'\n"
             "  • Ensure directory exists\n"
             "  • Check disk space: df -h\n"
             "  • Verify file is not locked by another process",
             filename);

    snprintf(report->command, sizeof(report->command),
             "ls -l '%s'", filename);
}

void error_invalid_parameter(const char *param_name, const char *value,
                             const char *expected, error_report_t *report) {
    snprintf(report->message, sizeof(report->message),
             "Invalid parameter '%s': got '%s', expected %s",
             param_name, value, expected);

    snprintf(report->technical, sizeof(report->technical),
             "Parameter: %s | Value: %s | Expected: %s",
             param_name, value, expected);

    snprintf(report->resolution, sizeof(report->resolution),
             "Invalid parameter value:\n"
             "  • Check parameter syntax\n"
             "  • See valid ranges with --help\n"
             "  • Run --diagnose to check system capabilities\n"
             "  • Refer to documentation for examples");

    snprintf(report->command, sizeof(report->command),
             "./keyhunt --help");
}

void error_system_resource(const char *resource, uint64_t requested,
                           uint64_t available, error_report_t *report) {
    snprintf(report->message, sizeof(report->message),
             "Insufficient %s: requested %llu, available %llu",
             resource,
             (unsigned long long)requested,
             (unsigned long long)available);

    snprintf(report->technical, sizeof(report->technical),
             "Resource: %s | Requested: %llu | Available: %llu",
             resource,
             (unsigned long long)requested,
             (unsigned long long)available);

    snprintf(report->resolution, sizeof(report->resolution),
             "System resource limit reached:\n"
             "  • Reduce resource usage (e.g., -t for threads)\n"
             "  • Check system limits: ulimit -a\n"
             "  • Close other resource-intensive applications\n"
             "  • Increase system limits if necessary");

    snprintf(report->command, sizeof(report->command),
             "ulimit -a");
}

void error_suggest_memory_fix(const char *mode, uint64_t n_value, int k_factor,
                              uint64_t available_mb, char *suggestion, size_t size) {
    // Calculate current memory requirement for BSGS
    if (strcmp(mode, "bsgs") == 0 && n_value > 0) {
        uint64_t m = (uint64_t)sqrt((double)n_value);
        uint64_t current_mb = (m * k_factor * 35) / 10 / (1024 * 1024);

        // Calculate safe N value that fits in 60% of available RAM
        uint64_t safe_ram_mb = (available_mb * 60) / 100;
        uint64_t safe_entries = (safe_ram_mb * 1024ULL * 1024ULL * 10ULL) / (35ULL * k_factor);
        uint64_t safe_m = (uint64_t)sqrt((double)safe_entries);
        uint64_t safe_n = safe_m * safe_m;

        snprintf(suggestion, size,
                 "BSGS memory optimization:\n"
                 "  Current: -n 0x%llx -k %d (requires ~%llu MB)\n"
                 "  Suggested: -n 0x%llx -k %d (requires ~%llu MB)\n"
                 "  Alternative: Reduce -k to 1 or 2\n"
                 "  Available RAM: %llu MB",
                 (unsigned long long)n_value, k_factor, (unsigned long long)current_mb,
                 (unsigned long long)safe_n, k_factor, (unsigned long long)safe_ram_mb,
                 (unsigned long long)available_mb);
    } else {
        snprintf(suggestion, size,
                 "Memory optimization suggestions:\n"
                 "  • Close background applications\n"
                 "  • Run --diagnose to see detailed memory analysis\n"
                 "  • Reduce batch size or other memory parameters\n"
                 "  Available RAM: %llu MB",
                 (unsigned long long)available_mb);
    }
}
