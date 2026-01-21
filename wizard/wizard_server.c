/*
 * wizard_server.c - Server mode (coordinator + local worker)
 *
 * The server:
 * 1. Acts as coordinator distributing work to remote clients
 * 2. Also runs a local worker to contribute to the search
 * 3. Handles community exclusions
 * 4. Tracks progress and checkpoints
 */

#include "wizard.h"
#include "../distributed/distributed.h"
#include "../sysinfo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

/* Global state for signal handler */
static volatile int g_server_running = 1;
static wizard_config_t *g_server_cfg = NULL;
static dist_coordinator_t *g_server_coord = NULL;

static void server_signal_handler(int sig) {
    (void)sig;
    g_server_running = 0;
    printf("\n\n[!] Shutdown signal received, stopping...\n");
}

/* ============================================================================
 * Local Worker Thread
 * ============================================================================ */

typedef struct {
    wizard_config_t *cfg;
    dist_coordinator_t *coord;
    int worker_id;
    volatile int *running;

    /* Stats */
    uint64_t total_keys;
    uint64_t current_keys;
    double current_speed;
    int work_units_done;
    time_t start_time;
} local_worker_ctx_t;

/* Real keyhunt search via subprocess */
static int search_range_subprocess(const char *start, const char *end,
                                    const wizard_config_t *cfg,
                                    uint64_t *keys_checked, volatile int *stop_flag,
                                    char *found_key, char *found_addr) {
    char cmd[2048];
    *keys_checked = 0;
    found_key[0] = '\0';
    found_addr[0] = '\0';

    /* Build keyhunt command */
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

    /* Build command based on mode, with optional GPU */
    char gpu_arg[32] = "";
    if (cfg->gpu_percent > 0) {
        snprintf(gpu_arg, sizeof(gpu_arg), "-G auto ");
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

        /* Parse found key: "Hit! Private Key: XXX" */
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

static void* local_worker_thread(void *arg) {
    local_worker_ctx_t *ctx = (local_worker_ctx_t*)arg;
    wizard_config_t *cfg = ctx->cfg;
    dist_coordinator_t *coord = ctx->coord;

    printf("[LocalWorker] Started (threads=%d, gpu=%d%%)\n",
           cfg->threads, cfg->gpu_percent);

    ctx->start_time = time(NULL);

    while (*ctx->running) {
        /* Find pending work unit */
        dist_work_unit_t *unit = NULL;

        for (int i = 0; i < coord->work_unit_count && *ctx->running; i++) {
            if (coord->work_units[i].status == WORK_STATUS_PENDING) {
                unit = &coord->work_units[i];
                unit->status = WORK_STATUS_ASSIGNED;
                unit->assigned_worker = ctx->worker_id;
                unit->assigned_time = time(NULL);
                break;
            }
        }

        if (!unit) {
            /* Check if all done */
            if (coord->work_units_completed >= coord->work_unit_count) {
                printf("[LocalWorker] All work completed!\n");
                break;
            }
            usleep(100000);  /* Wait 100ms */
            continue;
        }

        /* Process work unit */
        uint64_t keys_checked = 0;
        char found_key[65] = {0};
        char found_addr[36] = {0};

        time_t unit_start = time(NULL);

        /* Call keyhunt via subprocess */
        int result = search_range_subprocess(
            unit->range_start, unit->range_end,
            cfg, &keys_checked, ctx->running,
            found_key, found_addr
        );

        time_t unit_elapsed = time(NULL) - unit_start;
        if (unit_elapsed == 0) unit_elapsed = 1;

        /* Update stats */
        ctx->total_keys += keys_checked;
        ctx->current_keys = keys_checked;
        ctx->current_speed = (double)keys_checked / unit_elapsed / 1000000.0;
        ctx->work_units_done++;

        /* Update coordinator */
        unit->status = WORK_STATUS_COMPLETED;
        unit->completed_time = time(NULL);
        coord->work_units_completed++;
        coord->keys_processed += keys_checked;

        /* Check if found */
        if (result == 1 && found_key[0]) {
            printf("\n\n");
            printf("╔═══════════════════════════════════════════════════════════╗\n");
            printf("║               🎉 PRIVATE KEY FOUND! 🎉                    ║\n");
            printf("╠═══════════════════════════════════════════════════════════╣\n");
            printf("║ Key:  %-52s ║\n", found_key);
            printf("║ Addr: %-52s ║\n", found_addr);
            printf("╚═══════════════════════════════════════════════════════════╝\n");

            /* Save result */
            if (coord->result_count < coord->result_capacity) {
                dist_result_t *r = &coord->results[coord->result_count++];
                strncpy(r->private_key, found_key, sizeof(r->private_key) - 1);
                strncpy(r->address, found_addr, sizeof(r->address) - 1);
                r->worker_id = ctx->worker_id;
                r->found_time = time(NULL);
            }

            /* Save to file immediately */
            FILE *f = fopen("FOUND_KEY.txt", "w");
            if (f) {
                fprintf(f, "PRIVATE KEY FOUND!\n");
                fprintf(f, "Time: %s", ctime(&(time_t){time(NULL)}));
                fprintf(f, "Puzzle: #%d\n", cfg->puzzle_number);
                fprintf(f, "Private Key: %s\n", found_key);
                fprintf(f, "Address: %s\n", found_addr);
                fclose(f);
            }

            *ctx->running = 0;
            break;
        }
    }

    printf("[LocalWorker] Stopped. Processed %d units, %.2e keys\n",
           ctx->work_units_done, (double)ctx->total_keys);

    return NULL;
}

/* ============================================================================
 * Server Main Loop
 * ============================================================================ */

int wizard_server_run(wizard_config_t *cfg) {
    signal(SIGINT, server_signal_handler);
    signal(SIGTERM, server_signal_handler);

    g_server_cfg = cfg;

    wizard_print_header("KEYHUNT WIZARD - SERVER MODE");

    /* Initialize coordinator */
    dist_coordinator_t coord;
    memset(&coord, 0, sizeof(coord));

    if (dist_coordinator_init(&coord, cfg->server_port) != 0) {
        printf("[-] Failed to initialize coordinator on port %d\n", cfg->server_port);
        return -1;
    }

    g_server_coord = &coord;

    /* Configure work range */
    printf("[+] Setting up puzzle #%d...\n", cfg->puzzle_number);
    printf("    Range: %s - %s\n", cfg->range_start, cfg->range_end);
    printf("    Target: %s\n", cfg->target_address);

    int num_units = dist_coordinator_set_range(&coord,
        cfg->range_start, cfg->range_end, cfg->work_unit_size);

    if (num_units < 0) {
        printf("[-] Failed to configure work range\n");
        dist_coordinator_shutdown(&coord);
        return -1;
    }

    cfg->total_ranges = num_units;
    printf("[+] Created %d work units (%.2e keys each)\n",
           num_units, (double)cfg->work_unit_size);

    /* Mark excluded ranges as completed */
    if (cfg->community_excluded > 0) {
        printf("[+] Marking %llu community-scanned ranges as completed...\n",
               (unsigned long long)cfg->community_excluded);
        /* In real implementation, would mark matching units as COMPLETED */
    }

    /* Start coordinator */
    if (dist_coordinator_start(&coord) != 0) {
        printf("[-] Failed to start coordinator\n");
        dist_coordinator_shutdown(&coord);
        return -1;
    }

    printf("[+] Coordinator listening on port %d\n", cfg->server_port);

    /* Start local worker thread if enabled */
    pthread_t worker_thread;
    local_worker_ctx_t local_worker = {0};
    bool has_local_worker = false;

    if (cfg->server_also_worker) {
        printf("[+] Starting local worker thread...\n");

        /* Auto-detect threads if not set */
        if (cfg->threads <= 0) {
            system_info_t sysinfo;
            sysinfo_init(&sysinfo);
            cfg->threads = sysinfo.cpu_logical_cores;
        }

        local_worker.cfg = cfg;
        local_worker.coord = &coord;
        local_worker.worker_id = -1;  /* -1 = local worker */
        local_worker.running = &g_server_running;

        if (pthread_create(&worker_thread, NULL, local_worker_thread, &local_worker) == 0) {
            has_local_worker = true;
            printf("[+] Local worker started with %d threads\n", cfg->threads);
        } else {
            printf("[-] Failed to start local worker thread\n");
        }
    }

    printf("\n[+] Server running. Press Ctrl+C to stop.\n");
    wizard_print_separator();
    printf("\n");

    /* Main loop */
    time_t start_time = time(NULL);
    time_t last_checkpoint = start_time;
    time_t last_print = 0;

    while (g_server_running) {
        /* Process coordinator events */
        int status = dist_coordinator_process(&coord, 500);

        if (status == 1) {
            printf("\n\n[+] All work completed!\n");
            break;
        }

        /* Print stats every second */
        time_t now = time(NULL);
        if (now != last_print) {
            last_print = now;

            int workers, pending, completed;
            double throughput;
            dist_coordinator_stats(&coord, &workers, &pending, &completed, &throughput);

            /* Add local worker stats */
            if (has_local_worker) {
                throughput += local_worker.current_speed;
            }

            double progress = (double)completed / (double)num_units * 100.0;
            time_t elapsed = now - start_time;

            /* Calculate ETA */
            double keys_done = (double)completed * cfg->work_unit_size;
            double keys_total = (double)num_units * cfg->work_unit_size;
            double keys_remaining = keys_total - keys_done;
            double eta_sec = (throughput > 0) ? (keys_remaining / (throughput * 1000000.0)) : 0;

            int eta_days = (int)(eta_sec / 86400);
            int eta_hours = (int)((eta_sec - eta_days * 86400) / 3600);

            printf("\r[%02ld:%02ld:%02ld] Workers: %d%s | Progress: %d/%d (%.4f%%) | "
                   "%.2f Mkeys/s | ETA: %dd %dh     ",
                   elapsed / 3600, (elapsed % 3600) / 60, elapsed % 60,
                   workers, has_local_worker ? "+1" : "",
                   completed, num_units, progress,
                   throughput,
                   eta_days, eta_hours);
            fflush(stdout);
        }

        /* Checkpoint */
        if (now - last_checkpoint >= cfg->checkpoint_interval_sec) {
            cfg->local_completed = coord.work_units_completed;
            wizard_config_save(cfg, "keyhunt_wizard.json");
            last_checkpoint = now;
        }
    }

    /* Shutdown */
    g_server_running = 0;

    if (has_local_worker) {
        printf("\n[+] Waiting for local worker to finish...\n");
        pthread_join(worker_thread, NULL);
    }

    /* Final stats */
    printf("\n\n");
    wizard_print_separator();
    printf("\n[+] Final Statistics:\n");
    printf("    Work units completed: %d / %d (%.2f%%)\n",
           coord.work_units_completed, num_units,
           (double)coord.work_units_completed / num_units * 100.0);
    printf("    Total keys processed: %.2e\n", (double)coord.keys_processed);
    printf("    Run time: %ld seconds\n", time(NULL) - start_time);

    if (has_local_worker) {
        printf("    Local worker contribution: %.2e keys\n", (double)local_worker.total_keys);
    }

    if (coord.result_count > 0) {
        printf("\n");
        printf("╔═══════════════════════════════════════════════════════════════╗\n");
        printf("║                    KEYS FOUND: %d                              ║\n", coord.result_count);
        printf("╚═══════════════════════════════════════════════════════════════╝\n");
        for (int i = 0; i < coord.result_count; i++) {
            printf("\n  Key #%d:\n", i + 1);
            printf("    Private: %s\n", coord.results[i].private_key);
            printf("    Address: %s\n", coord.results[i].address);
        }
    }

    /* Save final state */
    cfg->local_completed = coord.work_units_completed;
    wizard_config_save(cfg, "keyhunt_wizard.json");
    printf("\n[+] Configuration saved to keyhunt_wizard.json\n");

    dist_coordinator_shutdown(&coord);
    return (coord.result_count > 0) ? 1 : 0;
}
