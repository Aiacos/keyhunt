/*
 * wizard_client.c - Client mode (worker with auto-configuration)
 *
 * The client:
 * 1. Connects to server and receives configuration
 * 2. Auto-detects local hardware
 * 3. Runs as worker processing assigned ranges
 *
 * Architecture: Uses subprocess to run keyhunt search with proper
 * path resolution and error handling.
 */

#include "wizard.h"
#include "../distributed/distributed.h"
#include "../core/sysinfo.h"
#include "../gpu/gpu_backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>
#include <pthread.h>
#include <ctype.h>      /* isalnum() */
#include <fcntl.h>      /* open(), O_WRONLY */

/* PATH_MAX with fallback */
#ifdef __linux__
#include <linux/limits.h>
#endif
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* ============================================================================
 * Global State
 * ============================================================================ */

static volatile int g_client_running = 1;
static volatile int g_shutdown_requested = 0;
static char g_executable_path[PATH_MAX] = "";  /* Absolute path to keyhunt binary */

/* State for graceful shutdown progress saving */
static int g_client_puzzle_number = 0;
static char g_client_last_range_start[65] = "";
static char g_client_last_range_end[65] = "";

/* Heartbeat thread state */
static pthread_t g_heartbeat_thread;
static volatile int g_heartbeat_running = 0;
static dist_worker_client_t *g_heartbeat_client = NULL;
static pthread_mutex_t g_heartbeat_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile uint64_t g_keys_since_heartbeat = 0;
static volatile time_t g_last_heartbeat_time = 0;

/* ============================================================================
 * Executable Path Resolution
 * ============================================================================ */

/**
 * Get the absolute path to the current executable.
 * This ensures we can spawn subprocesses regardless of working directory.
 *
 * @param buf Output buffer for path
 * @param bufsz Size of buffer
 * @return 0 on success, -1 on error
 */
static int resolve_executable_path(char *buf, size_t bufsz) {
    /* Method 1: /proc/self/exe (Linux) */
    ssize_t len = readlink("/proc/self/exe", buf, bufsz - 1);
    if (len > 0) {
        buf[len] = '\0';
        return 0;
    }

    /* Method 2: Try common locations */
    const char *paths[] = {
        "./keyhunt",
        "/opt/keyhunt/keyhunt",
        "/usr/local/bin/keyhunt",
        NULL
    };

    for (int i = 0; paths[i] != NULL; i++) {
        if (access(paths[i], X_OK) == 0) {
            /* Get absolute path */
            if (realpath(paths[i], buf) != NULL) {
                return 0;
            }
            strncpy(buf, paths[i], bufsz - 1);
            buf[bufsz - 1] = '\0';
            return 0;
        }
    }

    /* Fallback: assume current directory */
    strncpy(buf, "./keyhunt", bufsz - 1);
    buf[bufsz - 1] = '\0';
    return -1;
}

/**
 * Validate a path for safe use in shell commands.
 * Checks for shell metacharacters that could cause injection.
 *
 * @param path Path to validate
 * @return true if safe, false if contains dangerous characters
 */
static bool is_safe_path(const char *path) {
    if (!path) return false;

    /* Check for shell metacharacters */
    for (const char *p = path; *p; p++) {
        switch (*p) {
            case ';':   /* Command separator */
            case '|':   /* Pipe */
            case '&':   /* Background/AND */
            case '$':   /* Variable expansion */
            case '`':   /* Command substitution */
            case '(':   /* Subshell */
            case ')':
            case '{':   /* Brace expansion */
            case '}':
            case '<':   /* Redirection */
            case '>':
            case '!':   /* History expansion */
            case '*':   /* Glob */
            case '?':   /* Glob */
            case '[':   /* Glob */
            case ']':
            case '\n':  /* Newline */
            case '\r':
            case '\t':  /* Tab (suspicious in paths) */
            case '\\':  /* Escape character */
            case '"':   /* Quotes */
            case '\'':
                return false;
            default:
                /* Allow alphanumeric, dots, dashes, underscores, slashes, spaces */
                if (!isalnum((unsigned char)*p) &&
                    *p != '.' && *p != '-' && *p != '_' && *p != '/' && *p != ' ') {
                    return false;
                }
        }
    }
    return true;
}

/**
 * Validate that the executable exists and is runnable.
 * Performs a quick test run to ensure everything works.
 *
 * @param exe_path Path to executable
 * @return 0 on success, -1 on error
 */
static int validate_executable(const char *exe_path) {
    struct stat st;

    /* Security check: Validate path doesn't contain shell metacharacters */
    if (!is_safe_path(exe_path)) {
        fprintf(stderr, "[-] Invalid executable path (contains unsafe characters): %s\n", exe_path);
        return -1;
    }

    /* Check file exists */
    if (stat(exe_path, &st) != 0) {
        fprintf(stderr, "[-] Executable not found: %s (%s)\n", exe_path, strerror(errno));
        return -1;
    }

    /* Check it's a regular file */
    if (!S_ISREG(st.st_mode)) {
        fprintf(stderr, "[-] Not a regular file: %s\n", exe_path);
        return -1;
    }

    /* Check execute permission */
    if (access(exe_path, X_OK) != 0) {
        fprintf(stderr, "[-] No execute permission: %s (%s)\n", exe_path, strerror(errno));
        return -1;
    }

    /*
     * Sanity test using fork/exec instead of system() for security.
     * This avoids shell metacharacter injection vulnerabilities.
     */
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "[-] Fork failed: %s\n", strerror(errno));
        return -1;
    }

    if (pid == 0) {
        /* Child process */
        /* Redirect stdout and stderr to /dev/null */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        /* Execute with --help argument */
        execl(exe_path, exe_path, "--help", (char *)NULL);
        /* If execl returns, it failed */
        _exit(127);
    }

    /* Parent process - wait for child */
    int status;
    if (waitpid(pid, &status, 0) < 0) {
        fprintf(stderr, "[-] Waitpid failed: %s\n", strerror(errno));
        return -1;
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        return 0;  /* Success */
    }

    /* Non-zero exit is okay for --help, just means it ran */
    if (WIFEXITED(status)) {
        return 0;  /* Executable ran, even if it returned error for --help */
    }

    fprintf(stderr, "[-] Executable failed sanity test: %s\n", exe_path);
    return -1;
}

/* ============================================================================
 * Signal Handler
 * ============================================================================ */

static void client_signal_handler(int sig) {
    (void)sig;
    g_client_running = 0;
    g_heartbeat_running = 0;
    g_shutdown_requested = 1;
    /* Use write() which is async-signal-safe instead of printf */
    const char msg[] = "\n\n[!] Shutdown signal received...\n";
    (void)write(STDERR_FILENO, msg, sizeof(msg) - 1);
}

/* ============================================================================
 * Heartbeat Thread
 * ============================================================================ */

/**
 * Background thread that sends periodic heartbeats to the server.
 * This ensures the server knows we're still alive during long searches.
 */
static void *heartbeat_thread_func(void *arg) {
    int interval_sec = *(int *)arg;
    if (interval_sec <= 0) interval_sec = 30;

    while (g_heartbeat_running && g_client_running) {
        /* Sleep in small increments to respond quickly to shutdown */
        for (int i = 0; i < interval_sec && g_heartbeat_running && g_client_running; i++) {
            sleep(1);
        }

        if (!g_heartbeat_running || !g_client_running) break;

        /* Send heartbeat */
        pthread_mutex_lock(&g_heartbeat_mutex);
        if (g_heartbeat_client && g_heartbeat_client->connected) {
            uint64_t keys = g_keys_since_heartbeat;
            g_keys_since_heartbeat = 0;

            int result = dist_worker_heartbeat(g_heartbeat_client, keys);
            if (result == 0) {
                g_last_heartbeat_time = time(NULL);
            }
        }
        pthread_mutex_unlock(&g_heartbeat_mutex);
    }

    return NULL;
}

/**
 * Start the heartbeat thread.
 */
static int start_heartbeat_thread(dist_worker_client_t *client, int interval_sec) {
    static int interval;  /* Static to keep value valid for thread */
    interval = interval_sec;

    g_heartbeat_client = client;
    g_heartbeat_running = 1;
    g_keys_since_heartbeat = 0;
    g_last_heartbeat_time = time(NULL);

    if (pthread_create(&g_heartbeat_thread, NULL, heartbeat_thread_func, &interval) != 0) {
        g_heartbeat_running = 0;
        return -1;
    }

    return 0;
}

/**
 * Stop the heartbeat thread.
 */
static void stop_heartbeat_thread(void) {
    if (!g_heartbeat_running) return;

    g_heartbeat_running = 0;
    pthread_join(g_heartbeat_thread, NULL);
    g_heartbeat_client = NULL;
}

/**
 * Update keys processed (called during search to track progress).
 */
static void update_heartbeat_keys(uint64_t keys) {
    pthread_mutex_lock(&g_heartbeat_mutex);
    g_keys_since_heartbeat += keys;
    pthread_mutex_unlock(&g_heartbeat_mutex);
}

/* ============================================================================
 * Subprocess Search with Robust Error Handling
 * ============================================================================ */

/**
 * Run keyhunt search via subprocess with proper error handling.
 *
 * @param start Range start (hex)
 * @param end Range end (hex)
 * @param cfg Configuration
 * @param keys_checked Output: number of keys checked
 * @param stop_flag Flag to check for early termination
 * @param found_key Output: found private key (if any)
 * @param found_addr Output: found address (if any)
 * @return 1 if key found, 0 if not found, -1 on error, -2 on timeout
 */
static int search_range_subprocess(const char *start, const char *end,
                                    const wizard_config_t *cfg,
                                    uint64_t *keys_checked, volatile int *stop_flag,
                                    char *found_key, char *found_addr) {
    char cmd[4096];
    *keys_checked = 0;
    found_key[0] = '\0';
    found_addr[0] = '\0';

    /* Create temp file for target address */
    char target_file[256];
    snprintf(target_file, sizeof(target_file), "/tmp/wizard_target_%d.txt", getpid());

    FILE *tf = fopen(target_file, "w");
    if (!tf) {
        fprintf(stderr, "[-] Failed to create target file: %s\n", strerror(errno));
        return -1;
    }
    fprintf(tf, "%s\n", cfg->target_address);
    fclose(tf);

    /* Build GPU argument if enabled - use hybrid mode for CPU+GPU parallel search */
    char gpu_arg[64] = "";
    if (cfg->gpu_percent > 0) {
        /* Use hybrid mode for CPU+GPU parallel search (maximum throughput).
         * keyhunt will automatically fall back to CPU-only if GPU backend
         * is not available (compiled without CUDA). */
        snprintf(gpu_arg, sizeof(gpu_arg), "-G hybrid ");
    }

    /* Build command with absolute path */
    if (strcmp(cfg->mode, "bsgs") == 0) {
        snprintf(cmd, sizeof(cmd),
            "timeout 300s '%s' -m bsgs -f '%s' -r %s:%s -t %d %s-q -s 1 2>&1",
            g_executable_path, target_file, start, end, cfg->threads, gpu_arg);
    } else {
        snprintf(cmd, sizeof(cmd),
            "timeout 300s '%s' -m %s -f '%s' -r %s:%s -t %d %s-l %s -q -s 1 2>&1",
            g_executable_path, cfg->mode, target_file, start, end,
            cfg->threads, gpu_arg, cfg->key_type);
    }

    /* Run keyhunt subprocess */
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        fprintf(stderr, "[-] Failed to start subprocess: %s\n", strerror(errno));
        unlink(target_file);
        return -1;
    }

    char line[1024];
    int found = 0;
    int lines_read = 0;
    static int debug_subprocess = -1;

    /* Check for debug mode on first call */
    if (debug_subprocess < 0) {
        debug_subprocess = (getenv("KEYHUNT_DEBUG_SUBPROCESS") != NULL) ? 1 : 0;
    }

    while (fgets(line, sizeof(line), fp) && *stop_flag) {
        lines_read++;

        if (debug_subprocess) {
            fprintf(stderr, "[DEBUG] LINE %d: %s", lines_read, line);
        }

        /* Parse total keys from various output formats */
        char *total_ptr = strstr(line, "Total ");
        if (total_ptr) {
            uint64_t total = 0;
            /* Format: "[+] Total X keys in Y seconds" */
            if (sscanf(total_ptr, "Total %llu keys", (unsigned long long*)&total) == 1) {
                *keys_checked = total;
                if (debug_subprocess) {
                    fprintf(stderr, "[DEBUG] Parsed keys: %llu\n", (unsigned long long)total);
                }
            }
            /* Format: "[+] Total keys checked: X" */
            else if (sscanf(total_ptr, "Total keys checked: %llu", (unsigned long long*)&total) == 1) {
                *keys_checked = total;
                if (debug_subprocess) {
                    fprintf(stderr, "[DEBUG] Parsed keys (v2): %llu\n", (unsigned long long)total);
                }
            }
        }

        /* Parse speed for progress tracking */
        char *speed_ptr = strstr(line, "Mkeys/s");
        if (speed_ptr) {
            /* Could extract speed here for real-time updates */
        }

        /* Parse found key */
        char *hit_ptr = strstr(line, "Hit! Private Key:");
        if (hit_ptr) {
            char key_hex[65] = {0};
            if (sscanf(hit_ptr, "Hit! Private Key: %64s", key_hex) == 1) {
                strncpy(found_key, key_hex, 64);
                found_key[64] = '\0';
                found = 1;
            }
        }

        /* Parse address (comes after Hit!) */
        if (found && found_addr[0] == '\0') {
            char *addr_ptr = strstr(line, "Address ");
            if (addr_ptr) {
                char addr[36] = {0};
                if (sscanf(addr_ptr, "Address %35s", addr) == 1) {
                    strncpy(found_addr, addr, 35);
                    found_addr[35] = '\0';
                }
            }
        }

        /* Check for error messages */
        if (strstr(line, "[E]") || strstr(line, "Error") || strstr(line, "error:")) {
            fprintf(stderr, "  [subprocess] %s", line);
        }
    }

    int exit_status = pclose(fp);
    unlink(target_file);

    /* Analyze exit status */
    if (WIFEXITED(exit_status)) {
        int exit_code = WEXITSTATUS(exit_status);

        if (exit_code == 0) {
            /* Success */
            return found ? 1 : 0;
        } else if (exit_code == 124) {
            /* Timeout */
            fprintf(stderr, "\n[-] Work unit timed out (5 min limit)\n");
            return -2;
        } else if (exit_code == 126) {
            /* Cannot execute */
            fprintf(stderr, "\n[-] Cannot execute keyhunt (code 126)\n");
            fprintf(stderr, "    Path: %s\n", g_executable_path);
            fprintf(stderr, "    Hint: Check permissions and architecture\n");
            return -1;
        } else if (exit_code == 127) {
            /* Command not found */
            fprintf(stderr, "\n[-] Keyhunt not found (code 127)\n");
            fprintf(stderr, "    Path: %s\n", g_executable_path);
            return -1;
        } else {
            /* Other error - might still have partial results */
            if (lines_read == 0) {
                fprintf(stderr, "\n[-] Subprocess failed with code %d (no output)\n", exit_code);
                fprintf(stderr, "    Command: %s\n", cmd);
                return -1;
            }
            /* Got some output, consider it partial success */
            return found ? 1 : 0;
        }
    } else if (WIFSIGNALED(exit_status)) {
        int sig = WTERMSIG(exit_status);
        fprintf(stderr, "\n[-] Subprocess killed by signal %d\n", sig);
        return -1;
    }

    if (debug_subprocess) {
        fprintf(stderr, "[DEBUG] Final: lines_read=%d, keys_checked=%llu, found=%d\n",
                lines_read, (unsigned long long)*keys_checked, found);
    }

    return found ? 1 : 0;
}

/* ============================================================================
 * Client Main Loop
 * ============================================================================ */

int wizard_client_run(wizard_config_t *cfg) {
    signal(SIGINT, client_signal_handler);
    signal(SIGTERM, client_signal_handler);

    wizard_print_header("KEYHUNT WIZARD - CLIENT MODE");

    /* Step 1: Resolve and validate executable path */
    printf("[+] Resolving executable path...\n");
    if (resolve_executable_path(g_executable_path, sizeof(g_executable_path)) != 0) {
        printf("[!] Warning: Could not resolve absolute path, using fallback\n");
    }
    printf("    Executable: %s\n", g_executable_path);

    printf("[+] Validating executable...\n");
    if (validate_executable(g_executable_path) != 0) {
        printf("\n[-] FATAL: Keyhunt executable validation failed!\n");
        printf("    Please ensure keyhunt is properly installed and executable.\n");
        printf("    Expected path: %s\n", g_executable_path);
        return -1;
    }
    printf("    Validation: OK\n\n");

    /* Step 2: Detect local hardware */
    system_info_t sysinfo;
    sysinfo_init(&sysinfo);

    printf("[+] Hardware Detection:\n");
    printf("    CPU: %d physical cores, %d logical threads\n",
           sysinfo.cpu_physical_cores, sysinfo.cpu_logical_cores);
    printf("    RAM: %llu MB available\n", (unsigned long long)sysinfo.ram_available);
    printf("    L3 Cache: %llu KB\n", (unsigned long long)sysinfo.cache_l3_size);
    printf("    Features: %s%s%s\n",
           sysinfo.has_avx2 ? "AVX2 " : "",
           sysinfo.has_avx512f ? "AVX512 " : "",
           sysinfo.has_sha_ni ? "SHA-NI " : "");
    sysinfo_compute_scores(&sysinfo);
    printf("    Performance Score: %.2f\n", sysinfo.cpu_score);

    /* Auto-configure based on hardware - ALWAYS use detected values
     * This ensures we use all available resources regardless of saved config.
     * Saved config values like threads=4 from a previous run should not
     * limit performance on machines with more cores. */
    cfg->threads = sysinfo.cpu_logical_cores;

    /* GPU detection - check both hardware AND compiled backend */
    int gpu_backend_ready = gpu_backend_available();

    if (sysinfo.gpu_count > 0) {
        if (gpu_backend_ready) {
            /* GPU hardware available AND backend compiled - enable hybrid mode */
            cfg->gpu_percent = 95;
            printf("    GPU: %s (%llu MB VRAM) - hybrid mode enabled\n",
                   sysinfo.gpu_name,
                   (unsigned long long)sysinfo.gpu_vram_mb);
        } else {
            /* GPU hardware detected but CUDA backend not compiled */
            printf("    GPU: %s (%llu MB VRAM) - DETECTED but CUDA backend not compiled\n",
                   sysinfo.gpu_name,
                   (unsigned long long)sysinfo.gpu_vram_mb);
            printf("    [!] To enable GPU: install CUDA toolkit and rebuild with 'make clean && make'\n");
            cfg->gpu_percent = 0;  /* Disable GPU since backend not available */
        }
    } else {
        printf("    GPU: None detected\n");
        cfg->gpu_percent = 0;
    }

    /* Step 3: Connect to server */
    printf("\n[+] Connecting to server %s:%d...\n", cfg->server_host, cfg->server_port);

    dist_worker_client_t client;
    memset(&client, 0, sizeof(client));

    /* Initialize client with connection info */
    if (dist_worker_init(&client, cfg->server_host, cfg->server_port, sysinfo.cpu_score) != 0) {
        printf("[-] Failed to initialize worker client\n");
        return -1;
    }

    /* Set hardware info from detected values - must be before connect */
    dist_worker_set_hardware_info(&client,
                                  sysinfo.cpu_physical_cores,
                                  sysinfo.cpu_logical_cores,
                                  sysinfo.cpu_model,
                                  sysinfo.gpu_name,
                                  (int)sysinfo.gpu_vram_mb);

    /* Set authentication token if configured */
    if (cfg->auth_token[0] != '\0') {
        dist_worker_set_auth_token(&client, cfg->auth_token);
        printf("    Using authentication token\n");
    }

    /* Load and report local progress count for cross-execution resume */
    int local_count = wizard_load_local_progress_count(cfg->puzzle_number);
    if (local_count > 0) {
        dist_worker_set_local_progress(&client, local_count);
        printf("    Local progress: %d ranges already completed\n", local_count);
    }

    /* Connection retry loop */
    int connect_attempts = 0;
    const int max_attempts = 5;

    while (connect_attempts < max_attempts && g_client_running) {
        if (dist_worker_connect(&client) == 0) {
            break;
        }
        connect_attempts++;
        printf("[-] Connection failed, retrying (%d/%d)...\n", connect_attempts, max_attempts);
        sleep(2);
    }

    if (!client.connected) {
        printf("[-] Could not connect to server after %d attempts\n", max_attempts);
        return -1;
    }

    printf("[+] Connected to server!\n");

    /* Step 4: Receive configuration from server */
    char server_target[64], server_mode[32], server_key_type[16];
    if (dist_worker_get_job_config(&client, server_target, server_mode, server_key_type) == 0) {
        strncpy(cfg->target_address, server_target, sizeof(cfg->target_address) - 1);
        strncpy(cfg->mode, server_mode, sizeof(cfg->mode) - 1);
        strncpy(cfg->key_type, server_key_type, sizeof(cfg->key_type) - 1);
        cfg->puzzle_number = client.received_puzzle_number;
        cfg->bits = client.received_bits;
        printf("[+] Received configuration from server:\n");
    } else {
        printf("[+] Using local configuration:\n");
    }

    /* Adjust GPU settings based on mode
     * BSGS is CPU-optimized and doesn't benefit from GPU */
    if (strcmp(cfg->mode, "bsgs") == 0 && cfg->gpu_percent > 0) {
        printf("[i] BSGS mode detected - disabling GPU (CPU-optimized algorithm)\n");
        cfg->gpu_percent = 0;
    }

    /* Store puzzle number for graceful shutdown */
    g_client_puzzle_number = cfg->puzzle_number;

    printf("    Puzzle: #%d (%d bits)\n", cfg->puzzle_number, cfg->bits);
    printf("    Target: %s\n", cfg->target_address);
    printf("    Mode: %s (%s)\n", cfg->mode, cfg->key_type);
    printf("    Threads: %d\n", cfg->threads);
    printf("    GPU: %d%%\n", cfg->gpu_percent);

    int heartbeat_interval = dist_worker_get_heartbeat_interval(&client);
    printf("    Heartbeat: every %d seconds\n", heartbeat_interval);

    printf("\n[+] Starting worker loop...\n");
    printf("[+] Press Ctrl+C to stop.\n");
    wizard_print_separator();
    printf("\n");

    /* Start heartbeat thread */
    if (start_heartbeat_thread(&client, heartbeat_interval) != 0) {
        printf("[!] Warning: Could not start heartbeat thread\n");
    } else {
        printf("[+] Heartbeat thread started (every %d seconds)\n", heartbeat_interval);
    }

    /* Step 5: Main work loop */
    uint64_t total_keys = 0;
    int work_count = 0;
    int error_count = 0;
    const int max_consecutive_errors = 5;
    time_t start_time = time(NULL);
    int no_work_count = 0;

    while (g_client_running) {
        /* Request work from coordinator */
        char range_start[65], range_end[65];
        int result = dist_worker_request_work(&client, range_start, range_end);

        if (result == 1) {
            /* No work available */
            no_work_count++;
            if (no_work_count >= 10) {
                printf("\n[+] No more work available from server (waited 30s).\n");
                break;
            }
            printf("\r[i] Waiting for work... (%d/10)     ", no_work_count);
            fflush(stdout);
            sleep(3);
            continue;
        }

        no_work_count = 0;

        if (result < 0) {
            printf("\n[-] Error requesting work, reconnecting...\n");
            error_count++;

            if (error_count >= max_consecutive_errors) {
                printf("[-] Too many consecutive errors (%d), exiting\n", error_count);
                break;
            }

            sleep(3);
            dist_worker_disconnect(&client);
            if (dist_worker_connect(&client) != 0) {
                printf("[-] Reconnection failed\n");
                break;
            }
            printf("[+] Reconnected\n");
            continue;
        }

        work_count++;
        error_count = 0;  /* Reset on successful work request */

        /* Store current range for graceful shutdown progress saving */
        strncpy(g_client_last_range_start, range_start, sizeof(g_client_last_range_start) - 1);
        strncpy(g_client_last_range_end, range_end, sizeof(g_client_last_range_end) - 1);

        /* Process the range */
        printf("\r[Unit #%d] Range: %.16s...%.8s ",
               work_count, range_start, range_end + strlen(range_end) - 8);
        fflush(stdout);

        uint64_t keys_checked = 0;
        char found_key[65] = {0};
        char found_addr[36] = {0};
        time_t unit_start = time(NULL);

        /* Run search */
        int search_result = search_range_subprocess(
            range_start, range_end,
            cfg, &keys_checked, &g_client_running,
            found_key, found_addr
        );

        time_t unit_elapsed = time(NULL) - unit_start;
        if (unit_elapsed == 0) unit_elapsed = 1;

        /* Handle results */
        if (search_result == -1) {
            /* Fatal error */
            error_count++;
            printf("\n[-] Search error, will retry next unit\n");

            if (error_count >= max_consecutive_errors) {
                printf("[-] Too many consecutive errors, exiting\n");
                break;
            }
            continue;
        }

        if (search_result == -2) {
            /* Timeout - partial progress */
            printf("\n[!] Work unit timed out, reporting partial progress\n");
        }

        total_keys += keys_checked;
        update_heartbeat_keys(keys_checked);  /* Track for heartbeat reporting */
        double speed = (double)keys_checked / unit_elapsed / 1000000.0;

        /* Show progress */
        time_t elapsed = time(NULL) - start_time;
        printf("\r[Unit #%d] %.2e keys | %.1f Mkeys/s | Total: %.2e | Elapsed: %02ld:%02ld:%02ld",
               work_count,
               (double)keys_checked,
               speed,
               (double)total_keys,
               elapsed / 3600, (elapsed % 3600) / 60, elapsed % 60);
        fflush(stdout);

        /* Report completion to server */
        if (dist_worker_report_done(&client, keys_checked, unit_elapsed * 1000) != 0) {
            printf("\n[-] Failed to report completion, reconnecting...\n");
            dist_worker_disconnect(&client);
            sleep(2);
            if (dist_worker_connect(&client) != 0) {
                printf("[-] Reconnection failed, exiting\n");
                break;
            }
            printf("[+] Reconnected to server\n");
            continue;
        }

        /* Save local progress */
        wizard_save_local_progress(cfg->puzzle_number, range_start, range_end);

        /* Check if key found */
        if (search_result == 1 && found_key[0]) {
            printf("\n\n");
            printf("╔═══════════════════════════════════════════════════════════╗\n");
            printf("║               🎉 PRIVATE KEY FOUND! 🎉                    ║\n");
            printf("╠═══════════════════════════════════════════════════════════╣\n");
            printf("║ Key:  %-52s ║\n", found_key);
            printf("║ Addr: %-52s ║\n", found_addr);
            printf("╚═══════════════════════════════════════════════════════════╝\n");

            /* Report to server */
            dist_worker_report_found(&client, found_key, found_addr);

            /* Save locally */
            FILE *f = fopen("FOUND_KEY.txt", "w");
            if (f) {
                time_t now = time(NULL);
                fprintf(f, "PRIVATE KEY FOUND!\n");
                fprintf(f, "Time: %s", ctime(&now));
                fprintf(f, "Puzzle: #%d\n", cfg->puzzle_number);
                fprintf(f, "Private Key: %s\n", found_key);
                fprintf(f, "Address: %s\n", found_addr);
                fclose(f);
            }

            printf("\n[+] Key saved to FOUND_KEY.txt\n");
            printf("[+] Continuing search in case of multiple targets...\n\n");
        }

        printf("\n");
    }

    /* Stop heartbeat thread */
    stop_heartbeat_thread();

    /* Graceful shutdown: save final progress if we have a pending work unit */
    if (g_shutdown_requested && g_client_puzzle_number > 0 &&
        g_client_last_range_start[0] != '\0' && g_client_last_range_end[0] != '\0') {
        printf("[+] Saving final progress before shutdown...\n");
        wizard_save_local_progress(g_client_puzzle_number,
                                   g_client_last_range_start,
                                   g_client_last_range_end);
    }

    /* Cleanup and final stats */
    printf("\n\n");
    wizard_print_separator();
    printf("\n[+] Client Statistics:\n");
    printf("    Work units processed: %d\n", work_count);
    printf("    Total keys checked: %.2e\n", (double)total_keys);
    printf("    Run time: %ld seconds\n", time(NULL) - start_time);
    if (work_count > 0) {
        double avg_speed = (double)total_keys / (time(NULL) - start_time) / 1000000.0;
        printf("    Average speed: %.2f Mkeys/s\n", avg_speed);
    }

    /* Clear global state */
    g_client_puzzle_number = 0;
    g_client_last_range_start[0] = '\0';
    g_client_last_range_end[0] = '\0';

    dist_worker_disconnect(&client);
    return 0;
}

/* ============================================================================
 * Auto-Client Entry Point (used by --wizard-client flag)
 * ============================================================================ */

/**
 * Run client with auto-configuration from host:port string.
 * Called when keyhunt is invoked with --wizard-client flag.
 *
 * @param host_port Host:port string (e.g., "192.168.1.100:7777")
 * @return 0 on success, -1 on error
 */
int wizard_client_run_auto(const char *host_port) {
    wizard_config_t cfg;
    wizard_config_init(&cfg);

    /* Validate input */
    if (!host_port || host_port[0] == '\0') {
        fprintf(stderr, "[-] Error: host:port argument is required\n");
        fprintf(stderr, "    Usage: --wizard-client host:port\n");
        fprintf(stderr, "    Example: --wizard-client 192.168.1.100:7777\n");
        return -1;
    }

    /* Parse host:port */
    char host[256] = {0};
    int port = 0;

    const char *colon = strchr(host_port, ':');
    if (colon) {
        /* Format: host:port */
        size_t host_len = colon - host_port;

        /* Validate host is not empty */
        if (host_len == 0) {
            fprintf(stderr, "[-] Error: hostname cannot be empty\n");
            fprintf(stderr, "    Usage: --wizard-client host:port\n");
            fprintf(stderr, "    Example: --wizard-client 192.168.1.100:7777\n");
            return -1;
        }

        if (host_len >= sizeof(host)) {
            fprintf(stderr, "[-] Error: hostname too long (max %zu characters)\n", sizeof(host) - 1);
            return -1;
        }

        strncpy(host, host_port, host_len);
        host[host_len] = '\0';

        /* Parse and validate port */
        const char *port_str = colon + 1;
        if (port_str[0] == '\0') {
            fprintf(stderr, "[-] Error: port number is required after ':'\n");
            fprintf(stderr, "    Usage: --wizard-client host:port\n");
            fprintf(stderr, "    Example: --wizard-client 192.168.1.100:7777\n");
            return -1;
        }

        port = atoi(port_str);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "[-] Error: invalid port number '%s' (must be 1-65535)\n", port_str);
            return -1;
        }
    } else {
        /* Just host, use default port */
        if (strlen(host_port) >= sizeof(host)) {
            fprintf(stderr, "[-] Error: hostname too long (max %zu characters)\n", sizeof(host) - 1);
            return -1;
        }
        strncpy(host, host_port, sizeof(host) - 1);
        host[sizeof(host) - 1] = '\0';
        port = 7777;  /* Default port */
        fprintf(stderr, "[i] No port specified, using default port %d\n", port);
    }

    /* Configure as client */
    strncpy(cfg.server_host, host, sizeof(cfg.server_host) - 1);
    cfg.server_port = port;
    cfg.is_server = false;

    /* Check for auth token from environment (passed by server when spawning) */
    const char *env_token = getenv("KEYHUNT_AUTH_TOKEN");
    if (env_token && env_token[0] != '\0') {
        strncpy(cfg.auth_token, env_token, sizeof(cfg.auth_token) - 1);
        cfg.auth_token[sizeof(cfg.auth_token) - 1] = '\0';
    }

    printf("[Auto-Client] Connecting to %s:%d\n\n", cfg.server_host, cfg.server_port);

    return wizard_client_run(&cfg);
}
