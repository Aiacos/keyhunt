/*
 * wizard_ui.c - Interactive terminal UI for wizard
 */

#include "wizard.h"
#include "../platform/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>


/* ANSI color codes */
#define RESET       "\033[0m"
#define BOLD        "\033[1m"
#define DIM         "\033[2m"
#define RED         "\033[0;31m"
#define GREEN       "\033[0;32m"
#define YELLOW      "\033[1;33m"
#define BLUE        "\033[0;34m"
#define MAGENTA     "\033[0;35m"
#define CYAN        "\033[0;36m"
#define WHITE       "\033[0;37m"
#define BG_GREEN    "\033[42m"
#define BG_RED      "\033[41m"

/* Box drawing characters */
#define BOX_TL      "╔"
#define BOX_TR      "╗"
#define BOX_BL      "╚"
#define BOX_BR      "╝"
#define BOX_H       "═"
#define BOX_V       "║"
#define BOX_SEP     "─"

void wizard_ui_clear(void) {
    printf("\033[2J\033[H");
    fflush(stdout);
}

void wizard_print_header(const char *title) {
    int width = platform_terminal_width();
    if (width > 70) width = 70;

    printf("\n");
    printf(CYAN BOX_TL);
    for (int i = 0; i < width - 2; i++) printf(BOX_H);
    printf(BOX_TR "\n" RESET);

    printf(CYAN BOX_V BOLD " %-*s" RESET CYAN BOX_V "\n" RESET, width - 3, title);

    printf(CYAN BOX_BL);
    for (int i = 0; i < width - 2; i++) printf(BOX_H);
    printf(BOX_BR "\n" RESET);
    printf("\n");
}

void wizard_print_separator(void) {
    int width = platform_terminal_width();
    if (width > 70) width = 70;

    printf(CYAN);
    for (int i = 0; i < width; i++) printf(BOX_SEP);
    printf(RESET "\n");
}

void wizard_print_step(int current, int total, const char *title) {
    printf("\n" YELLOW "[Step %d/%d]" RESET " " BOLD "%s" RESET "\n", current, total, title);
    wizard_print_separator();
}

int wizard_ask_choice(const char *prompt, const char **options, int count, int default_choice) {
    printf(YELLOW "%s" RESET "\n\n", prompt);

    for (int i = 0; i < count; i++) {
        if (i == default_choice) {
            printf(GREEN "  > [%d] %s" RESET " (default)\n", i + 1, options[i]);
        } else {
            printf("    [%d] %s\n", i + 1, options[i]);
        }
    }

    printf("\n" CYAN "Choice" RESET " [%d]: ", default_choice + 1);
    fflush(stdout);

    char buf[32];
    if (!fgets(buf, sizeof(buf), stdin)) {
        return default_choice;
    }

    /* Trim whitespace */
    char *p = buf;
    while (*p && isspace((unsigned char)*p)) p++;

    /* Empty = default */
    if (*p == '\0' || *p == '\n') {
        return default_choice;
    }

    int choice = atoi(p) - 1;
    if (choice < 0 || choice >= count) {
        printf(RED "[!] Invalid choice, using default" RESET "\n");
        return default_choice;
    }

    return choice;
}

int wizard_ask_string(const char *prompt, char *buffer, size_t bufsize, const char *default_val) {
    if (default_val && *default_val) {
        printf(CYAN "%s" RESET " [" DIM "%s" RESET "]: ", prompt, default_val);
    } else {
        printf(CYAN "%s" RESET ": ", prompt);
    }
    fflush(stdout);

    if (!fgets(buffer, bufsize, stdin)) {
        if (default_val) {
            strncpy(buffer, default_val, bufsize - 1);
            buffer[bufsize - 1] = '\0';
        }
        return -1;
    }

    /* Remove trailing newline */
    size_t len = strlen(buffer);
    while (len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
        buffer[--len] = '\0';
    }

    /* Empty = default */
    if (len == 0 && default_val) {
        strncpy(buffer, default_val, bufsize - 1);
        buffer[bufsize - 1] = '\0';
    }

    return 0;
}

int wizard_ask_int(const char *prompt, int min_val, int max_val, int default_val) {
    char buf[64];
    char def_str[32];
    snprintf(def_str, sizeof(def_str), "%d", default_val);

    printf(CYAN "%s" RESET " (%d-%d) [" DIM "%d" RESET "]: ", prompt, min_val, max_val, default_val);
    fflush(stdout);

    if (!fgets(buf, sizeof(buf), stdin) || buf[0] == '\n') {
        return default_val;
    }

    int val = atoi(buf);
    if (val < min_val) {
        printf(YELLOW "[!] Value too low, using %d" RESET "\n", min_val);
        return min_val;
    }
    if (val > max_val) {
        printf(YELLOW "[!] Value too high, using %d" RESET "\n", max_val);
        return max_val;
    }

    return val;
}

bool wizard_ask_yesno(const char *prompt, bool default_val) {
    printf(CYAN "%s" RESET " [%s]: ", prompt, default_val ? "Y/n" : "y/N");
    fflush(stdout);

    char buf[16];
    if (!fgets(buf, sizeof(buf), stdin) || buf[0] == '\n') {
        return default_val;
    }

    char c = tolower((unsigned char)buf[0]);
    if (c == 'y') return true;
    if (c == 'n') return false;

    return default_val;
}

void wizard_print_config_summary(const wizard_config_t *cfg) {
    printf("\n");
    wizard_print_separator();
    printf(BOLD "Configuration Summary:" RESET "\n\n");

    printf("  ┌─────────────────────────────────────────────────────────┐\n");
    printf("  │ " CYAN "Puzzle:" RESET " #%-3d (%d bits)                              │\n",
           cfg->puzzle_number, cfg->bits);
    printf("  │ " CYAN "Target:" RESET " %.45s...│\n", cfg->target_address);
    printf("  │ " CYAN "Mode:" RESET "   %-10s %-10s                        │\n",
           cfg->is_server ? "SERVER" : "CLIENT",
           cfg->server_also_worker ? "(+worker)" : "");
    printf("  │ " CYAN "Port:" RESET "   %-5d                                       │\n", cfg->server_port);
    printf("  │ " CYAN "Unit:" RESET "   %llu keys/work                         │\n",
           (unsigned long long)cfg->work_unit_size);
    if (cfg->threads <= 0) {
        printf("  │ " CYAN "CPU:" RESET "    auto (all cores)                              │\n");
    } else {
        printf("  │ " CYAN "CPU:" RESET "    %d threads                                   │\n", cfg->threads);
    }
    if (cfg->gpu_percent <= 0) {
        printf("  │ " CYAN "GPU:" RESET "    auto (if available)                           │\n");
    } else {
        printf("  │ " CYAN "GPU:" RESET "    %d%%                                          │\n", cfg->gpu_percent);
    }
    printf("  │ " CYAN "Type:" RESET "   %-10s                                   │\n", cfg->key_type);
    printf("  │ " CYAN "Community:" RESET " %s                                     │\n",
           cfg->community_enabled ? GREEN "Enabled" RESET : YELLOW "Disabled" RESET);
    if (cfg->community_excluded > 0) {
        printf("  │ " YELLOW "Excluded:" RESET " %llu ranges                           │\n",
               (unsigned long long)cfg->community_excluded);
    }
    if (cfg->privatekeys_percent > 0.0) {
        printf("  │ " CYAN "Progress:" RESET " %.4f%% (privatekeys.pw)                 │\n",
               cfg->privatekeys_percent);
    }

    /* Webhook notifications */
    bool has_discord = (cfg->webhook_discord_url[0] != '\0');
    bool has_telegram = (cfg->webhook_telegram_url[0] != '\0');
    if (has_discord || has_telegram) {
        printf("  │ " CYAN "Webhooks:" RESET " ");
        if (has_discord) printf(GREEN "Discord" RESET);
        if (has_discord && has_telegram) printf(", ");
        if (has_telegram) printf(GREEN "Telegram" RESET);
        int pad = 42 - (has_discord ? 7 : 0) - (has_telegram ? 8 : 0) - (has_discord && has_telegram ? 2 : 0);
        for (int i = 0; i < pad; i++) printf(" ");
        printf("│\n");
    }

    /* Progress reporting */
    if (cfg->report_progress_enabled && cfg->report_progress_url[0] != '\0') {
        printf("  │ " CYAN "Reporting:" RESET " " GREEN "Enabled" RESET " (to community API)              │\n");
    }

    printf("  └─────────────────────────────────────────────────────────┘\n");
}

void wizard_print_progress(int current, int total, double speed, const char *status) {
    int width = platform_terminal_width() - 40;
    if (width < 20) width = 20;
    if (width > 50) width = 50;

    double pct = (total > 0) ? ((double)current / total * 100.0) : 0;
    int filled = (int)(pct / 100.0 * width);

    printf("\r[");
    for (int i = 0; i < width; i++) {
        if (i < filled) printf(GREEN "█" RESET);
        else printf(DIM "░" RESET);
    }
    printf("] " CYAN "%.2f%%" RESET " | " GREEN "%.2f" RESET " Mkeys/s | %s   ",
           pct, speed, status);
    fflush(stdout);
}

/* Print puzzle selection table */
void wizard_print_puzzle_table(const puzzle_def_t *puzzles, int count, int highlight) {
    printf("\n");
    printf("  " BOLD "%-4s %-8s %-6s %-10s %s" RESET "\n",
           "#", "Bits", "BTC", "Status", "Note");
    printf("  " DIM "──── ──────── ────── ────────── ─────────────────" RESET "\n");

    for (int i = 0; i < count && i < 15; i++) {
        const puzzle_def_t *p = &puzzles[i];

        const char *status = p->solved ? RED "Solved" RESET : GREEN "Active" RESET;
        const char *note = p->has_public_key ? YELLOW "[PubKey!]" RESET : "";

        if (i == highlight) {
            printf(BG_GREEN "  %-4d %-8d %-6.2f %-10s %s" RESET "\n",
                   p->number, p->bits, p->reward_btc,
                   p->solved ? "Solved" : "Active", p->has_public_key ? "[PubKey]" : "");
        } else {
            printf("  %-4d %-8d %-6.2f %s %s\n",
                   p->number, p->bits, p->reward_btc, status, note);
        }
    }

    if (count > 15) {
        printf("  " DIM "... and %d more puzzles" RESET "\n", count - 15);
    }
    printf("\n");
}

/* Print privacy warning for progress reporting */
void wizard_print_privacy_warning(void) {
    int width = platform_terminal_width();
    if (width > 70) width = 70;

    printf("\n");
    printf(YELLOW BOX_TL);
    for (int i = 0; i < width - 2; i++) printf(BOX_H);
    printf(BOX_TR "\n" RESET);

    printf(YELLOW BOX_V " " BOLD "PRIVACY NOTICE: Progress Reporting" RESET);
    int padding = width - 38;  /* 38 = length of title + spaces + box chars */
    for (int i = 0; i < padding; i++) printf(" ");
    printf(YELLOW BOX_V "\n" RESET);

    printf(YELLOW BOX_V RESET " %-*s" YELLOW BOX_V "\n" RESET, width - 3, "");

    printf(YELLOW BOX_V RESET " Progress reporting allows you to share your search progress ");
    printf(YELLOW BOX_V "\n" RESET);
    printf(YELLOW BOX_V RESET " with the community, helping coordinate distributed efforts.   ");
    printf(YELLOW BOX_V "\n" RESET);

    printf(YELLOW BOX_V RESET " %-*s" YELLOW BOX_V "\n" RESET, width - 3, "");

    printf(YELLOW BOX_V " " CYAN "Data Shared (if enabled):" RESET);
    padding = width - 28;
    for (int i = 0; i < padding; i++) printf(" ");
    printf(YELLOW BOX_V "\n" RESET);

    printf(YELLOW BOX_V RESET "   • Puzzle number (e.g., #66, #125)                          ");
    printf(YELLOW BOX_V "\n" RESET);
    printf(YELLOW BOX_V RESET "   • Search range (e.g., 0x20000000000000000 - 0x3ffff...)     ");
    printf(YELLOW BOX_V "\n" RESET);
    printf(YELLOW BOX_V RESET "   • Keys checked count (progress indicator)                   ");
    printf(YELLOW BOX_V "\n" RESET);
    printf(YELLOW BOX_V RESET "   • Worker ID (your hostname)                                 ");
    printf(YELLOW BOX_V "\n" RESET);
    printf(YELLOW BOX_V RESET "   • Timestamp of last update                                  ");
    printf(YELLOW BOX_V "\n" RESET);

    printf(YELLOW BOX_V RESET " %-*s" YELLOW BOX_V "\n" RESET, width - 3, "");

    printf(YELLOW BOX_V " " RED "NOT Shared:" RESET);
    padding = width - 16;
    for (int i = 0; i < padding; i++) printf(" ");
    printf(YELLOW BOX_V "\n" RESET);

    printf(YELLOW BOX_V RESET "   • Found private keys (NEVER transmitted!)                   ");
    printf(YELLOW BOX_V "\n" RESET);
    printf(YELLOW BOX_V RESET "   • IP address or personal information                        ");
    printf(YELLOW BOX_V "\n" RESET);
    printf(YELLOW BOX_V RESET "   • System hardware details                                   ");
    printf(YELLOW BOX_V "\n" RESET);

    printf(YELLOW BOX_V RESET " %-*s" YELLOW BOX_V "\n" RESET, width - 3, "");

    printf(YELLOW BOX_V " " GREEN "Benefits:" RESET);
    padding = width - 13;
    for (int i = 0; i < padding; i++) printf(" ");
    printf(YELLOW BOX_V "\n" RESET);

    printf(YELLOW BOX_V RESET "   ✓ Community coordination (avoid duplicate work)             ");
    printf(YELLOW BOX_V "\n" RESET);
    printf(YELLOW BOX_V RESET "   ✓ Progress visibility for pool operators                    ");
    printf(YELLOW BOX_V "\n" RESET);
    printf(YELLOW BOX_V RESET "   ✓ Better resource allocation across workers                 ");
    printf(YELLOW BOX_V "\n" RESET);

    printf(YELLOW BOX_V RESET " %-*s" YELLOW BOX_V "\n" RESET, width - 3, "");

    printf(YELLOW BOX_V " " DIM "Progress reporting is " BOLD "OPTIONAL" RESET DIM " and disabled by default." RESET);
    padding = width - 59;
    for (int i = 0; i < padding; i++) printf(" ");
    printf(YELLOW BOX_V "\n" RESET);

    printf(YELLOW BOX_BL);
    for (int i = 0; i < width - 2; i++) printf(BOX_H);
    printf(BOX_BR "\n" RESET);
}
