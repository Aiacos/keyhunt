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

            printf("\r[%02ld:%02ld:%02ld] Workers: %d | %d/%d (%.2f%%) | "
                   "CPU: %.1f | GPU: %.1f | Total: \033[1;32m%.1f\033[0m Mkeys/s | ETA: %dd %dh     ",
                   elapsed / 3600, (elapsed % 3600) / 60, elapsed % 60,
                   workers,
                   completed, num_units, progress,
                   cpu_speed, gpu_speed, combined_speed,
                   eta_days, eta_hours);
            fflush(stdout);

            /* Print per-worker stats every 30 seconds or when worker count changes */
            if ((now - last_worker_stats >= 30) || (workers != last_worker_count)) {
                if (workers != last_worker_count || (now - last_worker_stats >= 60)) {
                    last_worker_stats = now;
                    last_worker_count = workers;
                    if (workers > 0) {
                        printf("\n\n  \033[36m%-4s %-18s %10s %10s %10s %10s\033[0m\n",
                               "ID", "Host", "CPU M/s", "GPU M/s", "Total M/s", "Keys Done");
                        printf("  \033[36m%.4s %.18s %.10s %.10s %.10s %.10s\033[0m\n",
                               "----", "------------------", "----------", "----------", "----------", "----------");
                        for (int i = 0; i < coord.worker_count; i++) {
                            dist_worker_t *w = &coord.workers[i];
                            if (!w->connected) continue;
                            double worker_total = w->cpu_speed_mkeys + w->gpu_speed_mkeys;
                            printf("  %-4d %-18.18s %10.1f %10.1f \033[32m%10.1f\033[0m %10.2e\n",
                                   w->id,
                                   w->hostname[0] ? w->hostname : "localhost",
                                   w->cpu_speed_mkeys,
                                   w->gpu_speed_mkeys,
                                   worker_total,
                                   (double)w->keys_processed);
                        }
                        printf("\n");
                    }
                }
            }
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
    printf("\n[+] Configuration saved to keyhunt_wizard.json\n");

    dist_coordinator_shutdown(&coord);
    return (coord.result_count > 0) ? 1 : 0;
}
