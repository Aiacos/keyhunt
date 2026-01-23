/*
 * wizard_server.c - Server mode (coordinator only, spawns local client)
 *
 * The server:
 * 1. Acts as coordinator distributing work to remote clients
 * 2. Spawns a local client process for computation (separate process)
 * 3. Handles community exclusions
 * 4. Tracks progress and checkpoints
 *
 * Architecture: Server does ONLY orchestration, no computation.
 * Computation is handled by spawned client process via TCP.
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
#include <errno.h>
#include <sys/wait.h>
#include <linux/limits.h>  /* PATH_MAX */

/* Fallback if PATH_MAX not defined */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* Global state for signal handler - use sig_atomic_t for signal safety */
static volatile sig_atomic_t g_server_running = 1;
static volatile sig_atomic_t g_shutdown_requested = 0;
static wizard_config_t *g_server_cfg = NULL;
static dist_coordinator_t *g_server_coord = NULL;
static volatile pid_t g_local_client_pid = 0;

static void server_signal_handler(int sig) {
    (void)sig;
    g_server_running = 0;
    g_shutdown_requested = 1;

    /* Signal handler should be minimal - just set flags
     * The main loop will handle client termination safely */
    /* Note: write() is async-signal-safe, printf is not */
    const char msg[] = "\n\n[!] Shutdown signal received, stopping...\n";
    (void)write(STDERR_FILENO, msg, sizeof(msg) - 1);

    /* Forward signal to local client if spawned - read PID atomically */
    pid_t pid = g_local_client_pid;
    if (pid > 0) {
        kill(pid, SIGTERM);
    }
}

/* ============================================================================
 * Local Client Spawning (fork/exec separate process)
 * ============================================================================ */

/**
 * Get the path to the current executable.
 * Uses /proc/self/exe on Linux.
 *
 * @param buf Output buffer for path
 * @param bufsz Size of buffer
 * @return 0 on success, -1 on error
 */
static int get_executable_path(char *buf, size_t bufsz) {
    ssize_t len = readlink("/proc/self/exe", buf, bufsz - 1);
    if (len < 0) {
        /* Fallback to ./keyhunt if readlink fails */
        strncpy(buf, "./keyhunt", bufsz - 1);
        buf[bufsz - 1] = '\0';
        return 0;
    }
    buf[len] = '\0';
    return 0;
}

/**
 * Spawn a local client process that connects back to this server via TCP.
 * This separates computation from orchestration for better performance.
 *
 * @param port Server port to connect to
 * @return PID of spawned process, or -1 on error
 */
static pid_t spawn_local_client(int port, const char *auth_token) {
    char host_port[64];
    snprintf(host_port, sizeof(host_port), "127.0.0.1:%d", port);

    /* Get the executable path dynamically */
    char exe_path[PATH_MAX];
    if (get_executable_path(exe_path, sizeof(exe_path)) != 0) {
        fprintf(stderr, "[-] Failed to get executable path\n");
        return -1;
    }

    pid_t pid = fork();

    if (pid < 0) {
        perror("[-] fork failed");
        return -1;
    }

    if (pid == 0) {
        /* Child process: exec keyhunt in client mode */
        /* Small delay to ensure server is ready */
        usleep(500000);  /* 500ms */

        /* Pass auth token via environment variable if set */
        if (auth_token && auth_token[0] != '\0') {
            setenv("KEYHUNT_AUTH_TOKEN", auth_token, 1);
        }

        execl(exe_path, "keyhunt", "--wizard-client", host_port, (char *)NULL);

        /* If execl returns, it failed */
        perror("[-] execl failed");
        _exit(1);
    }

    /* Parent: return child PID */
    return pid;
}

/**
 * Wait for local client to finish (non-blocking check)
 * @return 1 if still running, 0 if exited, -1 on error
 */
static int check_local_client(pid_t pid) {
    if (pid <= 0) return 0;

    int status;
    pid_t result = waitpid(pid, &status, WNOHANG);

    if (result == 0) {
        return 1;  /* Still running */
    } else if (result == pid) {
        if (WIFEXITED(status)) {
            printf("[LocalClient] Exited with code %d\n", WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("[LocalClient] Killed by signal %d\n", WTERMSIG(status));
        }
        return 0;  /* Exited */
    }

    return -1;  /* Error */
}

/* ============================================================================
 * Worker Health Monitoring
 * ============================================================================ */

#define WORKER_TIMEOUT_SEC 120  /* Mark worker as stale after 2 minutes without heartbeat */
#define WORK_UNIT_TIMEOUT_SEC 600  /* Reclaim work unit after 10 minutes */

/**
 * Check worker health and reclaim work from timed-out workers.
 * Returns number of work units reclaimed.
 */
static int check_worker_health(dist_coordinator_t *coord, time_t now) {
    int reclaimed = 0;

    pthread_mutex_lock(&coord->work_mutex);

    for (int i = 0; i < coord->worker_count; i++) {
        dist_worker_t *w = &coord->workers[i];
        if (!w->connected) continue;

        /* Check if worker has timed out (last_heartbeat is in milliseconds) */
        time_t last_seen_sec = (time_t)(w->last_heartbeat / 1000);
        if (last_seen_sec > 0 && (now - last_seen_sec) > WORKER_TIMEOUT_SEC) {
            /* Worker timed out - mark as disconnected */
            w->connected = false;

            /* Reclaim any work assigned to this worker */
            if (w->current_work_id >= 0 && w->current_work_id < coord->work_unit_count) {
                dist_work_unit_t *unit = &coord->work_units[w->current_work_id];
                if (unit->status == WORK_STATUS_ASSIGNED && unit->assigned_worker == w->id) {
                    unit->status = WORK_STATUS_PENDING;
                    unit->assigned_worker = -1;
                    coord->work_units_pending++;
                    reclaimed++;
                }
            }
            w->current_work_id = -1;
        }
    }

    /* Also check for long-running work units (in case heartbeat still arriving but work stuck) */
    /* Note: assigned_time is in milliseconds */
    for (int i = 0; i < coord->work_unit_count; i++) {
        dist_work_unit_t *unit = &coord->work_units[i];
        if (unit->status == WORK_STATUS_ASSIGNED) {
            time_t assigned_time_sec = (time_t)(unit->assigned_time / 1000);
            if (assigned_time_sec > 0 && (now - assigned_time_sec) > WORK_UNIT_TIMEOUT_SEC) {
                /* Work unit has been assigned too long - reclaim it */
                unit->status = WORK_STATUS_PENDING;
                unit->assigned_worker = -1;
                coord->work_units_pending++;
                reclaimed++;
            }
        }
    }

    pthread_mutex_unlock(&coord->work_mutex);

    return reclaimed;
}

/**
 * Count active (healthy) workers
 */
static int count_active_workers(dist_coordinator_t *coord, time_t now) {
    int active = 0;
    for (int i = 0; i < coord->worker_count; i++) {
        dist_worker_t *w = &coord->workers[i];
        if (w->connected) {
            time_t last_seen = (time_t)w->last_heartbeat;
            /* Consider worker active if heartbeat within last 60 seconds */
            if (last_seen == 0 || (now - last_seen) < 60) {
                active++;
            }
        }
    }
    return active;
}

/* ============================================================================
 * Progress History CSV Logging
 * ============================================================================ */

/**
 * Append a progress entry to the history CSV file.
 * Creates the file with headers if it doesn't exist.
 * Format: timestamp, keys_checked, speed_mkeys, active_workers
 *
 * @param puzzle_number Puzzle number (used in filename)
 * @param keys_checked Total keys checked so far
 * @param speed_mkeys Current speed in Mkeys/s
 * @param active_workers Number of active workers
 * @return 0 on success, -1 on error
 */
static int append_progress_history(int puzzle_number, uint64_t keys_checked,
                                   double speed_mkeys, int active_workers) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "progress_history_%d.csv", puzzle_number);

    /* Check if file exists to determine if we need headers */
    int needs_header = (access(filepath, F_OK) != 0);

    FILE *f = fopen(filepath, "a");
    if (!f) {
        return -1;
    }

    /* Write header if new file */
    if (needs_header) {
        fprintf(f, "timestamp,keys_checked,speed_mkeys,active_workers\n");
    }

    /* Write data row with ISO 8601 timestamp */
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", tm_info);

    fprintf(f, "%s,%llu,%.2f,%d\n",
            timestamp,
            (unsigned long long)keys_checked,
            speed_mkeys,
            active_workers);

    fclose(f);
    return 0;
}

/* ============================================================================
 * Stable UI Rendering
 * ============================================================================ */

#define UI_WIDTH 70  /* Fixed width for consistent formatting */

/**
 * Render the dashboard UI with cursor positioning (no full clear).
 * This eliminates flicker while maintaining a clean display.
 */
static void render_dashboard(const wizard_config_t *cfg, const dist_coordinator_t *coord,
                             int num_units, time_t start_time, time_t now,
                             bool full_redraw) {
    int workers, pending, completed;
    double throughput;
    dist_coordinator_stats(coord, &workers, &pending, &completed, &throughput);

    double cpu_speed, gpu_speed, combined_speed;
    dist_coordinator_get_speed_stats(coord, &cpu_speed, &gpu_speed, &combined_speed);

    double progress = (num_units > 0) ? ((double)completed / (double)num_units * 100.0) : 0.0;
    if (progress > 100.0) progress = 100.0;
    time_t elapsed = now - start_time;

    /* Calculate ETA */
    double keys_done = (double)completed * cfg->work_unit_size;
    double keys_total = (double)num_units * cfg->work_unit_size;
    double keys_remaining = keys_total - keys_done;
    double effective_speed = combined_speed > 0 ? combined_speed : throughput;
    double eta_sec = (effective_speed > 0) ? (keys_remaining / (effective_speed * 1000000.0)) : 0;

    int eta_days = (int)(eta_sec / 86400);
    int eta_hours = (int)((eta_sec - eta_days * 86400) / 3600);
    int eta_mins = (int)((eta_sec - eta_days * 86400 - eta_hours * 3600) / 60);

    char eta_str[32];
    if (eta_sec <= 0 || eta_sec > 365*86400) {
        snprintf(eta_str, sizeof(eta_str), "--:--");
    } else if (eta_days > 0) {
        snprintf(eta_str, sizeof(eta_str), "%dd %dh %dm", eta_days, eta_hours, eta_mins);
    } else if (eta_hours > 0) {
        snprintf(eta_str, sizeof(eta_str), "%dh %dm", eta_hours, eta_mins);
    } else {
        snprintf(eta_str, sizeof(eta_str), "%dm", eta_mins);
    }

    if (full_redraw) {
        /* Move cursor to home position and clear screen */
        printf("\033[H\033[J");

        /* Header */
        printf("\033[36m╔════════════════════════════════════════════════════════════════════╗\033[0m\n");
        printf("\033[36m║\033[0m\033[1m  KEYHUNT SERVER - Puzzle #%-3d (%d bits)                           \033[0m\033[36m║\033[0m\n",
               cfg->puzzle_number, cfg->bits);
        printf("\033[36m╠════════════════════════════════════════════════════════════════════╣\033[0m\n");

        /* Progress bar (50 chars) */
        const int bar_width = 50;
        int filled = (int)(progress / 100.0 * bar_width);
        if (filled < 0) filled = 0;
        if (filled > bar_width) filled = bar_width;

        printf("\033[36m║\033[0m  Progress: [");
        for (int i = 0; i < bar_width; i++) {
            if (i < filled) printf("\033[42m \033[0m");
            else printf("\033[100m \033[0m");
        }
        printf("] %5.1f%% \033[36m║\033[0m\n", progress);

        /* Time stats */
        printf("\033[36m║\033[0m  Elapsed: \033[1m%02ld:%02ld:%02ld\033[0m    ETA: \033[1m%-12s\033[0m   Units: \033[1m%d/%d\033[0m      \033[36m║\033[0m\n",
               elapsed / 3600, (elapsed % 3600) / 60, elapsed % 60,
               eta_str, completed, num_units);

        /* Speed stats */
        printf("\033[36m╠════════════════════════════════════════════════════════════════════╣\033[0m\n");
        printf("\033[36m║\033[0m  \033[33mCPU:\033[0m %8.2f Mk/s  \033[35mGPU:\033[0m %8.2f Mk/s  \033[32mTotal:\033[1m %8.2f Mk/s\033[0m \033[36m║\033[0m\n",
               cpu_speed, gpu_speed, combined_speed);

        /* Workers header */
        printf("\033[36m╠════════════════════════════════════════════════════════════════════╣\033[0m\n");

        int active_workers = count_active_workers((dist_coordinator_t*)coord, now);
        printf("\033[36m║\033[0m  \033[1mWorkers: %d connected (%d active)\033[0m                                \033[36m║\033[0m\n",
               workers, active_workers);
        printf("\033[36m╠════════════════════════════════════════════════════════════════════╣\033[0m\n");

        if (workers > 0) {
            /* Worker table header */
            printf("\033[36m║\033[0m  \033[2m%-3s %-14s %7s %7s %7s %6s  %-8s\033[0m   \033[36m║\033[0m\n",
                   "ID", "Host", "CPU", "GPU", "Total", "Keys", "Status");
            printf("\033[36m║\033[0m  \033[2m─── ────────────── ─────── ─────── ─────── ────── ────────\033[0m   \033[36m║\033[0m\n");

            /* Create snapshot of worker speeds under mutex for consistent display.
             * This ensures the worker row speeds match the aggregated totals. */
            double worker_cpu_speeds[6] = {0};
            double worker_gpu_speeds[6] = {0};
            int snapshot_count = 0;

            dist_coordinator_t *mutable_coord = (dist_coordinator_t *)coord;
            pthread_mutex_lock(&mutable_coord->stats_mutex);
            for (int i = 0; i < coord->worker_count && snapshot_count < 6; i++) {
                const dist_worker_t *w = &coord->workers[i];
                if (!w->connected && w->keys_processed == 0) continue;
                worker_cpu_speeds[snapshot_count] = w->cpu_speed_mkeys;
                worker_gpu_speeds[snapshot_count] = w->gpu_speed_mkeys;
                snapshot_count++;
            }
            pthread_mutex_unlock(&mutable_coord->stats_mutex);

            int shown = 0;
            int speed_idx = 0;
            for (int i = 0; i < coord->worker_count && shown < 6; i++) {
                const dist_worker_t *w = &coord->workers[i];
                if (!w->connected && w->keys_processed == 0) continue;

                /* Use snapshot speeds for consistency with aggregated totals */
                double w_cpu_speed = worker_cpu_speeds[speed_idx];
                double w_gpu_speed = worker_gpu_speeds[speed_idx];
                speed_idx++;
                double worker_total = w_cpu_speed + w_gpu_speed;

                /* Status indicator */
                const char *status;
                const char *status_color;
                /* last_heartbeat is in milliseconds, convert to seconds for comparison */
                time_t last_seen_sec = (time_t)(w->last_heartbeat / 1000);
                time_t stale_threshold = 60;  /* seconds without heartbeat = stale */

                if (!w->connected) {
                    status = "OFFLINE";
                    status_color = "\033[31m";  /* Red */
                } else if (last_seen_sec > 0 && (now - last_seen_sec) > stale_threshold) {
                    status = "STALE";
                    status_color = "\033[33m";  /* Yellow */
                } else if (w->current_work_id >= 0) {
                    status = "WORKING";
                    status_color = "\033[32m";  /* Green */
                } else {
                    status = "IDLE";
                    status_color = "\033[36m";  /* Cyan */
                }

                /* Truncate hostname for display */
                char host_display[15];
                strncpy(host_display, w->hostname[0] ? w->hostname : "localhost", 14);
                host_display[14] = '\0';

                printf("\033[36m║\033[0m  %-3d %-14s %6.1f  %6.1f  \033[32m%6.1f\033[0m  %5.1fe %s%-8s\033[0m \033[36m║\033[0m\n",
                       w->id,
                       host_display,
                       w_cpu_speed,
                       w_gpu_speed,
                       worker_total,
                       (double)w->keys_processed / 1e9,
                       status_color,
                       status);
                shown++;
            }

            if (coord->worker_count > 6) {
                printf("\033[36m║\033[0m  \033[2m... and %d more workers\033[0m                                         \033[36m║\033[0m\n",
                       coord->worker_count - 6);
            }
        } else {
            printf("\033[36m║\033[0m  \033[2mWaiting for workers to connect...\033[0m                               \033[36m║\033[0m\n");
        }

        /* Footer */
        printf("\033[36m╠════════════════════════════════════════════════════════════════════╣\033[0m\n");
        printf("\033[36m║\033[0m  Keys checked: \033[1m%.3e\033[0m   Keys/unit: \033[1m%.2e\033[0m               \033[36m║\033[0m\n",
               keys_done, (double)cfg->work_unit_size);
        printf("\033[36m╚════════════════════════════════════════════════════════════════════╝\033[0m\n");
        printf("\n\033[2mPress Ctrl+C to stop\033[0m\n");

    } else {
        /* Quick update - just update key metrics on a single line */
        /* Save cursor, move to line 4 (progress line), update, restore */
        printf("\033[s");  /* Save cursor */
        printf("\033[4;1H");  /* Move to line 4 */

        /* Redraw progress bar */
        const int bar_width = 50;
        int filled = (int)(progress / 100.0 * bar_width);
        if (filled < 0) filled = 0;
        if (filled > bar_width) filled = bar_width;

        printf("\033[36m║\033[0m  Progress: [");
        for (int i = 0; i < bar_width; i++) {
            if (i < filled) printf("\033[42m \033[0m");
            else printf("\033[100m \033[0m");
        }
        printf("] %5.1f%% \033[36m║\033[0m", progress);

        /* Move to time line */
        printf("\033[5;1H");
        printf("\033[36m║\033[0m  Elapsed: \033[1m%02ld:%02ld:%02ld\033[0m    ETA: \033[1m%-12s\033[0m   Units: \033[1m%d/%d\033[0m      \033[36m║\033[0m",
               elapsed / 3600, (elapsed % 3600) / 60, elapsed % 60,
               eta_str, completed, num_units);

        /* Move to speed line */
        printf("\033[7;1H");
        printf("\033[36m║\033[0m  \033[33mCPU:\033[0m %8.2f Mk/s  \033[35mGPU:\033[0m %8.2f Mk/s  \033[32mTotal:\033[1m %8.2f Mk/s\033[0m \033[36m║\033[0m",
               cpu_speed, gpu_speed, combined_speed);

        printf("\033[u");  /* Restore cursor */
    }

    fflush(stdout);
}

/* ============================================================================
 * Server Main Loop
 * ============================================================================ */

int wizard_server_run(wizard_config_t *cfg) {
    signal(SIGINT, server_signal_handler);
    signal(SIGTERM, server_signal_handler);

    g_server_cfg = cfg;

    wizard_print_header("KEYHUNT WIZARD - SERVER MODE");
    printf("[i] Architecture: Server handles orchestration only\n");
    printf("[i] Computation is handled by separate client process(es)\n\n");

    /* Initialize coordinator */
    dist_coordinator_t coord;
    memset(&coord, 0, sizeof(coord));

    if (dist_coordinator_init(&coord, cfg->server_port) != 0) {
        printf("[-] Failed to initialize coordinator on port %d\n", cfg->server_port);
        return -1;
    }

    g_server_coord = &coord;

    /* Set job configuration for distribution to workers */
    dist_coordinator_set_job_config(&coord,
        cfg->target_address,
        cfg->mode,
        cfg->key_type,
        cfg->puzzle_number,
        cfg->bits);
    dist_coordinator_set_heartbeat_interval(&coord, 30);  /* 30 second heartbeats */

    /* Set authentication token if configured */
    if (cfg->auth_token[0] != '\0') {
        dist_coordinator_set_auth_token(&coord, cfg->auth_token);
    }

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

    /* Try to load previous state for resume capability */
    char state_file[256];
    dist_coordinator_get_state_path(&coord, state_file, sizeof(state_file));
    int load_result = dist_coordinator_load_state(&coord, state_file);
    if (load_result == 0) {
        printf("[+] Resumed from previous state: %d/%d units already completed\n",
               coord.work_units_completed, num_units);
    } else if (load_result == 1) {
        printf("[i] No previous state found, starting fresh\n");
    }

    /* Mark excluded ranges as completed */
    if (cfg->community_excluded > 0) {
        printf("[+] Marking %llu community-scanned ranges as completed...\n",
               (unsigned long long)cfg->community_excluded);
    }

    /* Start coordinator */
    if (dist_coordinator_start(&coord) != 0) {
        printf("[-] Failed to start coordinator\n");
        dist_coordinator_shutdown(&coord);
        return -1;
    }

    printf("[+] Coordinator listening on port %d\n", cfg->server_port);

    /* Spawn local client process if enabled */
    bool has_local_client = false;

    if (cfg->server_also_worker) {
        printf("[+] Spawning local client process...\n");

        g_local_client_pid = spawn_local_client(cfg->server_port, cfg->auth_token);

        if (g_local_client_pid > 0) {
            has_local_client = true;
            printf("[+] Local client spawned (PID: %d)\n", g_local_client_pid);
            printf("[i] Client will connect via TCP to localhost:%d\n", cfg->server_port);
        } else {
            printf("[-] Failed to spawn local client process\n");
        }
    }

    printf("\n[+] Server running. Press Ctrl+C to stop.\n");
    wizard_print_separator();

    /* Wait a moment before starting UI to let workers connect */
    sleep(2);

    /* Main loop */
    time_t start_time = time(NULL);
    time_t last_checkpoint = start_time;
    time_t last_full_redraw = 0;
    time_t last_health_check = start_time;
    int last_worker_count = 0;

    while (g_server_running) {
        /* Process coordinator events */
        int status = dist_coordinator_process(&coord, 500);

        if (status == 1) {
            printf("\033[H\033[J");  /* Clear screen */
            printf("\n\n[+] All work completed!\n");
            break;
        }

        /* Check if local client is still running */
        if (has_local_client && check_local_client(g_local_client_pid) == 0) {
            /* Log to a less intrusive place - will show on next full redraw */
            sleep(2);

            g_local_client_pid = spawn_local_client(cfg->server_port, cfg->auth_token);
            if (g_local_client_pid > 0) {
                /* Silently respawned */
            } else {
                has_local_client = false;
                g_local_client_pid = 0;
            }
        }

        time_t now = time(NULL);

        /* Worker health check every 30 seconds */
        if (now - last_health_check >= 30) {
            last_health_check = now;
            int reclaimed = check_worker_health(&coord, now);
            if (reclaimed > 0) {
                /* Work was reclaimed - will show on next update */
            }
        }

        /* Update display */
        int current_workers = 0;
        for (int i = 0; i < coord.worker_count; i++) {
            if (coord.workers[i].connected) current_workers++;
        }

        /* Full redraw every 3 seconds or on worker count change */
        bool full_redraw = (now - last_full_redraw >= 3) || (current_workers != last_worker_count);

        if (full_redraw) {
            last_full_redraw = now;
            last_worker_count = current_workers;
            render_dashboard(cfg, &coord, num_units, start_time, now, true);
        } else {
            /* Quick update every second */
            render_dashboard(cfg, &coord, num_units, start_time, now, false);
        }

        /* Checkpoint - save both wizard config and coordinator state */
        if (now - last_checkpoint >= cfg->checkpoint_interval_sec) {
            cfg->local_completed = coord.work_units_completed;
            wizard_config_save(cfg, "keyhunt_wizard.json");
            dist_coordinator_save_state(&coord, state_file);

            /* Append to progress history CSV for external graphing tools */
            double cpu_speed, gpu_speed, combined_speed;
            dist_coordinator_get_speed_stats(&coord, &cpu_speed, &gpu_speed, &combined_speed);
            append_progress_history(cfg->puzzle_number, coord.keys_processed,
                                    combined_speed, current_workers);

            last_checkpoint = now;
        }
    }

    /* Shutdown - immediately save state when shutdown requested */
    g_server_running = 0;

    /* Emergency checkpoint save - ensure we capture latest progress */
    if (g_shutdown_requested) {
        printf("\n[+] Saving state before shutdown...\n");
        cfg->local_completed = coord.work_units_completed;
        wizard_config_save(cfg, "keyhunt_wizard.json");
        dist_coordinator_save_state(&coord, state_file);
    }

    if (has_local_client && g_local_client_pid > 0) {
        printf("\n[+] Waiting for local client to finish...\n");
        kill(g_local_client_pid, SIGTERM);

        /* Wait up to 5 seconds for graceful shutdown */
        for (int i = 0; i < 50 && check_local_client(g_local_client_pid) == 1; i++) {
            usleep(100000);  /* 100ms */
        }

        /* Force kill if still running */
        if (check_local_client(g_local_client_pid) == 1) {
            printf("[!] Force killing local client...\n");
            kill(g_local_client_pid, SIGKILL);
            waitpid(g_local_client_pid, NULL, 0);
        }
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

    /* Print final worker statistics table */
    dist_coordinator_print_worker_stats(&coord);

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
    dist_coordinator_save_state(&coord, state_file);
    printf("\n[+] Configuration saved to keyhunt_wizard.json\n");
    printf("[+] Coordinator state saved to %s (can resume on restart)\n", state_file);

    dist_coordinator_shutdown(&coord);
    return (coord.result_count > 0) ? 1 : 0;
}
