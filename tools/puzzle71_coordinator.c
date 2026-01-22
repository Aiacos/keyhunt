/*
 * puzzle71_coordinator.c - Coordinator per puzzle 71 distribuito
 *
 * Compila: gcc -o puzzle71_coordinator puzzle71_coordinator.c distributed/distributed.c -lpthread
 * Esegui:  ./puzzle71_coordinator [porta]
 */

#include "../src/distributed/distributed.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

static dist_coordinator_t coord;
static volatile int running = 1;

void sighandler(int sig) {
    (void)sig;
    running = 0;
    printf("\n[!] Ricevuto segnale di terminazione...\n");
}

int main(int argc, char **argv) {
    int port = (argc > 1) ? atoi(argv[1]) : 7777;

    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);

    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║           KEYHUNT PUZZLE 71 - COORDINATOR                    ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");

    // Inizializza coordinator
    if (dist_coordinator_init(&coord, port) != 0) {
        fprintf(stderr, "[ERRORE] Impossibile inizializzare coordinator\n");
        return 1;
    }

    // Puzzle 71 range: 0x400000000000000000 - 0x7FFFFFFFFFFFFFFFFF
    // Dimensione work unit: 0x100000000 (4G chiavi) = ~8-10 secondi per work unit
    printf("[+] Configurazione puzzle 71:\n");
    printf("    Range: 0x400000000000000000 - 0x7FFFFFFFFFFFFFFFFF\n");
    printf("    Dimensione: 2^70 chiavi\n\n");

    int num_units = dist_coordinator_set_range(&coord,
        "400000000000000000",    // Start (hex) = 2^70
        "7FFFFFFFFFFFFFFFFF",    // End (hex) = 2^71 - 1
        0x100000000ULL);         // 4G chiavi per work unit (~4 miliardi)

    if (num_units < 0) {
        fprintf(stderr, "[ERRORE] Impossibile configurare range\n");
        return 1;
    }

    printf("[+] Creati %d work units\n", num_units);
    printf("[+] Ogni work unit: ~4.3 miliardi di chiavi\n\n");

    // Avvia server
    if (dist_coordinator_start(&coord) != 0) {
        fprintf(stderr, "[ERRORE] Impossibile avviare server\n");
        return 1;
    }

    printf("[+] Server in ascolto sulla porta %d\n", port);
    printf("[+] Attendo connessioni dai worker...\n");
    printf("[+] Premi Ctrl+C per terminare\n\n");
    printf("─────────────────────────────────────────────────────────────────\n");

    time_t start_time = time(NULL);
    time_t last_print = 0;

    while (running) {
        int status = dist_coordinator_process(&coord, 500);

        if (status == 1) {
            printf("\n\n[✓] TUTTO IL LAVORO COMPLETATO!\n");
            break;
        }

        // Stampa statistiche ogni secondo
        time_t now = time(NULL);
        if (now != last_print) {
            last_print = now;

            int workers, pending, completed;
            double throughput;
            dist_coordinator_stats(&coord, &workers, &pending, &completed, &throughput);

            double progress = (double)completed / (double)coord.work_unit_count * 100.0;
            time_t elapsed = now - start_time;

            // Calcola ETA
            double keys_done = (double)completed * 0x100000000ULL;
            double keys_total = (double)coord.work_unit_count * 0x100000000ULL;
            double keys_remaining = keys_total - keys_done;
            double eta_seconds = (throughput > 0) ? (keys_remaining / (throughput * 1000000.0)) : 0;

            int eta_days = (int)(eta_seconds / 86400);
            int eta_hours = (int)((eta_seconds - eta_days * 86400) / 3600);

            printf("\r[%02ld:%02ld:%02ld] Workers: %d | Progresso: %d/%d (%.4f%%) | "
                   "Speed: %.2f Mkeys/s | ETA: %dd %dh    ",
                   elapsed / 3600, (elapsed % 3600) / 60, elapsed % 60,
                   workers, completed, coord.work_unit_count, progress,
                   throughput, eta_days, eta_hours);
            fflush(stdout);
        }
    }

    printf("\n\n─────────────────────────────────────────────────────────────────\n");
    printf("[+] Riepilogo:\n");
    printf("    Work units completati: %d/%d\n", coord.work_units_completed, coord.work_unit_count);
    printf("    Chiavi controllate: %.2e\n", (double)coord.keys_processed);
    printf("    Risultati trovati: %d\n", coord.result_count);

    if (coord.result_count > 0) {
        printf("\n[!!!] CHIAVI TROVATE:\n");
        for (int i = 0; i < coord.result_count; i++) {
            printf("    Private Key: %s\n", coord.results[i].private_key);
            printf("    Address: %s\n", coord.results[i].address);
            printf("    Worker: %d\n\n", coord.results[i].worker_id);
        }
    }

    dist_coordinator_shutdown(&coord);
    return 0;
}
