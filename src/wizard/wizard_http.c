/*
 * wizard_http.c - HTTP client module implementation
 */

#include "wizard_http.h"
#include "../error/enhanced_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ============================================================================
 * URL Validation (shell injection prevention)
 * ============================================================================ */

/**
 * Validate that a URL is safe to pass to shell commands.
 * Rejects URLs containing shell metacharacters that could enable injection.
 * Returns 0 if safe, -1 if unsafe.
 */
static int validate_shell_safe(const char *str) {
    if (!str) return -1;
    for (const char *p = str; *p; p++) {
        unsigned char c = (unsigned char)*p;
        /* Reject shell metacharacters and control characters */
        if (c < 32 || c == '\'' || c == '"' || c == '`' || c == '$' ||
            c == '(' || c == ')' || c == '|' || c == ';' ||
            c == '&' || c == '!' || c == '{' || c == '}' ||
            c == '<' || c == '>' || c == '\n' || c == '\r') {
            return -1;
        }
    }
    return 0;
}

/* ============================================================================
 * HTTP GET Request
 * ============================================================================ */

int wizard_http_get(const char *url, char **response, size_t *response_len) {
    if (!url || !response || !response_len) {
        fprintf(stderr, "[-] wizard_http_get: Invalid parameters\n");
        return -1;
    }

    /* Validate URL is safe for shell use */
    if (validate_shell_safe(url) != 0) {
        fprintf(stderr, "[-] wizard_http_get: URL contains unsafe characters\n");
        return -1;
    }

    /* Build curl command for GET request */
    char cmd[2048];
    int cmd_len = snprintf(cmd, sizeof(cmd),
             "curl -sL --max-time %d --compressed "
             "-A '%s' "
             "'%s' 2>/dev/null",
             WIZARD_HTTP_TIMEOUT, WIZARD_HTTP_USER_AGENT, url);
    if (cmd_len < 0 || cmd_len >= (int)sizeof(cmd)) {
        fprintf(stderr, "[-] wizard_http_get: URL too long for command buffer\n");
        return -1;
    }

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        fprintf(stderr, "[-] wizard_http_get: Failed to execute curl\n");
        return -1;
    }

    /* Read response in chunks */
    size_t capacity = 65536;
    size_t len = 0;
    *response = malloc(capacity);
    if (!*response) {
        pclose(fp);
        error_report_t report;
        error_oom("HTTP response buffer allocation", &report);
        error_print(&report);
        return -1;
    }

    char buf[8192];
    while (!feof(fp) && !ferror(fp) && len < WIZARD_HTTP_MAX_RESPONSE_SIZE) {
        size_t n = fread(buf, 1, sizeof(buf), fp);
        if (n > 0) {
            /* Expand buffer if needed, +1 for null terminator */
            if (len + n + 1 >= capacity) {
                while (capacity <= len + n + 1) capacity *= 2;
                char *newbuf = realloc(*response, capacity);
                if (!newbuf) {
                    free(*response);
                    *response = NULL;
                    pclose(fp);
                    error_report_t report;
                    error_context_t ctx = ERROR_CONTEXT_VALUES(
                        ERROR_CAT_MEMORY, ERROR_SEV_ERROR,
                        "HTTP response buffer expansion",
                        "Failed to reallocate buffer for growing HTTP response",
                        capacity, capacity / 2
                    );
                    error_report(&ctx, &report);
                    error_print(&report);
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
    if (status != 0 || len == 0) {
        free(*response);
        *response = NULL;
        *response_len = 0;
        return -1;
    }

    return 0;
}

/* ============================================================================
 * HTTP POST Request
 * ============================================================================ */

int wizard_http_post(const char *url, const char *body, size_t body_len,
                     const char *content_type, char **response, size_t *response_len) {
    if (!url || !response || !response_len) {
        fprintf(stderr, "[-] wizard_http_post: Invalid parameters\n");
        return -1;
    }

    /* Validate URL and content_type are safe for shell use */
    if (validate_shell_safe(url) != 0) {
        fprintf(stderr, "[-] wizard_http_post: URL contains unsafe characters\n");
        return -1;
    }
    if (content_type && validate_shell_safe(content_type) != 0) {
        fprintf(stderr, "[-] wizard_http_post: content_type contains unsafe characters\n");
        return -1;
    }

    /* Create temporary file for POST data if body is provided */
    char tmp_file[256] = {0};
    if (body && body_len > 0) {
        snprintf(tmp_file, sizeof(tmp_file), "/tmp/wizard_http_XXXXXX");
        int fd = mkstemp(tmp_file);
        if (fd < 0) {
            fprintf(stderr, "[-] wizard_http_post: Failed to create temp file\n");
            return -1;
        }

        ssize_t written = write(fd, body, body_len);
        close(fd);

        if (written != (ssize_t)body_len) {
            unlink(tmp_file);
            fprintf(stderr, "[-] wizard_http_post: Failed to write POST data\n");
            return -1;
        }
    }

    /* Build curl command for POST request */
    char cmd[2048];
    int cmd_len;
    if (body && body_len > 0) {
        cmd_len = snprintf(cmd, sizeof(cmd),
                 "curl -sL --max-time %d --compressed "
                 "-A '%s' "
                 "-X POST "
                 "-H 'Content-Type: %s' "
                 "--data-binary '@%s' "
                 "'%s' 2>/dev/null",
                 WIZARD_HTTP_TIMEOUT, WIZARD_HTTP_USER_AGENT,
                 content_type ? content_type : "application/octet-stream",
                 tmp_file, url);
    } else {
        /* POST without body */
        cmd_len = snprintf(cmd, sizeof(cmd),
                 "curl -sL --max-time %d --compressed "
                 "-A '%s' "
                 "-X POST "
                 "'%s' 2>/dev/null",
                 WIZARD_HTTP_TIMEOUT, WIZARD_HTTP_USER_AGENT, url);
    }
    if (cmd_len < 0 || cmd_len >= (int)sizeof(cmd)) {
        if (tmp_file[0]) unlink(tmp_file);
        fprintf(stderr, "[-] wizard_http_post: URL too long for command buffer\n");
        return -1;
    }

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        if (tmp_file[0]) unlink(tmp_file);
        fprintf(stderr, "[-] wizard_http_post: Failed to execute curl\n");
        return -1;
    }

    /* Read response in chunks (same as GET) */
    size_t capacity = 65536;
    size_t len = 0;
    *response = malloc(capacity);
    if (!*response) {
        pclose(fp);
        if (tmp_file[0]) unlink(tmp_file);
        error_report_t report;
        error_oom("HTTP POST response buffer allocation", &report);
        error_print(&report);
        return -1;
    }

    char buf[8192];
    while (!feof(fp) && !ferror(fp) && len < WIZARD_HTTP_MAX_RESPONSE_SIZE) {
        size_t n = fread(buf, 1, sizeof(buf), fp);
        if (n > 0) {
            /* Expand buffer if needed, +1 for null terminator */
            if (len + n + 1 >= capacity) {
                while (capacity <= len + n + 1) capacity *= 2;
                char *newbuf = realloc(*response, capacity);
                if (!newbuf) {
                    free(*response);
                    *response = NULL;
                    pclose(fp);
                    if (tmp_file[0]) unlink(tmp_file);
                    error_report_t report;
                    error_context_t ctx = ERROR_CONTEXT_VALUES(
                        ERROR_CAT_MEMORY, ERROR_SEV_ERROR,
                        "HTTP POST response buffer expansion",
                        "Failed to reallocate buffer for growing HTTP response",
                        capacity, capacity / 2
                    );
                    error_report(&ctx, &report);
                    error_print(&report);
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

    /* Clean up temp file */
    if (tmp_file[0]) unlink(tmp_file);

    /* Only check exit status; len==0 is valid (e.g., HTTP 204 No Content) */
    if (status != 0) {
        free(*response);
        *response = NULL;
        *response_len = 0;
        return -1;
    }

    return 0;
}

/* ============================================================================
 * JSON Utilities
 * ============================================================================ */

char* wizard_http_json_escape(const char *str) {
    if (!str) return NULL;

    /* Calculate required buffer size (worst case: every char needs escaping) */
    size_t len = strlen(str);
    size_t max_len = len * 6 + 1;  /* Worst case: \uXXXX for each char */
    char *escaped = malloc(max_len);
    if (!escaped) {
        error_report_t report;
        error_oom("JSON escape buffer allocation", &report);
        error_print(&report);
        return NULL;
    }

    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];

        switch (c) {
            case '"':
                escaped[j++] = '\\';
                escaped[j++] = '"';
                break;
            case '\\':
                escaped[j++] = '\\';
                escaped[j++] = '\\';
                break;
            case '\b':
                escaped[j++] = '\\';
                escaped[j++] = 'b';
                break;
            case '\f':
                escaped[j++] = '\\';
                escaped[j++] = 'f';
                break;
            case '\n':
                escaped[j++] = '\\';
                escaped[j++] = 'n';
                break;
            case '\r':
                escaped[j++] = '\\';
                escaped[j++] = 'r';
                break;
            case '\t':
                escaped[j++] = '\\';
                escaped[j++] = 't';
                break;
            default:
                if (c < 32) {
                    /* Control characters: use \uXXXX */
                    j += snprintf(escaped + j, max_len - j, "\\u%04x", c);
                } else {
                    escaped[j++] = c;
                }
                break;
        }
    }

    escaped[j] = '\0';
    return escaped;
}

/* ============================================================================
 * JSON POST Request
 * ============================================================================ */

int wizard_http_post_json(const char *url, const char *json_body,
                          char **response, size_t *response_len) {
    if (!url || !json_body || !response || !response_len) {
        fprintf(stderr, "[-] wizard_http_post_json: Invalid parameters\n");
        return -1;
    }

    /* Call wizard_http_post with Content-Type: application/json */
    return wizard_http_post(url, json_body, strlen(json_body),
                           "application/json", response, response_len);
}
