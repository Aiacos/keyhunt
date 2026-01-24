/*
 * config.c - Simple INI configuration file parser
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

/* Default config filename */
#define DEFAULT_CONFIG_FILE "keyhunt.conf"

/* Helper: trim whitespace from both ends */
static char* trim(char *str) {
    if (!str) return NULL;

    /* Trim leading */
    while (isspace((unsigned char)*str)) str++;

    if (*str == 0) return str;

    /* Trim trailing */
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';

    return str;
}

/* Helper: case-insensitive string compare */
static int strcasecmp_local(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2) return c1 - c2;
        s1++;
        s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

void config_init(keyhunt_ini_config_t *cfg) {
    if (!cfg) return;

    memset(cfg, 0, sizeof(keyhunt_ini_config_t));

    /* Defaults */
    strncpy(cfg->mode, "auto", sizeof(cfg->mode) - 1);
    cfg->threads = -1;  /* auto */
    cfg->gpu_enabled = -1;  /* auto */
    cfg->gpu_device_count = 0;
    cfg->gpu_devices[0] = -1;  /* terminator */
    cfg->batch_size = 0;  /* auto */
    cfg->memory_limit_percent = 75;  /* 75% default */

    /* Nothing explicitly set yet */
    cfg->mode_set = false;
    cfg->threads_set = false;
    cfg->gpu_set = false;
    cfg->gpu_devices_set = false;
    cfg->batch_size_set = false;
    cfg->memory_limit_set = false;

    cfg->loaded_from[0] = '\0';
}

int config_parse_gpu_devices(const char *str, int *devices, int max_devices) {
    if (!str || !devices || max_devices <= 0) return 0;

    int count = 0;
    const char *p = str;

    while (*p && count < max_devices) {
        /* Skip whitespace and commas */
        while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
        if (!*p) break;

        /* Parse number */
        char *end;
        long val = strtol(p, &end, 10);
        if (end == p) break;  /* No number found */

        if (val >= 0 && val < 256) {  /* Reasonable GPU ID range */
            devices[count++] = (int)val;
        }

        p = end;
    }

    /* Terminate array */
    if (count < max_devices) {
        devices[count] = -1;
    }

    return count;
}

/* Parse a single config line */
static int parse_line(keyhunt_ini_config_t *cfg, const char *line, int line_num) {
    char buf[CONFIG_MAX_LINE];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *trimmed = trim(buf);

    /* Skip empty lines and comments */
    if (!trimmed[0] || trimmed[0] == '#' || trimmed[0] == ';') {
        return 0;
    }

    /* Find '=' */
    char *eq = strchr(trimmed, '=');
    if (!eq) {
        fprintf(stderr, "[Config] Warning: Invalid line %d (no '='): %s\n", line_num, trimmed);
        return 0;  /* Skip, don't fail */
    }

    /* Split key and value */
    *eq = '\0';
    char *key = trim(trimmed);
    char *value = trim(eq + 1);

    /* Parse known keys */
    if (strcasecmp_local(key, "mode") == 0) {
        strncpy(cfg->mode, value, sizeof(cfg->mode) - 1);
        cfg->mode_set = true;
    }
    else if (strcasecmp_local(key, "threads") == 0) {
        if (strcasecmp_local(value, "auto") == 0) {
            cfg->threads = -1;
        } else {
            cfg->threads = atoi(value);
            if (cfg->threads < 1) cfg->threads = 1;
        }
        cfg->threads_set = true;
    }
    else if (strcasecmp_local(key, "gpu") == 0) {
        if (strcasecmp_local(value, "auto") == 0) {
            cfg->gpu_enabled = -1;
        } else if (strcasecmp_local(value, "on") == 0 ||
                   strcasecmp_local(value, "yes") == 0 ||
                   strcasecmp_local(value, "true") == 0 ||
                   strcasecmp_local(value, "1") == 0) {
            cfg->gpu_enabled = 1;
        } else {
            cfg->gpu_enabled = 0;
        }
        cfg->gpu_set = true;
    }
    else if (strcasecmp_local(key, "gpu_devices") == 0) {
        if (strcasecmp_local(value, "all") == 0) {
            cfg->gpu_device_count = 0;
            cfg->gpu_devices[0] = -1;
        } else {
            cfg->gpu_device_count = config_parse_gpu_devices(value,
                cfg->gpu_devices, CONFIG_MAX_DEVICES);
        }
        cfg->gpu_devices_set = true;
    }
    else if (strcasecmp_local(key, "batch_size") == 0) {
        if (strcasecmp_local(value, "auto") == 0) {
            cfg->batch_size = 0;
        } else {
            cfg->batch_size = atoi(value);
            /* Validate: must be power of 2 and reasonable */
            if (cfg->batch_size < 64) cfg->batch_size = 64;
            if (cfg->batch_size > 8192) cfg->batch_size = 8192;
        }
        cfg->batch_size_set = true;
    }
    else if (strcasecmp_local(key, "memory_limit") == 0) {
        /* Remove '%' if present */
        char *pct = strchr(value, '%');
        if (pct) *pct = '\0';
        cfg->memory_limit_percent = atoi(value);
        if (cfg->memory_limit_percent < 10) cfg->memory_limit_percent = 10;
        if (cfg->memory_limit_percent > 95) cfg->memory_limit_percent = 95;
        cfg->memory_limit_set = true;
    }
    else {
        /* Unknown key - warn but continue */
        fprintf(stderr, "[Config] Warning: Unknown key '%s' on line %d\n", key, line_num);
    }

    return 0;
}

int config_load(keyhunt_ini_config_t *cfg, const char *filepath) {
    if (!cfg || !filepath) return -1;

    FILE *f = fopen(filepath, "r");
    if (!f) {
        return -1;  /* File not found */
    }

    char line[CONFIG_MAX_LINE];
    int line_num = 0;

    while (fgets(line, sizeof(line), f)) {
        line_num++;

        /* Remove newline */
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        if (len > 1 && line[len-2] == '\r') line[len-2] = '\0';

        if (parse_line(cfg, line, line_num) < 0) {
            fclose(f);
            return -1;
        }
    }

    fclose(f);

    /* Remember where we loaded from */
    strncpy(cfg->loaded_from, filepath, sizeof(cfg->loaded_from) - 1);

    return 0;
}

int config_save(const keyhunt_ini_config_t *cfg, const char *filepath) {
    if (!cfg || !filepath) return -1;

    FILE *f = fopen(filepath, "w");
    if (!f) {
        fprintf(stderr, "[Config] Error: Cannot write to '%s': %s\n",
                filepath, strerror(errno));
        return -1;
    }

    fprintf(f, "# Keyhunt configuration file\n");
    fprintf(f, "# Auto-generated - edit only if needed\n\n");

    /* Mode */
    fprintf(f, "mode = %s\n", cfg->mode);

    /* Threads */
    if (cfg->threads < 0) {
        fprintf(f, "threads = auto\n");
    } else {
        fprintf(f, "threads = %d\n", cfg->threads);
    }

    /* GPU */
    if (cfg->gpu_enabled < 0) {
        fprintf(f, "gpu = auto\n");
    } else if (cfg->gpu_enabled > 0) {
        fprintf(f, "gpu = on\n");
    } else {
        fprintf(f, "gpu = off\n");
    }

    /* GPU devices */
    if (cfg->gpu_device_count == 0) {
        fprintf(f, "gpu_devices = all\n");
    } else {
        fprintf(f, "gpu_devices = ");
        for (int i = 0; i < cfg->gpu_device_count; i++) {
            if (i > 0) fprintf(f, ",");
            fprintf(f, "%d", cfg->gpu_devices[i]);
        }
        fprintf(f, "\n");
    }

    /* Batch size */
    if (cfg->batch_size == 0) {
        fprintf(f, "batch_size = auto\n");
    } else {
        fprintf(f, "batch_size = %d\n", cfg->batch_size);
    }

    /* Memory limit */
    fprintf(f, "memory_limit = %d%%\n", cfg->memory_limit_percent);

    fprintf(f, "\n# Optional tuning (uncomment to override auto-detection)\n");
    fprintf(f, "# gpu_blocks_per_sm = 4\n");
    fprintf(f, "# gpu_keys_per_thread = 256\n");

    fclose(f);

    fprintf(stderr, "[Config] Saved configuration to '%s'\n", filepath);
    return 0;
}

int config_load_default(keyhunt_ini_config_t *cfg) {
    return config_load(cfg, DEFAULT_CONFIG_FILE);
}

void config_print(const keyhunt_ini_config_t *cfg) {
    if (!cfg) return;

    fprintf(stderr, "[Config] Current configuration:\n");

    if (cfg->loaded_from[0]) {
        fprintf(stderr, "  Loaded from: %s\n", cfg->loaded_from);
    }

    fprintf(stderr, "  mode = %s%s\n", cfg->mode, cfg->mode_set ? "" : " (default)");

    if (cfg->threads < 0) {
        fprintf(stderr, "  threads = auto%s\n", cfg->threads_set ? "" : " (default)");
    } else {
        fprintf(stderr, "  threads = %d%s\n", cfg->threads, cfg->threads_set ? "" : " (default)");
    }

    const char *gpu_str = cfg->gpu_enabled < 0 ? "auto" :
                          cfg->gpu_enabled > 0 ? "on" : "off";
    fprintf(stderr, "  gpu = %s%s\n", gpu_str, cfg->gpu_set ? "" : " (default)");

    if (cfg->gpu_device_count == 0) {
        fprintf(stderr, "  gpu_devices = all%s\n", cfg->gpu_devices_set ? "" : " (default)");
    } else {
        fprintf(stderr, "  gpu_devices = ");
        for (int i = 0; i < cfg->gpu_device_count; i++) {
            if (i > 0) fprintf(stderr, ",");
            fprintf(stderr, "%d", cfg->gpu_devices[i]);
        }
        fprintf(stderr, "%s\n", cfg->gpu_devices_set ? "" : " (default)");
    }

    if (cfg->batch_size == 0) {
        fprintf(stderr, "  batch_size = auto%s\n", cfg->batch_size_set ? "" : " (default)");
    } else {
        fprintf(stderr, "  batch_size = %d%s\n", cfg->batch_size, cfg->batch_size_set ? "" : " (default)");
    }

    fprintf(stderr, "  memory_limit = %d%%%s\n",
            cfg->memory_limit_percent, cfg->memory_limit_set ? "" : " (default)");
}

const char* config_mode_str(const keyhunt_ini_config_t *cfg) {
    if (!cfg) return "unknown";
    return cfg->mode;
}
