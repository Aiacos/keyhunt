#include "sysinfo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../platform/platform.h"

#if PLATFORM_WINDOWS
    /* Windows-specific headers for system information */
    #include <windows.h>
    #include <psapi.h>
    #ifdef _MSC_VER
        #include <intrin.h>  /* For __cpuid and __cpuidex intrinsics (MSVC only) */
    #elif defined(__GNUC__)
        #include <cpuid.h>   /* For __builtin_cpu_init/__builtin_cpu_supports (GCC/MinGW) */
    #endif
#else
    /* POSIX headers */
    #include <unistd.h>
    #include <dirent.h>
    #include <strings.h>
    #ifdef __linux__
        #include <sys/sysinfo.h>
        #include <dlfcn.h>
    #endif
#endif

/* Portable format specifier for uint64_t */
#include <inttypes.h>

#if PLATFORM_POSIX
// Helper function to read integer from file (POSIX only)
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

// Helper function to read string from file (POSIX only)
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
#endif

// Detect physical CPU cores (without hyperthreading)
static int detect_physical_cores(void) {
    int physical_cores = 0;

#if PLATFORM_WINDOWS
    // Use GetLogicalProcessorInformation to count physical cores
    DWORD buffer_size = 0;
    GetLogicalProcessorInformation(NULL, &buffer_size);

    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        SYSTEM_LOGICAL_PROCESSOR_INFORMATION *buffer =
            (SYSTEM_LOGICAL_PROCESSOR_INFORMATION *)malloc(buffer_size);

        if (buffer && GetLogicalProcessorInformation(buffer, &buffer_size)) {
            DWORD count = buffer_size / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);

            for (DWORD i = 0; i < count; i++) {
                if (buffer[i].Relationship == RelationProcessorCore) {
                    physical_cores++;
                }
            }

            free(buffer);
        }
    }
#elif defined(__linux__)
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

#if PLATFORM_POSIX
    // Final fallback for POSIX systems
    if (physical_cores == 0) {
        physical_cores = sysconf(_SC_NPROCESSORS_ONLN);
        // Assume hyperthreading (divide by 2)
        if (physical_cores > 2) {
            physical_cores = (physical_cores + 1) / 2;
        }
    }
#endif

    return physical_cores > 0 ? physical_cores : 1;
}

// Detect logical CPU cores
static int detect_logical_cores(void) {
#if PLATFORM_WINDOWS
    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);
    return (int)sys_info.dwNumberOfProcessors;
#else
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    return cores > 0 ? (int)cores : 1;
#endif
}

// Detect cache sizes
static void detect_cache_sizes(system_info_t *info) {
    info->cache_l1_size = 0;
    info->cache_l2_size = 0;
    info->cache_l3_size = 0;

#if PLATFORM_WINDOWS
    // Use GetLogicalProcessorInformation to get cache info
    DWORD buffer_size = 0;
    GetLogicalProcessorInformation(NULL, &buffer_size);

    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        SYSTEM_LOGICAL_PROCESSOR_INFORMATION *buffer =
            (SYSTEM_LOGICAL_PROCESSOR_INFORMATION *)malloc(buffer_size);

        if (buffer && GetLogicalProcessorInformation(buffer, &buffer_size)) {
            DWORD count = buffer_size / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);

            for (DWORD i = 0; i < count; i++) {
                if (buffer[i].Relationship == RelationCache) {
                    CACHE_DESCRIPTOR cache = buffer[i].Cache;
                    // Convert bytes to KB
                    uint64_t size_kb = cache.Size / 1024;

                    if (cache.Level == 1 && cache.Type == CacheData) {
                        info->cache_l1_size = size_kb;
                    } else if (cache.Level == 2) {
                        info->cache_l2_size = size_kb;
                    } else if (cache.Level == 3) {
                        info->cache_l3_size = size_kb;
                    }
                }
            }

            free(buffer);
        }
    }
#elif defined(__linux__)
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

#if PLATFORM_POSIX
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
#endif

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

#if PLATFORM_WINDOWS
    // Use GlobalMemoryStatusEx for Windows
    MEMORYSTATUSEX memstat;
    memstat.dwLength = sizeof(MEMORYSTATUSEX);

    if (GlobalMemoryStatusEx(&memstat)) {
        // Convert bytes to MB
        info->ram_total = (uint64_t)(memstat.ullTotalPhys / (1024 * 1024));
        info->ram_available = (uint64_t)(memstat.ullAvailPhys / (1024 * 1024));
        info->ram_free = info->ram_available;  // Windows doesn't distinguish available vs free
    }
#elif defined(__linux__)
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

#if PLATFORM_POSIX
    // Fallback for POSIX systems
    if (info->ram_total == 0) {
        long pages = sysconf(_SC_PHYS_PAGES);
        long page_size = sysconf(_SC_PAGE_SIZE);
        if (pages > 0 && page_size > 0) {
            info->ram_total = (pages * page_size) / (1024 * 1024);
        }
    }
#endif

    if (info->ram_available == 0) {
        info->ram_available = info->ram_free;
    }
}

// Detect CPU features (AVX2, AVX-512, SHA-NI)
// Note: /proc/cpuinfo "flags" lines can exceed small fixed buffers.
#ifdef __linux__
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
#endif

static void detect_cpu_features(system_info_t *info) {
    info->has_avx2 = false;
    info->has_avx512 = false;
    info->has_avx512f = false;
    info->has_avx512dq = false;
    info->has_avx512bw = false;
    info->has_avx512vl = false;
    info->has_sha_ni = false;
    info->numa_nodes = 1;

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    /* MSVC CPU feature detection using __cpuid intrinsics */
    int cpu_info[4];  /* EAX, EBX, ECX, EDX */

    /* Check CPUID support and get max function ID */
    __cpuid(cpu_info, 0);
    int max_func_id = cpu_info[0];

    if (max_func_id >= 1) {
        /* Get basic CPU features (function 1) */
        __cpuid(cpu_info, 1);
        int ecx_feat = cpu_info[2];  /* ECX register */

        /* Check if OSXSAVE is enabled (bit 27) - required for AVX/AVX-512 */
        bool osxsave = (ecx_feat & (1 << 27)) != 0;

        if (osxsave) {
            /* Use _xgetbv to check OS support for AVX/AVX-512 */
            unsigned long long xcr0 = _xgetbv(0);
            bool avx_supported = (xcr0 & 0x6) == 0x6;  /* XMM and YMM state */
            bool avx512_supported = (xcr0 & 0xE6) == 0xE6;  /* opmask+ZMM state */

            /* Get extended features (function 7, sub-function 0) */
            if (max_func_id >= 7) {
                __cpuidex(cpu_info, 7, 0);
                int ebx_feat = cpu_info[1];  /* EBX register */

                /* Check feature bits in EBX */
                if (avx_supported) {
                    info->has_avx2 = (ebx_feat & (1 << 5)) != 0;  /* AVX2 (bit 5) */
                }

                if (avx512_supported) {
                    info->has_avx512f = (ebx_feat & (1 << 16)) != 0;  /* AVX-512F (bit 16) */
                    info->has_avx512dq = (ebx_feat & (1 << 17)) != 0;  /* AVX-512DQ (bit 17) */
                    info->has_avx512bw = (ebx_feat & (1 << 30)) != 0;  /* AVX-512BW (bit 30) */
                    info->has_avx512vl = (ebx_feat & (1 << 31)) != 0;  /* AVX-512VL (bit 31) */
                    info->has_avx512 = info->has_avx512f;
                }

                /* SHA extensions (bit 29 in EBX) */
                info->has_sha_ni = (ebx_feat & (1 << 29)) != 0;
            }
        }
    }
    bool used_builtin = false;

#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
    /* Prefer compiler-provided runtime detection on x86: it accounts for OS
     * support (XSAVE/XGETBV) and avoids false positives that can crash when
     * executing AVX/AVX-512 instructions. */
    __builtin_cpu_init();
    info->has_avx2 = __builtin_cpu_supports("avx2");
    info->has_avx512f = __builtin_cpu_supports("avx512f");
    info->has_avx512 = info->has_avx512f;
    /* SHA-NI and AVX-512 variants: detect via /proc/cpuinfo below
     * for maximum compatibility across compiler versions. */
    bool used_builtin = true;
#else
    bool used_builtin = false;
#endif

#ifdef __linux__
    // Detect NUMA nodes
    DIR *numa_dir = opendir("/sys/devices/system/node");
    if (numa_dir) {
        struct dirent *entry;
        int numa_count = 0;
        while ((entry = readdir(numa_dir)) != NULL) {
            if (strncmp(entry->d_name, "node", 4) == 0 && isdigit(entry->d_name[4])) {
                numa_count++;
            }
        }
        closedir(numa_dir);
        if (numa_count > 0) {
            info->numa_nodes = numa_count;
        }
    }

    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f) return;

    char *line = NULL;
    size_t cap = 0;
    while (getline(&line, &cap, f) != -1) {
        if (strncmp(line, "flags", 5) == 0 || strncmp(line, "Features", 8) == 0) {
            if (!used_builtin) {
                if (token_present(line, "avx2")) info->has_avx2 = true;
                if (token_present(line, "avx512f")) {
                    info->has_avx512 = true;
                    info->has_avx512f = true;
                }
            }
            // Detect AVX-512 variants (always from cpuinfo for accuracy)
            if (token_present(line, "avx512dq")) info->has_avx512dq = true;
            if (token_present(line, "avx512bw")) info->has_avx512bw = true;
            if (token_present(line, "avx512vl")) info->has_avx512vl = true;
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

// Detect CPU model name from /proc/cpuinfo
static void detect_cpu_model(system_info_t *info) {
    info->cpu_model[0] = '\0';

#ifdef __linux__
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "model name", 10) == 0) {
            char *colon = strchr(line, ':');
            if (colon) {
                colon++;
                while (*colon && isspace((unsigned char)*colon)) colon++;
                strncpy(info->cpu_model, colon, sizeof(info->cpu_model) - 1);
                info->cpu_model[sizeof(info->cpu_model) - 1] = '\0';
                // Remove trailing newline
                size_t len = strlen(info->cpu_model);
                while (len > 0 && isspace((unsigned char)info->cpu_model[len - 1])) {
                    info->cpu_model[--len] = '\0';
                }
            }
            break;
        }
    }
    fclose(f);
#endif

    // Fallback
    if (info->cpu_model[0] == '\0') {
        strncpy(info->cpu_model, "Unknown CPU", sizeof(info->cpu_model) - 1);
    }
}

void sysinfo_init(system_info_t *info) {
    memset(info, 0, sizeof(system_info_t));

    // Detect CPU
    info->cpu_physical_cores = detect_physical_cores();
    info->cpu_logical_cores = detect_logical_cores();
    info->cpu_threads_optimal = info->cpu_physical_cores;
    detect_cpu_model(info);

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

    // Calculate performance scores
    sysinfo_compute_scores(info);
}

void sysinfo_print(const system_info_t *info) {
    printf("\n[+] System Configuration Detected:\n");
    printf("    ├─ CPU: %d physical cores, %d logical cores",
           info->cpu_physical_cores, info->cpu_logical_cores);
    if (info->numa_nodes > 1) {
        printf(" (%d NUMA nodes)", info->numa_nodes);
    }
    printf("\n");
    printf("    ├─ Cache: L1=%" PRIu64 " KB, L2=%" PRIu64 " KB, L3=%" PRIu64 " KB\n",
           info->cache_l1_size, info->cache_l2_size, info->cache_l3_size);
    printf("    ├─ RAM: %" PRIu64 " MB total, %" PRIu64 " MB available\n",
           info->ram_total, info->ram_available);
    if (info->gpu_count > 0) {
        if (info->gpu_vram_mb > 0) {
            printf("    ├─ GPU: %s x%d (%llu MB VRAM",
                   info->gpu_name[0] ? info->gpu_name : "NVIDIA GPU",
                   info->gpu_count,
                   (unsigned long long)info->gpu_vram_mb);
            if (info->gpu_compute_capability > 0) {
                printf(", sm_%d", info->gpu_compute_capability);
            }
            printf(")\n");
        } else {
            printf("    ├─ GPU: %s x%d\n",
                   info->gpu_name[0] ? info->gpu_name : "NVIDIA GPU",
                   info->gpu_count);
        }
    } else {
        printf("    ├─ GPU: none detected\n");
    }

    // Extended CPU features
    printf("    ├─ Features: AVX2=%s, AVX-512=%s",
           info->has_avx2 ? "yes" : "no",
           info->has_avx512 ? "yes" : "no");
    if (info->has_avx512) {
        printf(" (F");
        if (info->has_avx512dq) printf("+DQ");
        if (info->has_avx512bw) printf("+BW");
        if (info->has_avx512vl) printf("+VL");
        printf(")");
    }
    printf(", SHA-NI=%s\n", info->has_sha_ni ? "yes" : "no");

    // Performance scores
    printf("    └─ Scores: CPU=%.1f, GPU=%.1f",
           info->cpu_score, info->gpu_score);
    if (info->hybrid_ratio > 0.0f) {
        printf(" (hybrid: %d%% GPU)", (int)(info->hybrid_ratio * 100.0f));
    }
    printf("\n");

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

void sysinfo_compute_scores(system_info_t *info) {
    // CPU Score formula:
    // Base score = physical cores (not logical, to avoid HT double-counting)
    // Multipliers: +50% for AVX2, +100% for AVX-512
    float cpu_base = (float)info->cpu_physical_cores;
    float cpu_multiplier = 1.0f;

    if (info->has_avx512 && info->has_avx512dq) {
        // Full AVX-512 with DQ (needed for ripemd160_avx512)
        cpu_multiplier += 1.0f;
    } else if (info->has_avx2) {
        cpu_multiplier += 0.5f;
    }

    if (info->has_sha_ni) {
        // SHA-NI provides ~10-15% boost for SHA256
        cpu_multiplier += 0.1f;
    }

    info->cpu_score = cpu_base * cpu_multiplier;

    // GPU Score formula:
    // Based on SM count and compute capability
    // RTX 2080 Super: 48 SMs, CC 7.5 → score ~360
    // RTX 3080: 68 SMs, CC 8.6 → score ~585
    // RTX 4090: 128 SMs, CC 8.9 → score ~1139
    if (info->has_cuda && info->gpu_sm_count > 0) {
        float sm_count = (float)info->gpu_sm_count;
        float cc_factor = 1.0f;

        // Compute capability multiplier
        int cc = info->gpu_compute_capability;
        if (cc >= 90) {
            cc_factor = 1.2f;  // Hopper (H100)
        } else if (cc >= 89) {
            cc_factor = 1.1f;  // Ada Lovelace (RTX 40xx)
        } else if (cc >= 86) {
            cc_factor = 1.0f;  // Ampere (RTX 30xx)
        } else if (cc >= 75) {
            cc_factor = 0.9f;  // Turing (RTX 20xx)
        } else {
            cc_factor = 0.7f;  // Older
        }

        info->gpu_score = sm_count * cc_factor * 7.5f;  // Scale factor
    } else if (info->has_cuda) {
        // Estimate based on VRAM (rough approximation)
        // 8GB VRAM ≈ RTX 2070 ≈ 36 SMs
        // 11GB VRAM ≈ RTX 2080 Ti ≈ 68 SMs
        float estimated_sms = (float)info->gpu_vram_mb / 200.0f;  // Very rough
        info->gpu_score = estimated_sms * 6.0f;
    } else {
        info->gpu_score = 0.0f;
    }

    // Hybrid ratio: percentage of work that should go to GPU
    float total = info->cpu_score + info->gpu_score;
    if (total > 0.0f && info->gpu_score > 0.0f) {
        info->hybrid_ratio = info->gpu_score / total;
        // Clamp to reasonable range (at least 5% to CPU)
        if (info->hybrid_ratio > 0.95f) info->hybrid_ratio = 0.95f;
    } else {
        info->hybrid_ratio = 0.0f;
    }
}

int sysinfo_get_hybrid_gpu_percent(const system_info_t *info) {
    return (int)(info->hybrid_ratio * 100.0f);
}
