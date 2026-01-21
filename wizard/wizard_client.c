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

/* Stub search function - will be replaced by real implementation */
static int search_range_stub(const char *start, const char *end,
                              const wizard_config_t *cfg,
                              uint64_t *keys_checked, volatile int *stop_flag,
                              char *found_key, char *found_addr) {
    (void)found_key;
    (void)found_addr;

    /* Simulate search */
    uint64_t range_size = cfg->work_unit_size;
    uint64_t keys_per_sec = 50000000ULL;

    uint64_t total = 0;
    while (*stop_flag && total < range_size) {
        usleep(100000);
        uint64_t chunk = keys_per_sec / 10;
        total += chunk;
        if (!*stop_flag) break;
    }

    *keys_checked = total;
    (void)start; (void)end;
    return 0;
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

    printf("\n[+] Connecting to server %s:%d...\n",
           cfg->server_host, cfg->server_port);

    /* Initialize worker client */
    dist_worker_client_t client;
    if (dist_worker_init(&client, cfg->server_host, cfg->server_port,
                         sysinfo.cpu_score) != 0) {
        printf("[-] Failed to initialize worker client\n");
        return -1;
    }

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

    /* TODO: Receive configuration from server */
    /* For now, client must have config already */
    printf("[+] Using local configuration:\n");
    printf("    Puzzle: #%d (%d bits)\n", cfg->puzzle_number, cfg->bits);
    printf("    Target: %s\n", cfg->target_address);
    printf("    Mode: %s (%s)\n", cfg->mode, cfg->key_type);
    printf("    Threads: %d\n", cfg->threads);
    printf("    GPU: %d%%\n", cfg->gpu_percent);

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

        /* Search */
        int search_result;
        #ifdef KEYHUNT_SEARCH_IMPL
        search_result = keyhunt_search_range(
            range_start, range_end,
            cfg->target_address, cfg->mode, cfg->key_type,
            cfg->threads, cfg->gpu_percent,
            &keys_checked, &g_client_running,
            found_key, found_addr
        );
        #else
        search_result = search_range_stub(
            range_start, range_end,
            cfg, &keys_checked, &g_client_running,
            found_key, found_addr
        );
        #endif

        time_t unit_elapsed = time(NULL) - unit_start;
        if (unit_elapsed == 0) unit_elapsed = 1;

        total_keys += keys_checked;
        double speed = (double)keys_checked / unit_elapsed / 1000000.0;

        /* Report completion */
        if (dist_worker_report_done(&client, keys_checked, unit_elapsed * 1000) != 0) {
            printf("\n[-] Failed to report completion\n");
        }

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

        /* Periodic heartbeat */
        time_t now = time(NULL);
        if (now - last_heartbeat >= 30) {
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
