#include "sysinfo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <dirent.h>
#include <strings.h>

#ifdef __linux__
#include <sys/sysinfo.h>
#include <dlfcn.h>
#endif

// Helper function to read integer from file
static long read_long_from_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    long value = -1;
    if (fscanf(f, "%ld", &value) != 1) {
        value = -1;
    }
    fclose(f);
    return value;
}

// Helper function to read string from file
static int read_string_from_file(const char *path, char *buffer, size_t size) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    if (fgets(buffer, size, f) == NULL) {
        fclose(f);
        return 0;
    }
    fclose(f);

    // Remove trailing newline
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len-1] == '\n') {
        buffer[len-1] = '\0';
    }
    return 1;
}

// Detect physical CPU cores (without hyperthreading)
static int detect_physical_cores(void) {
    int physical_cores = 0;

#ifdef __linux__
    // Method 1: Read from /sys/devices/system/cpu/cpu*/topology/thread_siblings_list
    // to count unique physical cores
    DIR *dir = opendir("/sys/devices/system/cpu");
    if (dir) {
        struct dirent *entry;
        int max_core_id = -1;

        while ((entry = readdir(dir)) != NULL) {
            if (strncmp(entry->d_name, "cpu", 3) == 0 && isdigit(entry->d_name[3])) {
                char path[512];
                snprintf(path, sizeof(path),
                         "/sys/devices/system/cpu/%s/topology/core_id",
                         entry->d_name);

                long core_id = read_long_from_file(path);
                if (core_id > max_core_id) {
                    max_core_id = core_id;
                }
            }
        }
        closedir(dir);

        if (max_core_id >= 0) {
            physical_cores = max_core_id + 1;
        }
    }

    // Fallback: parse /proc/cpuinfo
    if (physical_cores == 0) {
        FILE *f = fopen("/proc/cpuinfo", "r");
        if (f) {
            char line[256];
            int core_ids[1024] = {0};
            int max_id = -1;

            while (fgets(line, sizeof(line), f)) {
                if (strncmp(line, "core id", 7) == 0) {
                    char *colon = strchr(line, ':');
                    if (colon) {
                        int core_id = atoi(colon + 1);
                        if (core_id >= 0 && core_id < 1024) {
                            core_ids[core_id] = 1;
                            if (core_id > max_id) max_id = core_id;
                        }
                    }
                }
            }
            fclose(f);

            // Count unique core IDs
            for (int i = 0; i <= max_id; i++) {
                if (core_ids[i]) physical_cores++;
            }
        }
    }
#endif

    // Final fallback
    if (physical_cores == 0) {
        physical_cores = sysconf(_SC_NPROCESSORS_ONLN);
        // Assume hyperthreading (divide by 2)
        if (physical_cores > 2) {
            physical_cores = (physical_cores + 1) / 2;
        }
    }

    return physical_cores > 0 ? physical_cores : 1;
}

// Detect logical CPU cores
static int detect_logical_cores(void) {
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    return cores > 0 ? (int)cores : 1;
}

// Detect cache sizes
static void detect_cache_sizes(system_info_t *info) {
    info->cache_l1_size = 0;
    info->cache_l2_size = 0;
    info->cache_l3_size = 0;

#ifdef __linux__
    // Parse size strings from sysfs (they might have K suffix)
    char buf[64];
    if (read_string_from_file("/sys/devices/system/cpu/cpu0/cache/index0/size", buf, sizeof(buf))) {
        info->cache_l1_size = atol(buf);
    }
    if (read_string_from_file("/sys/devices/system/cpu/cpu0/cache/index2/size", buf, sizeof(buf))) {
        info->cache_l2_size = atol(buf);
    }
    if (read_string_from_file("/sys/devices/system/cpu/cpu0/cache/index3/size", buf, sizeof(buf))) {
        info->cache_l3_size = atol(buf);
    }
#endif

    // Fallback to sysconf
    if (info->cache_l1_size == 0) {
        long l1 = sysconf(_SC_LEVEL1_DCACHE_SIZE);
        if (l1 > 0) info->cache_l1_size = l1 / 1024; // Convert to KB
    }
    if (info->cache_l2_size == 0) {
        long l2 = sysconf(_SC_LEVEL2_CACHE_SIZE);
        if (l2 > 0) info->cache_l2_size = l2 / 1024;
    }
    if (info->cache_l3_size == 0) {
        long l3 = sysconf(_SC_LEVEL3_CACHE_SIZE);
        if (l3 > 0) info->cache_l3_size = l3 / 1024;
    }

    // Reasonable defaults if detection failed
    if (info->cache_l1_size == 0) info->cache_l1_size = 32;   // 32 KB
    if (info->cache_l2_size == 0) info->cache_l2_size = 256;  // 256 KB
    if (info->cache_l3_size == 0) info->cache_l3_size = 8192; // 8 MB
}

// Detect RAM information
static void detect_memory(system_info_t *info) {
    info->ram_total = 0;
    info->ram_available = 0;
    info->ram_free = 0;

#ifdef __linux__
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        info->ram_total = (si.totalram * si.mem_unit) / (1024 * 1024);  // Convert to MB
        info->ram_free = (si.freeram * si.mem_unit) / (1024 * 1024);
    }

    // Get more accurate available memory from /proc/meminfo
    FILE *f = fopen("/proc/meminfo", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "MemAvailable:", 13) == 0) {
                unsigned long available_kb;
                if (sscanf(line + 13, "%lu", &available_kb) == 1) {
                    info->ram_available = available_kb / 1024; // Convert to MB
                }
                break;
            }
        }
        fclose(f);
    }
#endif

    // Fallback
    if (info->ram_total == 0) {
        long pages = sysconf(_SC_PHYS_PAGES);
        long page_size = sysconf(_SC_PAGE_SIZE);
        if (pages > 0 && page_size > 0) {
            info->ram_total = (pages * page_size) / (1024 * 1024);
        }
    }

    if (info->ram_available == 0) {
        info->ram_available = info->ram_free;
    }
}

// Detect CPU features (AVX2, AVX-512, SHA-NI)
// Note: /proc/cpuinfo "flags" lines can exceed small fixed buffers.
static bool token_present(const char *line, const char *token) {
    const size_t token_len = strlen(token);
    const char *p = line;
    while ((p = strcasestr(p, token)) != NULL) {
        bool left_ok = (p == line) || isspace((unsigned char)p[-1]) || p[-1] == ':';
        bool right_ok = isspace((unsigned char)p[token_len]) || p[token_len] == '\0';
        if (left_ok && right_ok) return true;
        p += token_len;
    }
    return false;
}

static void detect_cpu_features(system_info_t *info) {
    info->has_avx2 = false;
    info->has_avx512 = false;
    info->has_sha_ni = false;

#if (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
    /* Prefer compiler-provided runtime detection on x86: it accounts for OS
     * support (XSAVE/XGETBV) and avoids false positives that can crash when
     * executing AVX/AVX-512 instructions. */
    __builtin_cpu_init();
    info->has_avx2 = __builtin_cpu_supports("avx2");
    info->has_avx512 = __builtin_cpu_supports("avx512f");
    /* SHA-NI doesn't require OS extended state; detect via /proc/cpuinfo below
     * for maximum compatibility across compiler versions. */
    bool used_builtin = true;
#else
    bool used_builtin = false;
#endif

#ifdef __linux__
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f) return;

    char *line = NULL;
    size_t cap = 0;
    while (getline(&line, &cap, f) != -1) {
        if (strncmp(line, "flags", 5) == 0 || strncmp(line, "Features", 8) == 0) {
            if (!used_builtin) {
                if (token_present(line, "avx2")) info->has_avx2 = true;
                if (token_present(line, "avx512f")) info->has_avx512 = true;
            }
            // Intel uses "sha_ni", some platforms report "sha".
            if (token_present(line, "sha_ni") || token_present(line, "sha")) info->has_sha_ni = true;
        }
    }
    free(line);
    fclose(f);
#endif

#ifndef __linux__
    (void)used_builtin;
#endif
}

// Detect NVIDIA GPU via NVML (if available) to get count/name/VRAM
static void detect_nvidia_gpu_nvml(system_info_t *info) {
#ifdef __linux__
    typedef int nvmlReturn_t;
    typedef struct nvmlDevice_st *nvmlDevice_t;
    typedef struct nvmlMemory_st {
        unsigned long long total;
        unsigned long long free;
        unsigned long long used;
    } nvmlMemory_t;

    void *lib = dlopen("libnvidia-ml.so.1", RTLD_LAZY);
    if (!lib) {
        lib = dlopen("libnvidia-ml.so", RTLD_LAZY);
    }
    if (!lib) return;

    nvmlReturn_t (*nvmlInit_v2)(void) =
        (nvmlReturn_t (*)(void))dlsym(lib, "nvmlInit_v2");
    nvmlReturn_t (*nvmlInit)(void) =
        (nvmlReturn_t (*)(void))dlsym(lib, "nvmlInit");
    nvmlReturn_t (*nvmlShutdown)(void) =
        (nvmlReturn_t (*)(void))dlsym(lib, "nvmlShutdown");

    nvmlReturn_t (*nvmlDeviceGetCount_v2)(unsigned int *) =
        (nvmlReturn_t (*)(unsigned int *))dlsym(lib, "nvmlDeviceGetCount_v2");
    nvmlReturn_t (*nvmlDeviceGetCount)(unsigned int *) =
        (nvmlReturn_t (*)(unsigned int *))dlsym(lib, "nvmlDeviceGetCount");

    nvmlReturn_t (*nvmlDeviceGetHandleByIndex_v2)(unsigned int, nvmlDevice_t *) =
        (nvmlReturn_t (*)(unsigned int, nvmlDevice_t *))dlsym(lib, "nvmlDeviceGetHandleByIndex_v2");
    nvmlReturn_t (*nvmlDeviceGetHandleByIndex)(unsigned int, nvmlDevice_t *) =
        (nvmlReturn_t (*)(unsigned int, nvmlDevice_t *))dlsym(lib, "nvmlDeviceGetHandleByIndex");

    nvmlReturn_t (*nvmlDeviceGetName)(nvmlDevice_t, char *, unsigned int) =
        (nvmlReturn_t (*)(nvmlDevice_t, char *, unsigned int))dlsym(lib, "nvmlDeviceGetName");
    nvmlReturn_t (*nvmlDeviceGetMemoryInfo)(nvmlDevice_t, nvmlMemory_t *) =
        (nvmlReturn_t (*)(nvmlDevice_t, nvmlMemory_t *))dlsym(lib, "nvmlDeviceGetMemoryInfo");

    if ((!nvmlInit_v2 && !nvmlInit) ||
        (!nvmlDeviceGetCount_v2 && !nvmlDeviceGetCount) ||
        (!nvmlDeviceGetHandleByIndex_v2 && !nvmlDeviceGetHandleByIndex)) {
        dlclose(lib);
        return;
    }

    nvmlReturn_t init_rc = nvmlInit_v2 ? nvmlInit_v2() : nvmlInit();
    if (init_rc != 0) {
        dlclose(lib);
        return;
    }

    unsigned int count = 0;
    nvmlReturn_t count_rc = nvmlDeviceGetCount_v2
        ? nvmlDeviceGetCount_v2(&count)
        : nvmlDeviceGetCount(&count);

    if (count_rc != 0 || count == 0) {
        if (nvmlShutdown) nvmlShutdown();
        dlclose(lib);
        return;
    }

    info->gpu_count = (int)count;
    info->has_nvidia = true;
    info->has_cuda = true;

    nvmlDevice_t dev;
    nvmlReturn_t handle_rc = nvmlDeviceGetHandleByIndex_v2
        ? nvmlDeviceGetHandleByIndex_v2(0, &dev)
        : nvmlDeviceGetHandleByIndex(0, &dev);

    if (handle_rc == 0) {
        if (nvmlDeviceGetName) {
            char name[128] = {0};
            if (nvmlDeviceGetName(dev, name, (unsigned int)sizeof(name)) == 0) {
                strncpy(info->gpu_name, name, sizeof(info->gpu_name) - 1);
                info->gpu_name[sizeof(info->gpu_name) - 1] = '\0';
            }
        }
        if (nvmlDeviceGetMemoryInfo) {
            nvmlMemory_t mem;
            if (nvmlDeviceGetMemoryInfo(dev, &mem) == 0) {
                info->gpu_vram_mb = (uint64_t)(mem.total / (1024ULL * 1024ULL));
            }
        }
    }

    if (nvmlShutdown) nvmlShutdown();
    dlclose(lib);
#else
    (void)info;
#endif
}

// Detect NVIDIA GPU via /proc/driver (fallback for systems without NVML)
static void detect_nvidia_gpu_proc(system_info_t *info) {
#ifdef __linux__
    DIR *dir = opendir("/proc/driver/nvidia/gpus");
    if (!dir) return;

    struct dirent *entry;
    int count = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        count++;
        if (count == 1 && info->gpu_name[0] == '\0') {
            char path[512];
            snprintf(path, sizeof(path),
                     "/proc/driver/nvidia/gpus/%s/information",
                     entry->d_name);
            FILE *f = fopen(path, "r");
            if (f) {
                char line[256];
                while (fgets(line, sizeof(line), f)) {
                    if (strncmp(line, "Model:", 6) == 0) {
                        char *colon = strchr(line, ':');
                        if (colon) {
                            colon++;
                            while (*colon && isspace((unsigned char)*colon)) colon++;
                            strncpy(info->gpu_name, colon, sizeof(info->gpu_name) - 1);
                            info->gpu_name[sizeof(info->gpu_name) - 1] = '\0';
                            size_t len = strlen(info->gpu_name);
                            while (len > 0 && isspace((unsigned char)info->gpu_name[len - 1])) {
                                info->gpu_name[--len] = '\0';
                            }
                        }
                        break;
                    }
                }
                fclose(f);
            }
        }
    }

    closedir(dir);

    if (count > 0) {
        info->gpu_count = count;
        info->has_nvidia = true;
        info->has_cuda = true;
    }
#else
    (void)info;
#endif
}

// Detect GPU (currently NVIDIA/CUDA only)
static void detect_gpu(system_info_t *info) {
    info->gpu_count = 0;
    info->gpu_vram_mb = 0;
    info->gpu_name[0] = '\0';
    info->has_nvidia = false;
    info->has_cuda = false;

    detect_nvidia_gpu_nvml(info);
    if (info->gpu_count == 0) {
        detect_nvidia_gpu_proc(info);
    }
}

// Calculate optimal parameters
static void calculate_optimal_params(system_info_t *info) {
    // Optimal thread count
    // Testing shows that for this workload, hyperthreading is beneficial
    // Use all logical cores for maximum performance
    info->recommended_threads = info->cpu_logical_cores;

    // Ensure at least 1 thread
    if (info->recommended_threads < 1) {
        info->recommended_threads = 1;
    }

    // Batch size optimization
    // NOTE: Keep at proven optimal value of 1024
    // Testing showed 1024 is optimal for most CPUs
    // Larger batches (2048, 4096) can cause cache thrashing
    info->recommended_batch_size = 1024;

    // Workload per thread
    // Base on available memory - reserve ~25% for system
    uint64_t usable_ram_mb = (info->ram_available * 3) / 4;

    // Each workload unit needs memory for:
    // - Points array: batch_size * sizeof(Point) ≈ batch_size * 64
    // - Hash arrays: batch_size * 20 * 3 (compressed02, compressed03, uncompressed)
    // Total per batch: ~120 KB for batch_size=1024

    uint64_t memory_per_batch_kb = (info->recommended_batch_size * 200) / 1024;
    uint64_t max_concurrent_batches = (usable_ram_mb * 1024) / memory_per_batch_kb;

    // Distribute across threads
    info->recommended_workload = max_concurrent_batches / info->recommended_threads;

    // Clamp to reasonable range
    if (info->recommended_workload < 256) info->recommended_workload = 256;
    if (info->recommended_workload > 1048576) info->recommended_workload = 1048576;

    // Prefer power of 2
    uint32_t pow2 = 256;
    while (pow2 < info->recommended_workload && pow2 < 1048576) {
        pow2 *= 2;
    }
    info->recommended_workload = pow2;

    // Calculate optimal N and KFACTOR for BSGS mode
    // N must be a perfect square, and M = sqrt(N) must be divisible by 1024
    // BSGS memory formula:
    //   M = sqrt(N)
    //   bloom1_MB = (M * K) * 3.5 / 1024
    //   bloom2_MB = bloom1_MB / 32
    //   bloom3_MB = bloom1_MB / 1024
    //   bP_table_MB = (M / 32 * K) * 16 / (1024 * 1024)
    //   total_MB ≈ bloom1_MB * 1.035 + bP_table_MB

    // Use 80% of usable RAM for BSGS (usable_ram is already 75% of available)
    // This gives ~60% of total available RAM for BSGS
    uint64_t target_ram_mb = (usable_ram_mb * 8) / 10;

    // Try different N values and pick the largest that fits in RAM
    // Candidates: 0x400000000000, 0x100000000000, 0x40000000000, 0x10000000000

    struct {
        uint64_t n;
        uint64_t m;  // sqrt(N)
        int kfactor;
        uint64_t ram_mb;
    } candidates[] = {
        {0x400000000000ULL, 8388608, 4096, 0},  // M*K=34B → ~118 GB
        {0x100000000000ULL, 4194304, 4096, 0},  // M*K=17B → ~60 GB
        {0x40000000000ULL,  2097152, 2048, 0},  // M*K=4.3B → ~15 GB
        {0x10000000000ULL,  1048576, 2048, 0},  // M*K=2.1B → ~7.5 GB
        {0x10000000000ULL,  1048576, 1024, 0},  // M*K=1B → ~3.7 GB
    };

    // Calculate RAM for each candidate
    for (int i = 0; i < 5; i++) {
        uint64_t m = candidates[i].m;
        int k = candidates[i].kfactor;
        // bloom1 size in MB (3.5 bytes per element)
        uint64_t bloom1_mb = (m * k * 35) / (10 * 1024 * 1024);
        // Total: bloom1 + bloom2 (1/32) + bloom3 (1/1024) + bP_table
        uint64_t total_mb = (bloom1_mb * 1035) / 1000 + (m * k * 16) / (32 * 1024 * 1024);
        candidates[i].ram_mb = total_mb;
    }

    // Pick the largest N that fits in target RAM
    // Start from smallest (safe default) and upgrade if larger fits
    info->recommended_n = 0x10000000000ULL;  // Safe default
    info->recommended_kfactor = 1024;

    // Check from largest to smallest, pick first that fits
    for (int i = 0; i < 5; i++) {
        if (candidates[i].ram_mb <= target_ram_mb) {
            // This one fits, it's the largest because we check in order
            info->recommended_n = candidates[i].n;
            info->recommended_kfactor = candidates[i].kfactor;
            break;
        }
    }
}

void sysinfo_init(system_info_t *info) {
    memset(info, 0, sizeof(system_info_t));

    // Detect CPU
    info->cpu_physical_cores = detect_physical_cores();
    info->cpu_logical_cores = detect_logical_cores();
    info->cpu_threads_optimal = info->cpu_physical_cores;

    // Detect cache
    detect_cache_sizes(info);

    // Detect memory
    detect_memory(info);

    // Detect CPU features
    detect_cpu_features(info);

    // Detect GPU
    detect_gpu(info);

    // Calculate optimal parameters
    calculate_optimal_params(info);
}

void sysinfo_print(const system_info_t *info) {
    printf("\n[+] System Configuration Detected:\n");
    printf("    ├─ CPU: %d physical cores, %d logical cores\n",
           info->cpu_physical_cores, info->cpu_logical_cores);
    printf("    ├─ Cache: L1=%lu KB, L2=%lu KB, L3=%lu KB\n",
           info->cache_l1_size, info->cache_l2_size, info->cache_l3_size);
    printf("    ├─ RAM: %lu MB total, %lu MB available\n",
           info->ram_total, info->ram_available);
    if (info->gpu_count > 0) {
        if (info->gpu_vram_mb > 0) {
            printf("    ├─ GPU: %s x%d (%llu MB VRAM)\n",
                   info->gpu_name[0] ? info->gpu_name : "NVIDIA GPU",
                   info->gpu_count,
                   (unsigned long long)info->gpu_vram_mb);
        } else {
            printf("    ├─ GPU: %s x%d\n",
                   info->gpu_name[0] ? info->gpu_name : "NVIDIA GPU",
                   info->gpu_count);
        }
    } else {
        printf("    ├─ GPU: none detected\n");
    }
    printf("    └─ Features: AVX2=%s, AVX-512=%s, SHA-NI=%s\n",
           info->has_avx2 ? "yes" : "no",
           info->has_avx512 ? "yes" : "no",
           info->has_sha_ni ? "yes" : "no");

    printf("\n[+] Auto-Tuned Parameters:\n");
    printf("    ├─ Threads: %d (optimal for CPU)\n", info->recommended_threads);
    printf("    ├─ Batch Size: %u keys (optimized for L3 cache)\n",
           info->recommended_batch_size);
    printf("    ├─ Workload/Thread: %u (based on available RAM)\n",
           info->recommended_workload);
    printf("    ├─ N value: 0x%llx (range coverage)\n",
           (unsigned long long)info->recommended_n);
    printf("    └─ K factor: %d (jump multiplier)\n",
           info->recommended_kfactor);
}

void sysinfo_get_optimal_params(
    const system_info_t *info,
    int *threads,
    uint32_t *batch_size,
    uint32_t *workload_per_thread,
    uint64_t *n_value,
    int *kfactor
) {
    if (threads) *threads = info->recommended_threads;
    if (batch_size) *batch_size = info->recommended_batch_size;
    if (workload_per_thread) *workload_per_thread = info->recommended_workload;
    if (n_value) *n_value = info->recommended_n;
    if (kfactor) *kfactor = info->recommended_kfactor;
}
