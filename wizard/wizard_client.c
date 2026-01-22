/*
 * wizard_client.c - Client mode (worker with auto-configuration)
 *
 * The client:
 * 1. Connects to server and receives configuration
 * 2. Auto-detects local hardware
 * 3. Runs as worker processing assigned ranges
 */

#include "wizard.h"
#include "../distributed/distributed.h"
#include "../sysinfo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

static volatile int g_client_running = 1;

static void client_signal_handler(int sig) {
    (void)sig;
    g_client_running = 0;
    printf("\n\n[!] Shutdown signal received...\n");
}

/* Real keyhunt search via subprocess */
static int search_range_subprocess(const char *start, const char *end,
                                    const wizard_config_t *cfg,
                                    uint64_t *keys_checked, volatile int *stop_flag,
                                    char *found_key, char *found_addr) {
    char cmd[2048];
    *keys_checked = 0;
    found_key[0] = '\0';
    found_addr[0] = '\0';

    /* Create a temp file for the target address */
    char target_file[256];
    snprintf(target_file, sizeof(target_file), "/tmp/wizard_target_%d.txt", getpid());
    FILE *tf = fopen(target_file, "w");
    if (tf) {
        fprintf(tf, "%s\n", cfg->target_address);
        fclose(tf);
    } else {
        return -1;
    }

    /* Build command based on mode, with GPU hybrid for maximum throughput */
    char gpu_arg[64] = "";
    if (cfg->gpu_percent > 0) {
        /* Use hybrid mode: CPU + GPU in parallel for maximum resource utilization */
        snprintf(gpu_arg, sizeof(gpu_arg), "-G hybrid ");
    }

    if (strcmp(cfg->mode, "bsgs") == 0) {
        snprintf(cmd, sizeof(cmd),
            "./keyhunt -m bsgs -f %s -r %s:%s -t %d %s-q -s 1 2>&1",
            target_file, start, end, cfg->threads, gpu_arg);
    } else {
        snprintf(cmd, sizeof(cmd),
            "./keyhunt -m %s -f %s -r %s:%s -t %d %s-l %s -q -s 1 2>&1",
            cfg->mode, target_file, start, end, cfg->threads, gpu_arg, cfg->key_type);
    }

    /* Run keyhunt and parse output */
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        unlink(target_file);
        return -1;
    }

    char line[1024];
    int found = 0;

    while (fgets(line, sizeof(line), fp) && *stop_flag) {
        /* Parse total keys - CPU format: "[+] Total X keys" */
        char *total_ptr = strstr(line, "Total ");
        if (total_ptr) {
            uint64_t total = 0;
            if (sscanf(total_ptr, "Total %llu keys", (unsigned long long*)&total) == 1) {
                *keys_checked = total;
            }
            /* GPU format: "[+] Total keys checked: X" */
            else if (sscanf(total_ptr, "Total keys checked: %llu", (unsigned long long*)&total) == 1) {
                *keys_checked = total;
            }
        }

        /* Parse found key */
        char *hit_ptr = strstr(line, "Hit! Private Key:");
        if (hit_ptr) {
            char key_hex[65] = {0};
            if (sscanf(hit_ptr, "Hit! Private Key: %64s", key_hex) == 1) {
                strncpy(found_key, key_hex, 64);
                found = 1;
            }
        }

        /* Parse address */
        char *addr_ptr = strstr(line, "Address ");
        if (addr_ptr && found) {
            char addr[36] = {0};
            if (sscanf(addr_ptr, "Address %35s", addr) == 1) {
                strncpy(found_addr, addr, 35);
            }
        }
    }

    pclose(fp);
    unlink(target_file);

    return found ? 1 : 0;
}

int wizard_client_run(wizard_config_t *cfg) {
    signal(SIGINT, client_signal_handler);
    signal(SIGTERM, client_signal_handler);

    wizard_print_header("KEYHUNT WIZARD - CLIENT MODE");

    /* Detect local hardware */
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

    /* Auto-configure threads if not set */
    if (cfg->threads <= 0) {
        cfg->threads = sysinfo.cpu_logical_cores;
    }

    /* Auto-configure GPU if available and not manually set */
    if (cfg->gpu_percent == 0 && sysinfo.has_cuda) {
        cfg->gpu_percent = sysinfo_get_hybrid_gpu_percent(&sysinfo);
        printf("    GPU: %s (%llu MB VRAM) - auto-enabled %d%%\n",
               sysinfo.gpu_name, (unsigned long long)sysinfo.gpu_vram_mb, cfg->gpu_percent);
    } else if (sysinfo.has_cuda) {
        printf("    GPU: %s (%llu MB VRAM) - %d%%\n",
               sysinfo.gpu_name, (unsigned long long)sysinfo.gpu_vram_mb, cfg->gpu_percent);
    }

    printf("\n[+] Connecting to server %s:%d...\n",
           cfg->server_host, cfg->server_port);

    /* Initialize worker client */
    dist_worker_client_t client;
    if (dist_worker_init(&client, cfg->server_host, cfg->server_port,
                         sysinfo.cpu_score) != 0) {
        printf("[-] Failed to initialize worker client\n");
        return -1;
    }

    /* Set detailed hardware info before connecting */
    dist_worker_set_hardware_info(&client,
        sysinfo.cpu_physical_cores,
        sysinfo.cpu_logical_cores,
        sysinfo.cpu_model,
        sysinfo.gpu_name[0] ? sysinfo.gpu_name : NULL,
        (int)sysinfo.gpu_vram_mb);

    /* Connect to coordinator */
    int retry = 0;
    while (g_client_running && retry < 5) {
        if (dist_worker_connect(&client) == 0) {
            printf("[+] Connected to coordinator!\n");
            break;
        }
        retry++;
        printf("[-] Connection failed, retrying (%d/5)...\n", retry);
        sleep(2);
    }

    if (!client.connected) {
        printf("[-] Could not connect to server after 5 attempts\n");
        return -1;
    }

    /* Receive configuration from server */
    char server_target[64], server_mode[32], server_key_type[16];
    if (dist_worker_get_job_config(&client, server_target, server_mode, server_key_type) == 0) {
        /* Use server-provided configuration */
        strncpy(cfg->target_address, server_target, sizeof(cfg->target_address) - 1);
        strncpy(cfg->mode, server_mode, sizeof(cfg->mode) - 1);
        strncpy(cfg->key_type, server_key_type, sizeof(cfg->key_type) - 1);
        cfg->puzzle_number = client.received_puzzle_number;
        cfg->bits = client.received_bits;
        printf("[+] Received configuration from server:\n");
    } else {
        /* Fallback to local configuration */
        printf("[+] Using local configuration (server did not provide config):\n");
    }
    printf("    Puzzle: #%d (%d bits)\n", cfg->puzzle_number, cfg->bits);
    printf("    Target: %s\n", cfg->target_address);
    printf("    Mode: %s (%s)\n", cfg->mode, cfg->key_type);
    printf("    Threads: %d\n", cfg->threads);
    printf("    GPU: %d%%\n", cfg->gpu_percent);

    /* Get server-configured heartbeat interval */
    int heartbeat_interval = dist_worker_get_heartbeat_interval(&client);
    printf("    Heartbeat: every %d seconds\n", heartbeat_interval);

    printf("\n[+] Starting worker loop...\n");
    printf("[+] Press Ctrl+C to stop.\n");
    wizard_print_separator();
    printf("\n");

    /* Stats tracking */
    uint64_t total_keys = 0;
    int work_count = 0;
    time_t start_time = time(NULL);
    time_t last_heartbeat = start_time;

    /* Main work loop */
    while (g_client_running) {
        /* Request work from coordinator */
        char range_start[65], range_end[65];

        int result = dist_worker_request_work(&client, range_start, range_end);

        if (result == 1) {
            printf("\n[+] No more work available from server.\n");
            break;
        }

        if (result < 0) {
            printf("\n[-] Error requesting work, reconnecting...\n");
            sleep(3);

            /* Try to reconnect */
            dist_worker_disconnect(&client);
            if (dist_worker_connect(&client) != 0) {
                printf("[-] Reconnection failed\n");
                break;
            }
            continue;
        }

        work_count++;

        /* Process the range */
        printf("\r[Unit #%d] Range: %.16s...%.8s ",
               work_count, range_start, range_end + strlen(range_end) - 8);
        fflush(stdout);

        uint64_t keys_checked = 0;
        char found_key[65] = {0};
        char found_addr[36] = {0};

        time_t unit_start = time(NULL);

        /* Call keyhunt via subprocess */
        int search_result = search_range_subprocess(
            range_start, range_end,
            cfg, &keys_checked, &g_client_running,
            found_key, found_addr
        );

        time_t unit_elapsed = time(NULL) - unit_start;
        if (unit_elapsed == 0) unit_elapsed = 1;

        total_keys += keys_checked;
        double speed = (double)keys_checked / unit_elapsed / 1000000.0;

        /* Report completion */
        if (dist_worker_report_done(&client, keys_checked, unit_elapsed * 1000) != 0) {
            printf("\n[-] Failed to report completion\n");
        }

        /* Save local progress */
        wizard_save_local_progress(cfg->puzzle_number, range_start, range_end);

        /* Check if found */
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
                fprintf(f, "PRIVATE KEY FOUND!\n");
                fprintf(f, "Time: %s", ctime(&(time_t){time(NULL)}));
                fprintf(f, "Puzzle: #%d\n", cfg->puzzle_number);
                fprintf(f, "Private Key: %s\n", found_key);
                fprintf(f, "Address: %s\n", found_addr);
                fclose(f);
            }

            break;
        }

        printf("| %.2f Mkeys/s | Total: %.2e keys", speed, (double)total_keys);
        fflush(stdout);

        /* Periodic heartbeat using server-configured interval */
        time_t now = time(NULL);
        if (now - last_heartbeat >= heartbeat_interval) {
            dist_worker_heartbeat(&client, keys_checked);
            last_heartbeat = now;
        }
    }

    /* Disconnect */
    dist_worker_disconnect(&client);

    /* Final stats */
    time_t total_time = time(NULL) - start_time;
    if (total_time == 0) total_time = 1;

    printf("\n\n");
    wizard_print_separator();
    printf("\n[+] Worker Statistics:\n");
    printf("    Work units completed: %d\n", work_count);
    printf("    Total keys processed: %.2e\n", (double)total_keys);
    printf("    Average speed: %.2f Mkeys/s\n",
           (double)total_keys / total_time / 1000000.0);
    printf("    Total runtime: %ld seconds\n", total_time);

    return 0;
}

/* ============================================================================
 * Non-Interactive Client Mode (spawned by server)
 * ============================================================================ */

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

    printf("[Auto-Client] Connecting to %s:%d\n", cfg.server_host, cfg.server_port);

    return wizard_client_run(&cfg);
}
