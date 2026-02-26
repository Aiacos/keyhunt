/*
 * wizard_http.h - HTTP client module for GET/POST requests
 *
 * Provides reusable HTTP functions with timeout, user-agent,
 * and response buffer management using curl.
 */

#ifndef WIZARD_HTTP_H
#define WIZARD_HTTP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Configuration */
#define WIZARD_HTTP_TIMEOUT 30
#define WIZARD_HTTP_MAX_RESPONSE_SIZE (10 * 1024 * 1024)  /* 10 MB */
#define WIZARD_HTTP_USER_AGENT "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"

/**
 * Perform HTTP GET request
 *
 * @param url          Target URL
 * @param response     Output buffer (caller must free)
 * @param response_len Output response length
 * @return 0 on success, -1 on error
 */
int wizard_http_get(const char *url, char **response, size_t *response_len);

/**
 * Perform HTTP POST request
 *
 * @param url          Target URL
 * @param body         POST body data (can be NULL)
 * @param body_len     POST body length (0 if body is NULL)
 * @param content_type Content-Type header (e.g., "application/json")
 * @param response     Output buffer (caller must free)
 * @param response_len Output response length
 * @return 0 on success, -1 on error
 */
int wizard_http_post(const char *url, const char *body, size_t body_len,
                     const char *content_type, char **response, size_t *response_len);

#ifdef __cplusplus
}
#endif

#endif /* WIZARD_HTTP_H */
