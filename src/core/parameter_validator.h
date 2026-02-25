#ifndef PARAMETER_VALIDATOR_H
#define PARAMETER_VALIDATOR_H

#include <stdint.h>
#include <stdbool.h>
#include "sysinfo.h"

#ifdef __cplusplus
extern "C" {
#endif

// Validation result flags
#define PARAM_OK                 0  // Parameter is optimal
#define PARAM_SUBOPTIMAL         1  // Parameter works but isn't optimal
#define PARAM_CORRECTED          2  // Parameter was auto-corrected
#define PARAM_WARNING            3  // Parameter may cause issues

// Validation result structure
typedef struct {
    int status;                 // One of PARAM_* flags above
    int original_value;         // Original user-specified value
    int suggested_value;        // Recommended value based on hardware
    int corrected_value;        // Auto-corrected value (if applied)
    char message[512];          // Human-readable explanation
    bool applied_correction;    // Whether auto-correction was applied
} param_validation_result_t;

// Thread count validation and optimization
// Returns: corrected thread count (may differ from input if invalid)
int validate_threads(
    int user_threads,
    const system_info_t *sysinfo,
    param_validation_result_t *result
);

// N value validation for BSGS mode
// Returns: corrected N value (may differ from input if invalid/unsafe)
uint64_t validate_n_value(
    uint64_t user_n,
    int kfactor,
    const system_info_t *sysinfo,
    param_validation_result_t *result
);

// K factor validation for BSGS mode
// Returns: corrected K factor (may differ from input if invalid/unsafe)
int validate_kfactor(
    int user_kfactor,
    uint64_t n_value,
    const system_info_t *sysinfo,
    param_validation_result_t *result
);

// Validate batch size against cache constraints
uint32_t validate_batch_size(
    uint32_t user_batch_size,
    const system_info_t *sysinfo,
    param_validation_result_t *result
);

// GPU parameter validation functions
// Forward declaration (gpu_backend.h types)
struct gpu_backend_info_t;

// Validate blocks per SM (streaming multiprocessor)
// Returns: corrected blocks_per_sm value (may differ from input if invalid)
int validate_blocks_per_sm(
    int user_blocks_per_sm,
    const struct gpu_backend_info_t *gpu_info,
    param_validation_result_t *result
);

// Validate threads per block for GPU kernels
// Returns: corrected threads_per_block value (may differ from input if invalid)
int validate_threads_per_block(
    int user_threads_per_block,
    const struct gpu_backend_info_t *gpu_info,
    param_validation_result_t *result
);

// Validate keys per thread for GPU kernels
// Returns: corrected keys_per_thread value (may differ from input if invalid)
int validate_keys_per_thread(
    int user_keys_per_thread,
    const struct gpu_backend_info_t *gpu_info,
    param_validation_result_t *result
);

// Print validation result to user (with colors if supported)
void print_validation_result(const param_validation_result_t *result, const char *param_name);

// Comprehensive parameter validation (all at once)
// Returns: true if all parameters are safe to use
bool validate_all_parameters(
    int *threads,           // in/out: may be corrected
    uint64_t *n_value,      // in/out: may be corrected
    int *kfactor,           // in/out: may be corrected
    uint32_t *batch_size,   // in/out: may be corrected
    const system_info_t *sysinfo,
    bool auto_correct       // if true, apply corrections automatically
);

// Calculate recommended BSGS parameters for target memory usage
void recommend_bsgs_params(
    const system_info_t *sysinfo,
    uint64_t target_ram_mb,     // Target memory usage in MB (0 = auto)
    uint64_t *out_n,            // Output: recommended N value
    int *out_kfactor            // Output: recommended K factor
);

#ifdef __cplusplus
}
#endif

#endif // PARAMETER_VALIDATOR_H
