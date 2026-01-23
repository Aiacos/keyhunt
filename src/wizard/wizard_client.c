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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>

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
static char g_executable_path[PATH_MAX] = "";  /* Absolute path to keyhunt binary */

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
 * Validate that the executable exists and is runnable.
 * Performs a quick test run to ensure everything works.
 *
 * @param exe_path Path to executable
 * @return 0 on success, -1 on error
 */
static int validate_executable(const char *exe_path) {
    struct stat st;

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

    /* Quick sanity test: run --help and check exit code */
    char cmd[PATH_MAX + 64];
    snprintf(cmd, sizeof(cmd), "%s --help >/dev/null 2>&1", exe_path);
    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "[-] Executable failed sanity test (exit %d): %s\n",
                WEXITSTATUS(ret), exe_path);
        return -1;
    }

    return 0;
}

/* ============================================================================
 * Signal Handler
 * ============================================================================ */

static void client_signal_handler(int sig) {
    (void)sig;
    g_client_running = 0;
    printf("\n\n[!] Shutdown signal received...\n");
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

    /* Build GPU argument if enabled */
    char gpu_arg[64] = "";
    if (cfg->gpu_percent > 0) {
        /* Only use GPU if actually available - check will happen in keyhunt */
        snprintf(gpu_arg, sizeof(gpu_arg), "-G on ");
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

    while (fgets(line, sizeof(line), fp) && *stop_flag) {
        lines_read++;

        /* Parse total keys from various output formats */
        char *total_ptr = strstr(line, "Total ");
        if (total_ptr) {
            uint64_t total = 0;
            /* Format: "[+] Total X keys in Y seconds" */
            if (sscanf(total_ptr, "Total %llu keys", (unsigned long long*)&total) == 1) {
                *keys_checked = total;
            }
            /* Format: "[+] Total keys checked: X" */
            else if (sscanf(total_ptr, "Total keys checked: %llu", (unsigned long long*)&total) == 1) {
                *keys_checked = total;
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

    /* Auto-configure based on hardware */
    if (cfg->threads <= 0) {
        cfg->threads = sysinfo.cpu_logical_cores;
    }

    /* GPU detection */
    if (sysinfo.gpu_count > 0) {
        printf("    GPU: %s (%llu MB VRAM) - auto-enabled %d%%\n",
               sysinfo.gpu_name,
               (unsigned long long)sysinfo.gpu_vram_mb,
               cfg->gpu_percent > 0 ? cfg->gpu_percent : 95);
        if (cfg->gpu_percent == 0) {
            cfg->gpu_percent = 95;
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

    /* Set authentication token if configured */
    if (cfg->auth_token[0] != '\0') {
        dist_worker_set_auth_token(&client, cfg->auth_token);
        printf("    Using authentication token\n");
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

    /* Parse host:port */
    char host[256] = "localhost";
    int port = 7777;

    if (host_port) {
        const char *colon = strchr(host_port, ':');
        if (colon) {
            size_t host_len = colon - host_port;
            if (host_len > 0 && host_len < sizeof(host)) {
                strncpy(host, host_port, host_len);
                host[host_len] = '\0';
            }
            port = atoi(colon + 1);
            if (port <= 0 || port > 65535) {
                port = 7777;
            }
        } else {
            /* Just host, use default port */
            strncpy(host, host_port, sizeof(host) - 1);
        }
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
