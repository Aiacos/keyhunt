/*
 * wizard_community.c - BTCPuzzle.info scraper and community integration
 */

#include "wizard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define BTCPUZZLE_URL "https://btcpuzzle.info"
#define PRIVATEKEYS_URL "https://privatekeys.pw/puzzles/bitcoin-puzzle-tx"
#define FETCH_TIMEOUT 30
#define MAX_RESPONSE_SIZE (10 * 1024 * 1024)  /* 10 MB */

/* ============================================================================
 * HTTP Fetch (using curl command)
 * ============================================================================ */

static int fetch_url(const char *url, char **response, size_t *response_len) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "curl -sL --max-time %d --compressed '%s' 2>/dev/null",
             FETCH_TIMEOUT, url);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        fprintf(stderr, "[-] Failed to execute curl\n");
        return -1;
    }

    /* Read response in chunks */
    size_t capacity = 65536;
    size_t len = 0;
    *response = malloc(capacity);
    if (!*response) {
        pclose(fp);
        return -1;
    }

    char buf[8192];
    while (!feof(fp) && len < MAX_RESPONSE_SIZE) {
        size_t n = fread(buf, 1, sizeof(buf), fp);
        if (n > 0) {
            if (len + n >= capacity) {
                capacity *= 2;
                char *newbuf = realloc(*response, capacity);
                if (!newbuf) {
                    free(*response);
                    pclose(fp);
                    return -1;
                }
                *response = newbuf;
            }
            memcpy(*response + len, buf, n);
            len += n;
        }
    }

    (*response)[len] = '\0';
    *response_len = len;

    int status = pclose(fp);
    return (status == 0 && len > 0) ? 0 : -1;
}

/* ============================================================================
 * Puzzle Download from Web
 * ============================================================================ */

int wizard_download_puzzles(puzzle_def_t **puzzles, int *count) {
    printf("[+] Downloading puzzle list from %s...\n", BTCPUZZLE_URL);

    char url[256];
    snprintf(url, sizeof(url), "%s/puzzle", BTCPUZZLE_URL);

    char *html = NULL;
    size_t html_len = 0;

    if (fetch_url(url, &html, &html_len) != 0 || html_len < 1000) {
        printf("[-] Failed to fetch puzzle list (network error)\n");
        if (html) free(html);

        /* Fallback to builtin */
        *puzzles = NULL;
        *count = 0;
        return -1;
    }

    /* Parse puzzle data from HTML/JSON */
    /* BTCPuzzle.info embeds data in __NEXT_DATA__ script tag */

    char *json_start = strstr(html, "__NEXT_DATA__");
    if (!json_start) {
        printf("[-] Cannot parse puzzle data (format changed?)\n");
        free(html);
        return -1;
    }

    /* Count puzzles by looking for "puzzle": patterns */
    int n = 0;
    char *scan = html;
    while ((scan = strstr(scan, "\"puzzleNumber\":")) != NULL) {
        n++;
        scan++;
    }

    if (n == 0) {
        printf("[-] No puzzles found in response\n");
        free(html);
        return -1;
    }

    printf("[+] Found %d puzzles\n", n);

    *puzzles = calloc(n, sizeof(puzzle_def_t));
    if (!*puzzles) {
        free(html);
        return -1;
    }

    /* Extract puzzle data */
    int idx = 0;
    scan = html;

    while ((scan = strstr(scan, "\"puzzleNumber\":")) != NULL && idx < n) {
        puzzle_def_t *p = &(*puzzles)[idx];

        /* Extract puzzle number */
        scan += 15;  /* Skip "puzzleNumber": */
        p->number = atoi(scan);

        if (p->number <= 0) {
            scan++;
            continue;
        }

        /* Look for associated data nearby */
        char *block_end = scan + 2000;  /* Search within 2KB */
        if (block_end > html + html_len) block_end = html + html_len;

        /* Target address */
        char *addr = strstr(scan, "\"address\":\"");
        if (addr && addr < block_end) {
            addr += 11;
            char *end = strchr(addr, '"');
            if (end && end - addr < 64) {
                size_t len = end - addr;
                memcpy(p->target_address, addr, len);
                p->target_address[len] = '\0';
            }
        }

        /* Bits */
        char *bits = strstr(scan, "\"bits\":");
        if (bits && bits < block_end) {
            bits += 7;
            p->bits = atoi(bits);
        } else {
            p->bits = p->number;  /* Fallback: puzzle number = bits */
        }

        /* Calculate range from bits */
        if (p->bits > 0 && p->bits <= 256) {
            /* range_start = 2^(bits-1), range_end = 2^bits - 1 */
            snprintf(p->range_start, sizeof(p->range_start), "%llx",
                     (1ULL << (p->bits > 64 ? 63 : p->bits - 1)));
            snprintf(p->range_end, sizeof(p->range_end), "%llx",
                     (p->bits >= 64) ? 0xFFFFFFFFFFFFFFFFULL : ((1ULL << p->bits) - 1));
        }

        /* Reward */
        char *reward = strstr(scan, "\"reward\":");
        if (reward && reward < block_end) {
            reward += 9;
            p->reward_btc = atof(reward);
        } else {
            p->reward_btc = p->number / 10.0;  /* Fallback estimate */
        }

        /* Public key */
        char *pubkey = strstr(scan, "\"publicKey\":\"");
        if (pubkey && pubkey < block_end) {
            pubkey += 13;
            char *end = strchr(pubkey, '"');
            if (end && end - pubkey > 10) {
                size_t len = end - pubkey;
                if (len < sizeof(p->public_key)) {
                    memcpy(p->public_key, pubkey, len);
                    p->public_key[len] = '\0';
                    p->has_public_key = true;
                }
            }
        }

        /* Solved status */
        char *solved = strstr(scan, "\"solved\":");
        if (solved && solved < block_end) {
            solved += 9;
            p->solved = (strncmp(solved, "true", 4) == 0);
        }

        idx++;
        scan++;
    }

    *count = idx;
    free(html);

    printf("[+] Parsed %d puzzles successfully\n", idx);
    return 0;
}

/* ============================================================================
 * Community Scanned Ranges
 * ============================================================================ */

int wizard_community_fetch(int puzzle_number, community_range_t **ranges, int *count) {
    char url[256];
    snprintf(url, sizeof(url), "%s/puzzle/%d", BTCPUZZLE_URL, puzzle_number);

    printf("[+] Fetching scanned ranges for puzzle #%d...\n", puzzle_number);

    char *html = NULL;
    size_t html_len = 0;

    if (fetch_url(url, &html, &html_len) != 0) {
        printf("[-] Failed to fetch community data\n");
        *ranges = NULL;
        *count = 0;
        return -1;
    }

    /* Find scanned ranges data */
    char *ranges_start = strstr(html, "\"scannedRanges\"");
    if (!ranges_start) {
        /* Try alternative marker */
        ranges_start = strstr(html, "\"ranges\"");
    }

    if (!ranges_start) {
        printf("[i] No scanned ranges data found\n");
        free(html);
        *ranges = NULL;
        *count = 0;
        return 0;
    }

    /* Count ranges */
    int n = 0;
    char *scan = ranges_start;
    while ((scan = strstr(scan, "\"rangeId\":")) != NULL) {
        n++;
        scan++;
    }

    if (n == 0) {
        printf("[i] No ranges to exclude\n");
        free(html);
        *ranges = NULL;
        *count = 0;
        return 0;
    }

    *ranges = calloc(n, sizeof(community_range_t));
    if (!*ranges) {
        free(html);
        return -1;
    }

    /* Parse ranges */
    int idx = 0;
    scan = ranges_start;

    while ((scan = strstr(scan, "\"rangeId\":\"")) != NULL && idx < n) {
        scan += 11;  /* Skip to value */

        char *end = strchr(scan, '"');
        if (!end) break;

        size_t len = end - scan;
        if (len >= sizeof((*ranges)[idx].range_id)) {
            len = sizeof((*ranges)[idx].range_id) - 1;
        }

        memcpy((*ranges)[idx].range_id, scan, len);
        (*ranges)[idx].range_id[len] = '\0';

        /* Convert range_id to hex ranges */
        /* BTCPuzzle format: "45X943C" where X is variable */
        /* We expand this to full hex range */

        const puzzle_def_t *puzzle = wizard_get_puzzle(puzzle_number);
        if (puzzle) {
            /* Simple mapping: rangeId indicates position in puzzle space */
            snprintf((*ranges)[idx].hex_start, sizeof((*ranges)[idx].hex_start),
                     "%s%s", puzzle->range_start, (*ranges)[idx].range_id);
        }

        (*ranges)[idx].scanned_time = time(NULL);
        idx++;
        scan = end;
    }

    *count = idx;
    free(html);

    printf("[+] Found %d community-scanned ranges\n", idx);
    return 0;
}

void wizard_community_free(community_range_t *ranges, int count) {
    (void)count;
    free(ranges);
}

/* ============================================================================
 * Exclusion File Management
 * ============================================================================ */

int wizard_community_merge_exclusions(const char *exclusion_file,
                                       const community_range_t *ranges, int count) {
    if (!ranges || count == 0) return 0;

    FILE *f = fopen(exclusion_file, "a");  /* Append */
    if (!f) {
        fprintf(stderr, "[-] Cannot open exclusion file: %s\n", exclusion_file);
        return -1;
    }

    int added = 0;
    for (int i = 0; i < count; i++) {
        if (ranges[i].range_id[0]) {
            fprintf(f, "%s\n", ranges[i].range_id);
            added++;
        }
    }

    fclose(f);
    printf("[+] Added %d ranges to exclusion file\n", added);
    return added;
}

bool wizard_is_range_excluded(const char *exclusion_file, const char *range_start) {
    FILE *f = fopen(exclusion_file, "r");
    if (!f) return false;

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        /* Trim newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = '\0';
        }

        /* Check if range_start contains or matches this exclusion */
        if (strstr(range_start, line) != NULL) {
            fclose(f);
            return true;
        }
    }

    fclose(f);
    return false;
}

/* ============================================================================
 * Save Puzzles to Local File
 * ============================================================================ */

int wizard_save_puzzles_to_txt(const puzzle_def_t *puzzles, int count, const char *filepath) {
    FILE *f = fopen(filepath, "w");
    if (!f) return -1;

    fprintf(f, "# Bitcoin Puzzles Database\n");
    fprintf(f, "# Downloaded from BTCPuzzle.info on %s", ctime(&(time_t){time(NULL)}));
    fprintf(f, "# Format: number|address|range_start|range_end|bits|btc|has_pubkey|pubkey|solved\n");
    fprintf(f, "#\n");

    for (int i = 0; i < count; i++) {
        const puzzle_def_t *p = &puzzles[i];
        fprintf(f, "%d|%s|%s|%s|%d|%.2f|%d|%s|%d\n",
                p->number,
                p->target_address,
                p->range_start,
                p->range_end,
                p->bits,
                p->reward_btc,
                p->has_public_key ? 1 : 0,
                p->public_key,
                p->solved ? 1 : 0);
    }

    fclose(f);
    return 0;
}

int wizard_load_puzzles_from_txt(const char *filepath, puzzle_def_t **puzzles, int *count) {
    FILE *f = fopen(filepath, "r");
    if (!f) return -1;

    /* Count lines (excluding comments) */
    int n = 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] != '#' && line[0] != '\n') n++;
    }

    if (n == 0) {
        fclose(f);
        return -1;
    }

    rewind(f);

    *puzzles = calloc(n, sizeof(puzzle_def_t));
    if (!*puzzles) {
        fclose(f);
        return -1;
    }

    int idx = 0;
    while (fgets(line, sizeof(line), f) && idx < n) {
        if (line[0] == '#' || line[0] == '\n') continue;

        puzzle_def_t *p = &(*puzzles)[idx];

        /* Parse pipe-separated fields */
        char *tok = strtok(line, "|");
        if (tok) p->number = atoi(tok);

        tok = strtok(NULL, "|");
        if (tok) strncpy(p->target_address, tok, sizeof(p->target_address) - 1);

        tok = strtok(NULL, "|");
        if (tok) strncpy(p->range_start, tok, sizeof(p->range_start) - 1);

        tok = strtok(NULL, "|");
        if (tok) strncpy(p->range_end, tok, sizeof(p->range_end) - 1);

        tok = strtok(NULL, "|");
        if (tok) p->bits = atoi(tok);

        tok = strtok(NULL, "|");
        if (tok) p->reward_btc = atof(tok);

        tok = strtok(NULL, "|");
        if (tok) p->has_public_key = (atoi(tok) != 0);

        tok = strtok(NULL, "|");
        if (tok) strncpy(p->public_key, tok, sizeof(p->public_key) - 1);

        tok = strtok(NULL, "|\n");
        if (tok) p->solved = (atoi(tok) != 0);

        if (p->number > 0) idx++;
    }

    *count = idx;
    fclose(f);
    return 0;
}
