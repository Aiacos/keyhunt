# Environment Variable Reference

keyhunt supports the following environment variables for runtime tuning.
These are applied before CLI arguments and can override auto-detected values.

## Boolean Convention

Boolean env vars accept: `1`, `yes`, `true`, `on` (truthy)
and `0`, `no`, `false`, `off`, empty/unset (falsy).

## Variable Reference

### Profiling and Debugging

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `KEYHUNT_PROFILE` | bool | `0` | Enable lightweight internal profiler. Measures time spent in EC ops, hashing, bloom checks, binary search, and file writes. |
| `KEYHUNT_SKIP_SYSINFO` | bool | `0` | Bypass hardware auto-detection. Uses safe defaults: 4 cores, 8 GB RAM, no SIMD. Useful in containers or CI environments. |
| `KEYHUNT_DEBUG` | bool | `0` | Enable debug logging in the distributed worker client (response types, unknown messages). |
| `KEYHUNT_DEBUG_SUBPROCESS` | bool | `0` | Enable debug output for subprocess line reading in wizard client mode. |

### GPU Tuning

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `KEYHUNT_GPU_SELFTEST` | bool | `0` | Run GPU hash160 self-test on startup. Disables GPU if the test fails. |
| `KEYHUNT_HYBRID_GPU_PERCENT` | int (1-99) | auto | Manual override for GPU/CPU range split in hybrid mode. E.g., `80` gives 80% of the range to GPU. Default: auto-tuned based on measured throughput. |
| `KEYHUNT_HYBRID_WORK_STEAL` | bool | `0` | Enable work-stealing instead of static range split in hybrid mode. Requires non-random mode and stride=1. |
| `KEYHUNT_HYBRID_BLOCK_SIZE` | hex/int | `0x100000000` | Block size for work-stealing mode (4 billion keys). Accepts `0x` hex prefix or decimal. Minimum: 1024. |

### CPU Tuning

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `KEYHUNT_CPU_USE_Y` | 0 or 1 | `1` | Whether to compute the Y coordinate when generating addresses. Set to `0` to skip Y computation for slight speedup (only valid when searching compressed keys). |
| `KEYHUNT_HYBRID_CPU_USE_Y` | 0 or 1 | `1` | Same as above, but specifically for CPU threads in hybrid+full GPU mode. |
| `KEYHUNT_CPU_N` | int | auto | Override the maximum number of sequential keys per CPU thread iteration. Default is auto-calculated from range size. |
| `KEYHUNT_HYBRID_CPU_N` | int | auto | Same as above, but for CPU threads in hybrid mode. |

### Internal (Wizard Mode)

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `KEYHUNT_AUTH_TOKEN` | string | none | Internal authentication token passed from the wizard server to subprocess workers. Not intended for manual use. |

## Examples

```bash
# Enable profiling and skip hardware detection
KEYHUNT_PROFILE=1 KEYHUNT_SKIP_SYSINFO=1 ./keyhunt -m address -f targets.txt -b 66

# Force 90% GPU allocation in hybrid mode
KEYHUNT_HYBRID_GPU_PERCENT=90 ./keyhunt -m address -f targets.txt -b 66 --gpu hybrid

# Enable work-stealing with 1B key blocks
KEYHUNT_HYBRID_WORK_STEAL=1 KEYHUNT_HYBRID_BLOCK_SIZE=0x40000000 ./keyhunt -m address -f targets.txt

# Debug distributed worker communication
KEYHUNT_DEBUG=1 ./keyhunt --wizard
```

## Programmatic Access

The `env_overrides_t` struct in `src/config/config.h` provides a typed registry
of all environment variables. Call `kh_env_overrides_init()` to populate it:

```c
env_overrides_t env;
kh_env_overrides_init(&env);
if (env.profile_enabled) { /* ... */ }
```
