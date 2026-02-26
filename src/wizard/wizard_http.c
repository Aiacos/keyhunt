/*
 * wizard_http.c - HTTP client module implementation
 */

#include "wizard_http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ============================================================================
 * HTTP GET Request
 * ============================================================================ */

int wizard_http_get(const char *url, char **response, size_t *response_len) {
    if (!url || !response || !response_len) {
        fprintf(stderr, "[-] wizard_http_get: Invalid parameters\n");
        return -1;
    }

    /* Build curl command for GET request */
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "curl -sL --max-time %d --compressed "
             "-A '%s' "
             "'%s' 2>/dev/null",
             WIZARD_HTTP_TIMEOUT, WIZARD_HTTP_USER_AGENT, url);

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
        fprintf(stderr, "[-] wizard_http_get: Memory allocation failed\n");
        return -1;
    }

    char buf[8192];
    while (!feof(fp) && len < WIZARD_HTTP_MAX_RESPONSE_SIZE) {
        size_t n = fread(buf, 1, sizeof(buf), fp);
        if (n > 0) {
            /* Expand buffer if needed */
            if (len + n >= capacity) {
                capacity *= 2;
                char *newbuf = realloc(*response, capacity);
                if (!newbuf) {
                    free(*response);
                    pclose(fp);
                    fprintf(stderr, "[-] wizard_http_get: Memory reallocation failed\n");
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
    if (body && body_len > 0) {
        snprintf(cmd, sizeof(cmd),
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
        snprintf(cmd, sizeof(cmd),
                 "curl -sL --max-time %d --compressed "
                 "-A '%s' "
                 "-X POST "
                 "'%s' 2>/dev/null",
                 WIZARD_HTTP_TIMEOUT, WIZARD_HTTP_USER_AGENT, url);
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
        fprintf(stderr, "[-] wizard_http_post: Memory allocation failed\n");
        return -1;
    }

    char buf[8192];
    while (!feof(fp) && len < WIZARD_HTTP_MAX_RESPONSE_SIZE) {
        size_t n = fread(buf, 1, sizeof(buf), fp);
        if (n > 0) {
            /* Expand buffer if needed */
            if (len + n >= capacity) {
                capacity *= 2;
                char *newbuf = realloc(*response, capacity);
                if (!newbuf) {
                    free(*response);
                    pclose(fp);
                    if (tmp_file[0]) unlink(tmp_file);
                    fprintf(stderr, "[-] wizard_http_post: Memory reallocation failed\n");
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

    if (status != 0 || len == 0) {
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
        fprintf(stderr, "[-] wizard_http_json_escape: Memory allocation failed\n");
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
