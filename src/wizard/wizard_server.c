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
        /* In real implementation, would mark matching units as COMPLETED */
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
    printf("\n");

    /* Main loop */
    time_t start_time = time(NULL);
    time_t last_checkpoint = start_time;
    time_t last_print = 0;
    time_t last_worker_stats = start_time;
    int last_worker_count = 0;

    while (g_server_running) {
        /* Process coordinator events */
        int status = dist_coordinator_process(&coord, 500);

        if (status == 1) {
            printf("\n\n[+] All work completed!\n");
            break;
        }

        /* Check if local client is still running */
        if (has_local_client && check_local_client(g_local_client_pid) == 0) {
            printf("\n\n[!] Local client exited unexpectedly, respawning...\n");

            /* Wait a moment before respawning */
            sleep(2);

            g_local_client_pid = spawn_local_client(cfg->server_port, cfg->auth_token);
            if (g_local_client_pid > 0) {
                printf("[+] Local client respawned (PID: %d)\n\n", g_local_client_pid);
            } else {
                printf("[-] Failed to respawn local client\n");
                has_local_client = false;
                g_local_client_pid = 0;
            }
        }

        /* Print stats every second */
        time_t now = time(NULL);
        if (now != last_print) {
            last_print = now;

            int workers, pending, completed;
            double throughput;
            dist_coordinator_stats(&coord, &workers, &pending, &completed, &throughput);

            /* Get detailed CPU/GPU speeds */
            double cpu_speed, gpu_speed, combined_speed;
            dist_coordinator_get_speed_stats(&coord, &cpu_speed, &gpu_speed, &combined_speed);

            double progress = (double)completed / (double)num_units * 100.0;
            time_t elapsed = now - start_time;

            /* Calculate ETA using combined speed */
            double keys_done = (double)completed * cfg->work_unit_size;
            double keys_total = (double)num_units * cfg->work_unit_size;
            double keys_remaining = keys_total - keys_done;
            double effective_speed = combined_speed > 0 ? combined_speed : throughput;
            double eta_sec = (effective_speed > 0) ? (keys_remaining / (effective_speed * 1000000.0)) : 0;

            int eta_days = (int)(eta_sec / 86400);
            int eta_hours = (int)((eta_sec - eta_days * 86400) / 3600);
            int eta_mins = (int)((eta_sec - eta_days * 86400 - eta_hours * 3600) / 60);

            /* Full dashboard redraw every 5 seconds or on worker change */
            bool full_redraw = (now - last_worker_stats >= 5) || (workers != last_worker_count);

            if (full_redraw) {
                last_worker_stats = now;
                last_worker_count = workers;

                /* Clear screen and move to top */
                printf("\033[2J\033[H");

                /* Header */
                printf("\033[36m╔══════════════════════════════════════════════════════════════════╗\033[0m\n");
                printf("\033[36m║\033[0m\033[1m  KEYHUNT SERVER - Puzzle #%-3d (%d bits)                         \033[0m\033[36m║\033[0m\n",
                       cfg->puzzle_number, cfg->bits);
                printf("\033[36m╠══════════════════════════════════════════════════════════════════╣\033[0m\n");

                /* Main progress bar (50 chars) */
                const int main_bar_width = 50;
                int main_filled = (int)(progress / 100.0 * main_bar_width);
                if (main_filled > main_bar_width) main_filled = main_bar_width;

                printf("\033[36m║\033[0m  Progress: [");
                for (int i = 0; i < main_bar_width; i++) {
                    if (i < main_filled) printf("\033[42m \033[0m");  /* Green background */
                    else printf("\033[47m \033[0m");  /* Gray background */
                }
                printf("] \033[1m%5.1f%%\033[0m \033[36m║\033[0m\n", progress);

                /* Stats line 1 */
                char eta_str[32];
                if (eta_days > 0) snprintf(eta_str, sizeof(eta_str), "%dd %dh %dm", eta_days, eta_hours, eta_mins);
                else if (eta_hours > 0) snprintf(eta_str, sizeof(eta_str), "%dh %dm", eta_hours, eta_mins);
                else snprintf(eta_str, sizeof(eta_str), "%dm", eta_mins);

                printf("\033[36m║\033[0m  Elapsed: \033[1m%02ld:%02ld:%02ld\033[0m    ETA: \033[1m%-12s\033[0m    Units: \033[1m%d/%d\033[0m     \033[36m║\033[0m\n",
                       elapsed / 3600, (elapsed % 3600) / 60, elapsed % 60,
                       eta_str, completed, num_units);

                /* Stats line 2 - Speed */
                printf("\033[36m╠══════════════════════════════════════════════════════════════════╣\033[0m\n");
                printf("\033[36m║\033[0m  \033[33mCPU:\033[0m %7.1f Mk/s   \033[35mGPU:\033[0m %7.1f Mk/s   \033[32mTotal:\033[1m %7.1f Mk/s\033[0m   \033[36m║\033[0m\n",
                       cpu_speed, gpu_speed, combined_speed);

                /* Workers section */
                printf("\033[36m╠══════════════════════════════════════════════════════════════════╣\033[0m\n");
                printf("\033[36m║\033[0m  \033[1mWorkers: %d connected\033[0m                                            \033[36m║\033[0m\n", workers);
                printf("\033[36m╠══════════════════════════════════════════════════════════════════╣\033[0m\n");

                if (workers > 0) {
                    /* Worker table header */
                    printf("\033[36m║\033[0m  \033[36m%-3s %-16s %8s %8s %8s  %-12s\033[0m \033[36m║\033[0m\n",
                           "ID", "Host", "CPU", "GPU", "Total", "Progress");
                    printf("\033[36m║\033[0m  \033[2m─── ──────────────── ──────── ──────── ──────── ────────────\033[0m \033[36m║\033[0m\n");

                    /* Show each worker with mini progress bar */
                    for (int i = 0; i < coord.worker_count && i < 8; i++) {
                        dist_worker_t *w = &coord.workers[i];
                        if (!w->connected) continue;

                        double worker_total = w->cpu_speed_mkeys + w->gpu_speed_mkeys;

                        /* Mini sparkline based on speed (10 chars) */
                        char sparkline[16];
                        int spark_filled = (int)(worker_total / (combined_speed > 0 ? combined_speed : 1) * 10);
                        if (spark_filled > 10) spark_filled = 10;
                        for (int j = 0; j < 10; j++) {
                            if (j < spark_filled) sparkline[j] = '|';
                            else sparkline[j] = ' ';
                        }
                        sparkline[10] = '\0';

                        printf("\033[36m║\033[0m  %-3d %-16.16s %7.1f  %7.1f  \033[32m%7.1f\033[0m  [\033[33m%-10s\033[0m] \033[36m║\033[0m\n",
                               w->id,
                               w->hostname[0] ? w->hostname : "localhost",
                               w->cpu_speed_mkeys,
                               w->gpu_speed_mkeys,
                               worker_total,
                               sparkline);
                    }

                    if (coord.worker_count > 8) {
                        printf("\033[36m║\033[0m  \033[2m... and %d more workers\033[0m                                       \033[36m║\033[0m\n",
                               coord.worker_count - 8);
                    }
                } else {
                    printf("\033[36m║\033[0m  \033[2mWaiting for workers to connect...\033[0m                             \033[36m║\033[0m\n");
                }

                /* Footer */
                printf("\033[36m╠══════════════════════════════════════════════════════════════════╣\033[0m\n");
                printf("\033[36m║\033[0m  Keys checked: \033[1m%.2e\033[0m    Keys/unit: \033[1m%.2e\033[0m              \033[36m║\033[0m\n",
                       keys_done, (double)cfg->work_unit_size);
                printf("\033[36m╚══════════════════════════════════════════════════════════════════╝\033[0m\n");
                printf("\n\033[2mPress Ctrl+C to stop\033[0m\n");
            } else {
                /* Quick single-line update between full redraws */
                printf("\r\033[K[%02ld:%02ld:%02ld] %5.1f%% | W:%d | \033[1;32m%.1f\033[0m Mk/s | %d/%d units",
                       elapsed / 3600, (elapsed % 3600) / 60, elapsed % 60,
                       progress, workers, combined_speed, completed, num_units);
            }
            fflush(stdout);
        }

        /* Checkpoint - save both wizard config and coordinator state */
        if (now - last_checkpoint >= cfg->checkpoint_interval_sec) {
            cfg->local_completed = coord.work_units_completed;
            wizard_config_save(cfg, "keyhunt_wizard.json");
            dist_coordinator_save_state(&coord, state_file);
            last_checkpoint = now;
        }
    }

    /* Shutdown */
    g_server_running = 0;

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
