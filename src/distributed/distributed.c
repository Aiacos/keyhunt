/*
 * distributed.c - Distributed Mode Implementation
 */

#include "distributed.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <poll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>

/* ============================================================================
 * TLS/SSL Support (Optional - requires OpenSSL)
 * ============================================================================
 *
 * This section implements TLS encryption for distributed mode. All OpenSSL-
 * dependent code is wrapped in #ifdef HAVE_OPENSSL guards to allow building
 * without TLS support.
 *
 * - When HAVE_OPENSSL is defined: Full TLS support with OpenSSL
 * - When HAVE_OPENSSL is not defined: Stubs that return errors
 *
 * To build with TLS:    make ENABLE_TLS=1
 * To build without TLS: make (default)
 * ============================================================================ */

#ifdef HAVE_OPENSSL
/* ============================================================================
 * OpenSSL Implementation (HAVE_OPENSSL defined)
 * ============================================================================ */

static bool g_openssl_initialized = false;
static pthread_mutex_t g_openssl_init_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Initialize OpenSSL library (thread-safe, called once) */
static void tls_init_openssl(void) {
    pthread_mutex_lock(&g_openssl_init_mutex);
    if (!g_openssl_initialized) {
        /* OpenSSL 1.1.0+ auto-initializes, but we call this for compatibility */
        OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);
        g_openssl_initialized = true;
    }
    pthread_mutex_unlock(&g_openssl_init_mutex);
}

/* Print OpenSSL error and return -1 */
static int tls_print_error(const char *context) {
    unsigned long err = ERR_get_error();
    char buf[256];
    ERR_error_string_n(err, buf, sizeof(buf));
    fprintf(stderr, "[TLS ERROR] %s: %s\n", context, buf);
    return -1;
}

/* Create SSL context for server */
static SSL_CTX *tls_create_server_context(const char *cert_file, const char *key_file) {
    tls_init_openssl();

    /* Create context with TLS 1.2+ only */
    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) {
        tls_print_error("SSL_CTX_new");
        return NULL;
    }

    /* Set minimum TLS version to 1.2 for security */
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

    /* Load certificate */
    if (SSL_CTX_use_certificate_file(ctx, cert_file, SSL_FILETYPE_PEM) != 1) {
        tls_print_error("SSL_CTX_use_certificate_file");
        SSL_CTX_free(ctx);
        return NULL;
    }

    /* Load private key */
    if (SSL_CTX_use_PrivateKey_file(ctx, key_file, SSL_FILETYPE_PEM) != 1) {
        tls_print_error("SSL_CTX_use_PrivateKey_file");
        SSL_CTX_free(ctx);
        return NULL;
    }

    /* Verify private key matches certificate */
    if (SSL_CTX_check_private_key(ctx) != 1) {
        tls_print_error("SSL_CTX_check_private_key");
        SSL_CTX_free(ctx);
        return NULL;
    }

    return ctx;
}

/* Create SSL context for client */
static SSL_CTX *tls_create_client_context(bool verify_server) {
    tls_init_openssl();

    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        tls_print_error("SSL_CTX_new");
        return NULL;
    }

    /* Set minimum TLS version to 1.2 */
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

    if (verify_server) {
        /* Load system CA certificates */
        SSL_CTX_set_default_verify_paths(ctx);
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
    } else {
        /* Skip server certificate verification (not recommended for production) */
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    }

    return ctx;
}

/* Wrap socket with SSL (server side - accept) */
static SSL *tls_accept(SSL_CTX *ctx, int client_fd) {
    SSL *ssl = SSL_new(ctx);
    if (!ssl) {
        tls_print_error("SSL_new");
        return NULL;
    }

    SSL_set_fd(ssl, client_fd);

    int ret = SSL_accept(ssl);
    if (ret != 1) {
        int err = SSL_get_error(ssl, ret);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            /* Would block - non-blocking mode */
            fprintf(stderr, "[TLS] Handshake would block\n");
        } else {
            tls_print_error("SSL_accept");
        }
        SSL_free(ssl);
        return NULL;
    }

    return ssl;
}

/* Wrap socket with SSL (client side - connect) */
static SSL *tls_connect(SSL_CTX *ctx, int socket_fd) {
    SSL *ssl = SSL_new(ctx);
    if (!ssl) {
        tls_print_error("SSL_new");
        return NULL;
    }

    SSL_set_fd(ssl, socket_fd);

    int ret = SSL_connect(ssl);
    if (ret != 1) {
        int err = SSL_get_error(ssl, ret);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            fprintf(stderr, "[TLS] Handshake would block\n");
        } else {
            tls_print_error("SSL_connect");
        }
        SSL_free(ssl);
        return NULL;
    }

    return ssl;
}

/* Send all data over SSL with retry logic */
static int tls_send_all(SSL *ssl, const void *data, size_t len) {
    const char *ptr = (const char *)data;
    size_t remaining = len;

    while (remaining > 0) {
        int sent = SSL_write(ssl, ptr, (int)remaining);
        if (sent <= 0) {
            int err = SSL_get_error(ssl, sent);
            if (err == SSL_ERROR_WANT_WRITE) {
                continue;  /* Retry */
            }
            tls_print_error("SSL_write");
            return -1;
        }
        ptr += sent;
        remaining -= (size_t)sent;
    }
    return 0;
}

/* Receive data over SSL */
static int tls_recv_all(SSL *ssl, void *buf, size_t len) {
    char *ptr = (char *)buf;
    size_t remaining = len;

    while (remaining > 0) {
        int received = SSL_read(ssl, ptr, (int)remaining);
        if (received <= 0) {
            int err = SSL_get_error(ssl, received);
            if (err == SSL_ERROR_WANT_READ) {
                continue;  /* Retry */
            }
            if (err == SSL_ERROR_ZERO_RETURN) {
                return -1;  /* Connection closed */
            }
            tls_print_error("SSL_read");
            return -1;
        }
        ptr += received;
        remaining -= (size_t)received;
    }
    return 0;
}

#endif /* HAVE_OPENSSL */
/* ============================================================================
 * End of OpenSSL Implementation
 * ============================================================================ */

/* ============================================================================
 * Clean Logging System
 * ============================================================================ */

/* ANSI color codes */
#define CLR_RESET   "\033[0m"
#define CLR_BOLD    "\033[1m"
#define CLR_DIM     "\033[2m"
#define CLR_GREEN   "\033[32m"
#define CLR_YELLOW  "\033[33m"
#define CLR_BLUE    "\033[34m"
#define CLR_CYAN    "\033[36m"
#define CLR_RED     "\033[31m"
#define CLR_MAGENTA "\033[35m"

/* Log prefixes */
#define LOG_SERVER  CLR_CYAN  "[SERVER]" CLR_RESET
#define LOG_CLIENT  CLR_BLUE  "[CLIENT]" CLR_RESET
#define LOG_WORKER  CLR_GREEN "[WORKER]" CLR_RESET
#define LOG_OK      CLR_GREEN "  ✓ " CLR_RESET
#define LOG_INFO    CLR_CYAN  "  ℹ " CLR_RESET
#define LOG_WARN    CLR_YELLOW"  ⚠ " CLR_RESET
#define LOG_WARNING CLR_YELLOW"  ⚠ " CLR_RESET
#define LOG_ERR     CLR_RED   "  ✗ " CLR_RESET
#define LOG_DEBUG   CLR_CYAN  "  ⋯ " CLR_RESET
#define LOG_FOUND   CLR_MAGENTA CLR_BOLD "  ★ " CLR_RESET

/* Simple JSON helpers (minimal, no external deps) with safe buffer handling */
static void json_add_string(char *buf, size_t sz, const char *key, const char *val) {
    size_t current_len = strlen(buf);
    if (current_len >= sz - 1) return;  /* Buffer already full */
    size_t remaining = sz - current_len;
    int written = snprintf(buf + current_len, remaining, "\"%s\":\"%s\",", key, val);
    if (written < 0 || (size_t)written >= remaining) {
        buf[sz - 1] = '\0';  /* Ensure null termination on overflow */
    }
}

static void json_add_int(char *buf, size_t sz, const char *key, int64_t val) {
    size_t current_len = strlen(buf);
    if (current_len >= sz - 1) return;  /* Buffer already full */
    size_t remaining = sz - current_len;
    int written = snprintf(buf + current_len, remaining, "\"%s\":%lld,", key, (long long)val);
    if (written < 0 || (size_t)written >= remaining) {
        buf[sz - 1] = '\0';  /* Ensure null termination on overflow */
    }
}

static void json_add_double(char *buf, size_t sz, const char *key, double val) {
    size_t current_len = strlen(buf);
    if (current_len >= sz - 1) return;  /* Buffer already full */
    size_t remaining = sz - current_len;
    int written = snprintf(buf + current_len, remaining, "\"%s\":%.3f,", key, val);
    if (written < 0 || (size_t)written >= remaining) {
        buf[sz - 1] = '\0';  /* Ensure null termination on overflow */
    }
}

static int json_get_string(const char *json, const char *key, char *out, size_t outsz) {
    if (!json || !key || !out || outsz == 0) return -1;
    out[0] = '\0';  /* Initialize output */

    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *start = strstr(json, pattern);
    /* Verify the match is a real key (preceded by {, comma, or whitespace)
     * to avoid "type" matching inside "subtype" */
    while (start) {
        if (start == json || start[-1] == '{' || start[-1] == ','
            || start[-1] == ' ' || start[-1] == '\t'
            || start[-1] == '\n' || start[-1] == '\r') {
            break;  /* Valid key match */
        }
        start = strstr(start + 1, pattern);  /* Try next occurrence */
    }
    if (!start) return -1;
    start += strlen(pattern);
    /* Skip whitespace after the colon (handles both "key":"value" and "key": "value") */
    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') start++;
    /* Must start with opening quote */
    if (*start != '"') return -1;
    start++;  /* Skip the opening quote */
    const char *end = strchr(start, '"');
    if (!end || end < start) return -1;  /* Also check for inverted pointers */
    size_t len = (size_t)(end - start);
    if (len >= outsz) len = outsz - 1;
    memcpy(out, start, len);  /* memcpy is safer than strncpy here */
    out[len] = '\0';
    return 0;
}

static int64_t json_get_int(const char *json, const char *key) {
    if (!json || !key) return 0;

    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *start = strstr(json, pattern);
    while (start) {
        if (start == json || start[-1] == '{' || start[-1] == ','
            || start[-1] == ' ' || start[-1] == '\t'
            || start[-1] == '\n' || start[-1] == '\r') break;
        start = strstr(start + 1, pattern);
    }
    if (!start) return 0;
    start += strlen(pattern);
    /* Skip whitespace */
    while (*start == ' ' || *start == '\t') start++;
    return strtoll(start, NULL, 10);
}

static double json_get_double(const char *json, const char *key) {
    if (!json || !key) return 0.0;

    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *start = strstr(json, pattern);
    while (start) {
        if (start == json || start[-1] == '{' || start[-1] == ','
            || start[-1] == ' ' || start[-1] == '\t'
            || start[-1] == '\n' || start[-1] == '\r') break;
        start = strstr(start + 1, pattern);
    }
    if (!start) return 0.0;
    start += strlen(pattern);
    /* Skip whitespace */
    while (*start == ' ' || *start == '\t') start++;
    return strtod(start, NULL);
}

/**
 * Extract a nested JSON object as a string.
 * Given {"type":"ack","job":{...}}, extracts the {...} part for "job".
 *
 * @param json Input JSON string
 * @param key Key of the nested object
 * @param out Output buffer for the nested object
 * @param outsz Size of output buffer
 * @return 0 on success, -1 if not found or on error
 */
static int json_get_object(const char *json, const char *key, char *out, size_t outsz) {
    if (!json || !key || !out || outsz == 0) return -1;
    out[0] = '\0';

    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *start = strstr(json, pattern);
    if (!start) return -1;
    start += strlen(pattern);

    /* Skip whitespace */
    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') start++;

    /* Must start with { */
    if (*start != '{') return -1;

    /* Find matching closing brace */
    int depth = 0;
    const char *end = start;
    while (*end) {
        if (*end == '{') depth++;
        else if (*end == '}') {
            depth--;
            if (depth == 0) {
                end++;  /* Include the closing brace */
                break;
            }
        } else if (*end == '"') {
            /* Skip string content to avoid counting braces inside strings */
            end++;
            while (*end && *end != '"') {
                if (*end == '\\' && *(end+1)) end++;  /* Skip escaped chars */
                end++;
            }
        }
        if (*end) end++;
    }

    if (depth != 0) return -1;  /* Unbalanced braces */

    size_t len = (size_t)(end - start);
    if (len >= outsz) len = outsz - 1;
    memcpy(out, start, len);
    out[len] = '\0';
    return 0;
}

static uint64_t time_ms(void) {
    struct timespec ts;
    /* Use CLOCK_REALTIME (Unix epoch) for compatibility with time(NULL) comparisons */
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

/* Set socket to non-blocking */
static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* Set socket receive/send timeout
 * @param fd Socket file descriptor
 * @param timeout_sec Timeout in seconds (0 = no timeout)
 * @return 0 on success, -1 on error
 */
static int set_socket_timeout(int fd, int timeout_sec) {
    if (fd < 0) return -1;

    struct timeval tv;
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;

    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        return -1;
    }
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        return -1;
    }
    return 0;
}

/* Send all data with retry logic for partial sends */
static int send_all(int fd, const void *data, size_t len) {
    const char *ptr = (const char *)data;
    size_t remaining = len;

    while (remaining > 0) {
        ssize_t sent = send(fd, ptr, remaining, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR) continue;  /* Interrupted, retry */
            return -1;  /* Real error */
        }
        if (sent == 0) return -1;  /* Connection closed */
        ptr += sent;
        remaining -= (size_t)sent;
    }
    return 0;
}

/* Send message with length prefix */
static int send_msg(int fd, const char *msg) {
    if (!msg) return -1;

    uint32_t len = (uint32_t)strlen(msg);
    if (len > DIST_MAX_MSG_SIZE) return -1;  /* Message too large */

    uint32_t net_len = htonl(len);

    if (send_all(fd, &net_len, 4) != 0) return -1;
    if (send_all(fd, msg, len) != 0) return -1;
    return 0;
}

/* Receive message with length prefix - with proper bounds checking */
static int recv_msg(int fd, char *buf, size_t bufsz) {
    if (!buf || bufsz < 2) return -1;  /* Need at least space for one char + null */

    uint32_t net_len;
    ssize_t n = recv(fd, &net_len, 4, MSG_WAITALL);
    if (n <= 0) return -1;  /* Connection closed (0) or error (-1) */
    if (n != 4) return -1;  /* Partial read */

    uint32_t len = ntohl(net_len);

    /* Validate message size: must fit with null terminator, and have max limit */
    if (len == 0) return -1;  /* Empty message invalid */
    if (len > DIST_MAX_MSG_SIZE) return -1;  /* Prevent DoS with huge messages */
    if (len >= bufsz) return -1;  /* Must have room for null terminator */

    n = recv(fd, buf, len, MSG_WAITALL);
    if (n <= 0) return -1;  /* Connection closed or error */
    if (n != (ssize_t)len) return -1;  /* Partial read */

    buf[len] = '\0';
    return (int)len;
}

#ifdef HAVE_OPENSSL
/* TLS-aware send message with length prefix */
static int send_msg_tls(SSL *ssl, const char *msg) {
    if (!ssl || !msg) return -1;

    uint32_t len = (uint32_t)strlen(msg);
    if (len > DIST_MAX_MSG_SIZE) return -1;  /* Message too large */

    uint32_t net_len = htonl(len);

    if (tls_send_all(ssl, &net_len, 4) != 0) return -1;
    if (tls_send_all(ssl, msg, len) != 0) return -1;
    return 0;
}

/* TLS-aware receive message with length prefix */
static int recv_msg_tls(SSL *ssl, char *buf, size_t bufsz) {
    if (!ssl || !buf || bufsz < 2) return -1;

    uint32_t net_len;
    if (tls_recv_all(ssl, &net_len, 4) != 0) return -1;

    uint32_t len = ntohl(net_len);

    /* Validate message size */
    if (len == 0) return -1;
    if (len > DIST_MAX_MSG_SIZE) return -1;
    if (len >= bufsz) return -1;

    if (tls_recv_all(ssl, buf, len) != 0) return -1;

    buf[len] = '\0';
    return (int)len;
}
#endif /* HAVE_OPENSSL */

/* ============================================================================
 * Unified Send/Receive Functions (TLS-aware)
 * ============================================================================ */

/* Send message - uses TLS if ssl is non-NULL, otherwise plain TCP */
static int send_msg_ex(int fd, void *ssl_ptr, const char *msg) {
#ifdef HAVE_OPENSSL
    if (ssl_ptr) {
        return send_msg_tls((SSL *)ssl_ptr, msg);
    }
#else
    (void)ssl_ptr;  /* Unused when OpenSSL not available */
#endif
    return send_msg(fd, msg);
}

/* Receive message - uses TLS if ssl is non-NULL, otherwise plain TCP */
static int recv_msg_ex(int fd, void *ssl_ptr, char *buf, size_t bufsz) {
#ifdef HAVE_OPENSSL
    if (ssl_ptr) {
        return recv_msg_tls((SSL *)ssl_ptr, buf, bufsz);
    }
#else
    (void)ssl_ptr;  /* Unused when OpenSSL not available */
#endif
    return recv_msg(fd, buf, bufsz);
}

/* ============================================================================
 * Rate Limiter Implementation
 * ============================================================================ */

static int rate_limiter_init(rate_limiter_t *rl) {
    memset(rl, 0, sizeof(*rl));
    rl->entry_capacity = 256;
    rl->entries = calloc(rl->entry_capacity, sizeof(rate_limit_entry_t));
    if (!rl->entries) return -1;

    if (pthread_mutex_init(&rl->mutex, NULL) != 0) {
        free(rl->entries);
        rl->entries = NULL;
        return -1;
    }

    /* Defaults - can be overridden */
    rl->max_connections_per_window = DIST_RATE_LIMIT_MAX_CONNECTIONS;
    rl->max_messages_per_window = DIST_RATE_LIMIT_MAX_MESSAGES;
    rl->window_sec = DIST_RATE_LIMIT_WINDOW_SEC;
    rl->enabled = false;

    return 0;
}

static void rate_limiter_destroy(rate_limiter_t *rl) {
    if (rl->entries) {
        free(rl->entries);
        rl->entries = NULL;
    }
    pthread_mutex_destroy(&rl->mutex);
}

/* Check if connection from IP is allowed. Returns 1 if allowed, 0 if blocked. */
static int rate_limiter_check_connection(rate_limiter_t *rl, uint32_t ip_addr) {
    if (!rl->enabled) return 1;  /* Rate limiting disabled */

    pthread_mutex_lock(&rl->mutex);

    uint64_t now = time_ms();
    uint64_t window_start = now - (rl->window_sec * 1000ULL);

    /* Find or create entry for this IP */
    rate_limit_entry_t *entry = NULL;
    int free_slot = -1;

    for (int i = 0; i < rl->entry_count; i++) {
        if (rl->entries[i].ip_addr == ip_addr) {
            entry = &rl->entries[i];
            break;
        }
        /* Track first stale entry for potential reuse */
        if (free_slot < 0 && rl->entries[i].window_start < window_start) {
            free_slot = i;
        }
    }

    if (!entry) {
        /* Create new entry */
        if (rl->entry_count < rl->entry_capacity) {
            entry = &rl->entries[rl->entry_count++];
        } else if (free_slot >= 0) {
            entry = &rl->entries[free_slot];
        } else {
            /* Table full, allow connection (fail-open for availability) */
            pthread_mutex_unlock(&rl->mutex);
            return 1;
        }
        entry->ip_addr = ip_addr;
        entry->window_start = now;
        entry->connection_count = 0;
        entry->message_count = 0;
    }

    /* Check if window has expired and reset */
    if (entry->window_start < window_start) {
        entry->window_start = now;
        entry->connection_count = 0;
        entry->message_count = 0;
    }

    /* Check limit */
    int allowed = 1;
    if (entry->connection_count >= rl->max_connections_per_window) {
        allowed = 0;  /* Rate limited */
    } else {
        entry->connection_count++;
    }

    pthread_mutex_unlock(&rl->mutex);
    return allowed;
}

/* Check if message from IP is allowed. Returns 1 if allowed, 0 if blocked. */
/* Currently unused but available for future per-message rate limiting */
__attribute__((unused))
static int rate_limiter_check_message(rate_limiter_t *rl, uint32_t ip_addr) {
    if (!rl->enabled) return 1;  /* Rate limiting disabled */

    pthread_mutex_lock(&rl->mutex);

    uint64_t now = time_ms();
    uint64_t window_start = now - (rl->window_sec * 1000ULL);

    /* Find entry for this IP */
    rate_limit_entry_t *entry = NULL;
    for (int i = 0; i < rl->entry_count; i++) {
        if (rl->entries[i].ip_addr == ip_addr) {
            entry = &rl->entries[i];
            break;
        }
    }

    if (!entry) {
        pthread_mutex_unlock(&rl->mutex);
        return 1;  /* No entry, allow */
    }

    /* Check if window has expired and reset */
    if (entry->window_start < window_start) {
        entry->window_start = now;
        entry->connection_count = 0;
        entry->message_count = 0;
    }

    /* Check limit */
    int allowed = 1;
    if (entry->message_count >= rl->max_messages_per_window) {
        allowed = 0;  /* Rate limited */
    } else {
        entry->message_count++;
    }

    pthread_mutex_unlock(&rl->mutex);
    return allowed;
}

/* ============================================================================
 * JSON Input Sanitization
 * ============================================================================ */

/**
 * Sanitize a string for safe use in JSON parsing.
 * Removes control characters and ensures proper escaping.
 * Returns 0 on success, -1 if input is malformed.
 */
static int sanitize_json_string(char *str, size_t maxlen) {
    if (!str) return -1;

    size_t len = strnlen(str, maxlen);
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];

        /* Remove control characters (except \t, \n, \r which are valid JSON whitespace) */
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') {
            str[i] = ' ';  /* Replace with space */
        }

        /* Check for excessively long strings that might indicate an attack */
        if (i > maxlen - 1) {
            str[maxlen - 1] = '\0';
            return -1;
        }
    }

    return 0;
}

/* ============================================================================
 * Coordinator Implementation
 * ============================================================================ */

int dist_coordinator_check_port(int port, const char *bind_address) {
    int actual_port = (port > 0) ? port : DIST_DEFAULT_PORT;

    /* Create a test socket */
    int test_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (test_sock < 0) {
        return -1;  /* Cannot create socket */
    }

    /* Set SO_REUSEADDR to match actual bind behavior */
    int opt = 1;
    setsockopt(test_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
    setsockopt(test_sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(actual_port);

    if (bind_address && bind_address[0] != '\0') {
        if (inet_pton(AF_INET, bind_address, &addr.sin_addr) != 1) {
            close(test_sock);
            return -2;  /* Invalid bind address */
        }
    } else {
        addr.sin_addr.s_addr = INADDR_ANY;
    }

    int result = bind(test_sock, (struct sockaddr*)&addr, sizeof(addr));
    int bind_errno = errno;
    close(test_sock);

    if (result < 0) {
        if (bind_errno == EADDRINUSE) {
            return 1;  /* Port in use */
        }
        return -1;  /* Other bind error */
    }

    return 0;  /* Port available */
}

int dist_coordinator_init(dist_coordinator_t *coord, int port) {
    memset(coord, 0, sizeof(*coord));
    coord->port = (port > 0) ? port : DIST_DEFAULT_PORT;
    coord->listen_socket = -1;
    coord->bind_address[0] = '\0';  /* Bind to all interfaces by default */
    coord->next_pending_hint = 0;   /* Start searching from beginning */

    /* Initialize fine-grained mutexes for thread-safe operations */
    if (pthread_mutex_init(&coord->work_mutex, NULL) != 0) {
        return -1;
    }
    if (pthread_mutex_init(&coord->stats_mutex, NULL) != 0) {
        pthread_mutex_destroy(&coord->work_mutex);
        return -1;
    }
    if (pthread_mutex_init(&coord->worker_mutex, NULL) != 0) {
        pthread_mutex_destroy(&coord->stats_mutex);
        pthread_mutex_destroy(&coord->work_mutex);
        return -1;
    }

    /* Initialize rate limiter */
    if (rate_limiter_init(&coord->rate_limiter) != 0) {
        pthread_mutex_destroy(&coord->worker_mutex);
        pthread_mutex_destroy(&coord->stats_mutex);
        pthread_mutex_destroy(&coord->work_mutex);
        return -1;
    }

    /* Allocate results array */
    coord->result_capacity = 1024;
    coord->results = calloc(coord->result_capacity, sizeof(dist_result_t));
    if (!coord->results) {
        rate_limiter_destroy(&coord->rate_limiter);
        pthread_mutex_destroy(&coord->worker_mutex);
        pthread_mutex_destroy(&coord->stats_mutex);
        pthread_mutex_destroy(&coord->work_mutex);
        return -1;
    }

    return 0;
}

/* Helper: Parse hex string to 128-bit integer (supports up to 128-bit ranges) */
static __uint128_t parse_hex128(const char *hex) {
    __uint128_t result = 0;
    while (*hex) {
        char c = *hex++;
        int digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else continue;  /* Skip invalid chars */
        result = (result << 4) | digit;
    }
    return result;
}

/* Helper: Convert 128-bit integer to hex string */
static void uint128_to_hex(char *buf, size_t sz, __uint128_t val) {
    if (val == 0) {
        snprintf(buf, sz, "0");
        return;
    }
    /* 128-bit = 32 hex digits max + null terminator */
    char tmp[34];
    int i = 32;
    tmp[33] = '\0';
    tmp[i] = '\0';

    while (val > 0 && i > 0) {
        i--;
        int digit = val & 0xF;
        tmp[i] = digit < 10 ? '0' + digit : 'a' + digit - 10;
        val >>= 4;
    }

    /* Copy result to output buffer */
    size_t len = 32 - i;
    if (len >= sz) len = sz - 1;
    memcpy(buf, &tmp[i], len);
    buf[len] = '\0';
}

int dist_coordinator_set_range(dist_coordinator_t *coord,
                               const char *range_start, const char *range_end,
                               uint64_t work_unit_size) {
    if (!coord || !range_start || !range_end) return -1;

    /* Parse range as hex - using 128-bit integers for puzzles up to 128 bits */
    __uint128_t start = parse_hex128(range_start);
    __uint128_t end = parse_hex128(range_end);

    if (end <= start) {
        printf(LOG_SERVER LOG_ERR "Invalid range: end <= start\n");
        return -1;
    }

    __uint128_t total = end - start;

    /* Store as 64-bit for stats (may overflow for very large ranges) */
    if (total > ((__uint128_t)1 << 64)) {
        coord->total_keys = UINT64_MAX;  /* Capped for display */
    } else {
        coord->total_keys = (uint64_t)total;
    }

    /* Calculate number of work units */
    __uint128_t work_size = (__uint128_t)work_unit_size;
    __uint128_t num_units_128 = (total + work_size - 1) / work_size;

    int num_units;
    if (num_units_128 > 100000) {
        num_units = 100000;  /* Limit to prevent excessive memory */
        printf(LOG_SERVER LOG_WARN "Range too large, limiting to %d work units\n", num_units);
    } else {
        num_units = (int)num_units_128;
    }

    coord->work_units = calloc(num_units, sizeof(dist_work_unit_t));
    if (!coord->work_units) return -1;

    /* Create work units */
    __uint128_t pos = start;
    for (int i = 0; i < num_units && pos < end; i++) {
        __uint128_t unit_end = pos + work_size;
        if (unit_end > end) unit_end = end;

        dist_work_unit_t *unit = &coord->work_units[i];
        unit->id = i;
        uint128_to_hex(unit->range_start, sizeof(unit->range_start), pos);
        uint128_to_hex(unit->range_end, sizeof(unit->range_end), unit_end);
        unit->status = WORK_STATUS_PENDING;
        unit->assigned_worker = -1;

        /* Keys in unit (capped to 64-bit for stats) */
        __uint128_t keys = unit_end - pos;
        unit->keys_in_unit = keys > UINT64_MAX ? UINT64_MAX : (uint64_t)keys;

        coord->work_unit_count++;
        coord->work_units_pending++;
        pos = unit_end;
    }

    printf(LOG_SERVER LOG_OK "Created %d work units (range 0x%.16s...)\n",
           coord->work_unit_count, range_start);
    return coord->work_unit_count;
}

int dist_coordinator_start(dist_coordinator_t *coord) {
    /* Create socket */
    coord->listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (coord->listen_socket < 0) {
        perror("[Coordinator] socket");
        return -1;
    }

    /* Set socket options for immediate port reuse after restart.
     * SO_REUSEADDR allows binding to a port in TIME_WAIT state (e.g., after
     * previous process exited but TCP connections not fully closed).
     * SO_REUSEPORT (Linux 3.9+) allows multiple sockets to bind to the same
     * port, which is useful for graceful restarts. */
    int opt = 1;
    if (setsockopt(coord->listen_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        /* Non-fatal, but log it */
        perror("[Coordinator] setsockopt SO_REUSEADDR");
    }

#ifdef SO_REUSEPORT
    /* SO_REUSEPORT provides better behavior on Linux for rapid restarts */
    if (setsockopt(coord->listen_socket, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        /* Non-fatal on systems where it's not supported */
        /* Don't log - may not be available on all systems */
    }
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(coord->port);

    /* Bind to specific interface if configured, otherwise all interfaces */
    if (coord->bind_address[0] != '\0') {
        if (inet_pton(AF_INET, coord->bind_address, &addr.sin_addr) != 1) {
            printf(LOG_SERVER LOG_ERR "Invalid bind address: %s\n", coord->bind_address);
            close(coord->listen_socket);
            coord->listen_socket = -1;
            return -1;
        }
        printf(LOG_SERVER LOG_INFO "Binding to specific interface: %s\n", coord->bind_address);
    } else {
        addr.sin_addr.s_addr = INADDR_ANY;
    }

    if (bind(coord->listen_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        int bind_errno = errno;
        close(coord->listen_socket);
        coord->listen_socket = -1;

        if (bind_errno == EADDRINUSE) {
            printf(LOG_SERVER LOG_ERR "Port %d is already in use.\n", coord->port);
            printf(LOG_SERVER LOG_INFO "This usually means:\n");
            printf("    1. Another keyhunt server is already running on this port\n");
            printf("    2. A previous instance didn't shut down cleanly\n");
            printf("\n  To fix this:\n");
            printf("    - Check for running instances: lsof -i :%d\n", coord->port);
            printf("    - Kill the process: kill <PID>\n");
            printf("    - Or wait ~60 seconds for the port to be released\n");
            printf("    - Or use a different port with --port <N>\n");
        } else {
            printf(LOG_SERVER LOG_ERR "bind() failed: %s\n", strerror(bind_errno));
        }
        return -1;
    }

    if (listen(coord->listen_socket, 16) < 0) {
        perror("[Coordinator] listen");
        close(coord->listen_socket);
        coord->listen_socket = -1;
        return -1;
    }

    set_nonblocking(coord->listen_socket);
    coord->running = true;

    if (coord->bind_address[0] != '\0') {
        printf(LOG_SERVER LOG_OK "Listening on %s:%d\n", coord->bind_address, coord->port);
    } else {
        printf(LOG_SERVER LOG_OK "Listening on port " CLR_BOLD "%d" CLR_RESET " (all interfaces)\n", coord->port);
    }

    /* Log security settings */
    if (coord->rate_limiter.enabled) {
        printf(LOG_SERVER LOG_INFO "Rate limiting enabled: %d conn/%ds, %d msg/%ds\n",
               coord->rate_limiter.max_connections_per_window,
               coord->rate_limiter.window_sec,
               coord->rate_limiter.max_messages_per_window,
               coord->rate_limiter.window_sec);
    }
    if (coord->auth_enabled) {
        printf(LOG_SERVER LOG_INFO "Authentication enabled\n");
    }
    if (coord->tls_enabled) {
        printf(LOG_SERVER LOG_INFO "TLS enabled (cert: %s)\n", coord->tls_cert_file);
    }

    return 0;
}

/* Find next pending work unit - optimized with hint for O(1) average case
 * IMPORTANT: Caller must hold work_mutex */
static dist_work_unit_t* find_pending_work(dist_coordinator_t *coord) {
    int count = coord->work_unit_count;
    int hint = coord->next_pending_hint;

    /* Start from hint position for O(1) average case when assigning sequentially */
    for (int i = hint; i < count; i++) {
        if (coord->work_units[i].status == WORK_STATUS_PENDING) {
            coord->next_pending_hint = i + 1;  /* Update hint for next call */
            return &coord->work_units[i];
        }
    }

    /* Wrap around from beginning (handles reassigned work units) */
    for (int i = 0; i < hint; i++) {
        if (coord->work_units[i].status == WORK_STATUS_PENDING) {
            coord->next_pending_hint = i + 1;
            return &coord->work_units[i];
        }
    }

    return NULL;
}

/* ============================================================================
 * Per-Worker Handler Thread
 * ============================================================================
 *
 * Each connected worker gets its own dedicated thread for handling messages.
 * This ensures:
 * 1. One slow/stuck client doesn't block other clients
 * 2. Work assignment happens in parallel for multiple clients
 * 3. Network I/O doesn't block the coordinator's main loop
 * 4. CPU performance remains constant regardless of client count
 * ============================================================================ */

/* Forward declaration */
static int handle_worker_msg(dist_coordinator_t *coord, int worker_idx, const char *msg);

/**
 * Worker handler thread function - runs continuously for each connected worker.
 * Processes messages from the worker without blocking other workers.
 */
static void *worker_handler_thread(void *arg) {
    dist_worker_t *worker = (dist_worker_t *)arg;
    dist_coordinator_t *coord = (dist_coordinator_t *)worker->coordinator;
    int worker_idx = worker->id;

    /* Set socket to blocking with reasonable timeout for this thread */
    set_socket_timeout(worker->socket_fd, 10);  /* 10 second timeout for recv */

    while (worker->handler_running && coord->running && worker->connected) {
        char msg[DIST_MAX_MSG_SIZE];
        int n = recv_msg_ex(worker->socket_fd, worker->ssl, msg, sizeof(msg));

        if (n > 0) {
            /* Process the message - this may take a lock briefly but releases quickly */
            handle_worker_msg(coord, worker_idx, msg);
        } else if (n == 0) {
            /* Connection closed gracefully */
            break;
        } else {
            /* Error or timeout - check if we should continue */
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT) {
                /* Timeout is OK - just means no data yet, keep waiting */
                continue;
            }
            /* Real error - disconnect */
            break;
        }
    }

    /* Mark worker as disconnected */
    pthread_mutex_lock(&coord->worker_mutex);
    worker->connected = false;
    worker->handler_running = false;

    /* Reassign any pending work from this worker */
    if (worker->current_work_id >= 0 && worker->current_work_id < coord->work_unit_count) {
        pthread_mutex_lock(&coord->work_mutex);
        dist_work_unit_t *unit = &coord->work_units[worker->current_work_id];
        if (unit->status == WORK_STATUS_ASSIGNED && unit->assigned_worker == worker->id) {
            unit->status = WORK_STATUS_PENDING;
            unit->assigned_worker = -1;
            coord->work_units_pending++;
            /* Reset hint to search from this reclaimed unit */
            if (worker->current_work_id < coord->next_pending_hint) {
                coord->next_pending_hint = worker->current_work_id;
            }
        }
        pthread_mutex_unlock(&coord->work_mutex);
        worker->current_work_id = -1;
    }
    pthread_mutex_unlock(&coord->worker_mutex);

    printf(LOG_SERVER LOG_WARN "Worker " CLR_YELLOW "#%d" CLR_RESET " (%s) handler thread exited\n",
           worker->id, worker->hostname[0] ? worker->hostname : "localhost");

    /* Close socket */
#ifdef HAVE_OPENSSL
    if (worker->ssl) {
        SSL_shutdown((SSL *)worker->ssl);
        SSL_free((SSL *)worker->ssl);
        worker->ssl = NULL;
    }
#endif
    if (worker->socket_fd >= 0) {
        close(worker->socket_fd);
        worker->socket_fd = -1;
    }

    return NULL;
}

/**
 * Start a dedicated handler thread for a worker.
 * Called after successful registration.
 */
static int start_worker_handler_thread(dist_coordinator_t *coord, dist_worker_t *worker) {
    worker->coordinator = coord;
    worker->handler_running = true;

    if (pthread_create(&worker->handler_thread, NULL, worker_handler_thread, worker) != 0) {
        worker->handler_running = false;
        printf(LOG_SERVER LOG_ERR "Failed to create handler thread for worker #%d\n", worker->id);
        return -1;
    }

    /* Detach thread so it cleans up automatically when done */
    pthread_detach(worker->handler_thread);

    return 0;
}

/* Handle worker message - called from worker's dedicated handler thread
 * Uses fine-grained locking for scalability with many clients */
static int handle_worker_msg(dist_coordinator_t *coord, int worker_idx, const char *msg) {
    dist_worker_t *worker = &coord->workers[worker_idx];
    char type[32] = {0};
    json_get_string(msg, "type", type, sizeof(type));

    if (strcmp(type, "request_work") == 0) {
        /* Update heartbeat atomically - no lock needed for single write */
        worker->last_heartbeat = time_ms();

        char response[DIST_MAX_MSG_SIZE];
        int work_id = -1;
        char range_start[65] = {0};
        char range_end[65] = {0};

        /* Brief lock for work assignment only - release before I/O */
        pthread_mutex_lock(&coord->work_mutex);

        dist_work_unit_t *unit = find_pending_work(coord);

        if (unit) {
            unit->status = WORK_STATUS_ASSIGNED;
            unit->assigned_worker = worker->id;
            unit->assigned_time = time_ms();
            worker->current_work_id = unit->id;
            coord->work_units_pending--;

            /* Copy data under lock, build response outside */
            work_id = unit->id;
            strncpy(range_start, unit->range_start, sizeof(range_start) - 1);
            strncpy(range_end, unit->range_end, sizeof(range_end) - 1);
        }

        pthread_mutex_unlock(&coord->work_mutex);
        /* Lock released before network I/O - critical for performance */

        /* Build response outside the lock */
        if (work_id >= 0) {
            snprintf(response, sizeof(response), "{");
            json_add_string(response, sizeof(response), "type", "work_assignment");
            json_add_int(response, sizeof(response), "work_id", work_id);
            json_add_string(response, sizeof(response), "range_start", range_start);
            json_add_string(response, sizeof(response), "range_end", range_end);
            /* Remove trailing comma and close JSON object */
            size_t len = strlen(response);
            if (len > 0 && response[len-1] == ',') response[len-1] = '\0';
            len = strlen(response);
            if (len + 2 <= sizeof(response)) {
                response[len] = '}';
                response[len + 1] = '\0';
            }
        } else {
            snprintf(response, sizeof(response), "{\"type\":\"no_work\"}");
        }

        /* Network I/O happens outside lock */
        if (send_msg_ex(worker->socket_fd, worker->ssl, response) != 0) {
            printf(LOG_SERVER LOG_WARN "Failed to send response to worker %d\n",
                   worker->id);
        }

    } else if (strcmp(type, "work_done") == 0) {
        int work_id = (int)json_get_int(msg, "work_id");
        uint64_t keys = (uint64_t)json_get_int(msg, "keys_processed");
        uint64_t elapsed = (uint64_t)json_get_int(msg, "elapsed_ms");

        /* Update heartbeat atomically */
        worker->last_heartbeat = time_ms();

        /* Brief lock for work unit status update only */
        pthread_mutex_lock(&coord->work_mutex);

        if (work_id >= 0 && work_id < coord->work_unit_count) {
            dist_work_unit_t *unit = &coord->work_units[work_id];
            unit->status = WORK_STATUS_COMPLETED;
            unit->completed_time = time_ms();
            coord->work_units_completed++;
        }

        pthread_mutex_unlock(&coord->work_mutex);

        /* Update statistics with separate lock - doesn't block work assignment */
        pthread_mutex_lock(&coord->stats_mutex);
        worker->keys_processed += keys;
        coord->keys_processed += keys;
        pthread_mutex_unlock(&coord->stats_mutex);

        if (elapsed > 0) {
            /* Check if worker sent separate CPU/GPU speeds (hybrid mode).
             * If both are present and non-zero, use them directly.
             * Otherwise fall back to the old heuristic. */
            double msg_cpu_speed = json_get_double(msg, "cpu_speed_mkeys");
            double msg_gpu_speed = json_get_double(msg, "gpu_speed_mkeys");

            /* Update speed stats atomically with stats_mutex to ensure
             * dashboard reads consistent values */
            pthread_mutex_lock(&coord->stats_mutex);

            worker->throughput = (double)keys / (double)elapsed * 1000.0 / 1000000.0;

            if (msg_cpu_speed > 0.0 || msg_gpu_speed > 0.0) {
                /* Worker sent explicit speeds - use them directly */
                worker->cpu_speed_mkeys = msg_cpu_speed;
                worker->gpu_speed_mkeys = msg_gpu_speed;
            } else {
                /* Fallback: assign total throughput based on hardware presence */
                if (worker->gpu_memory_mb > 0) {
                    worker->gpu_speed_mkeys = worker->throughput;
                } else {
                    worker->cpu_speed_mkeys = worker->throughput;
                }
            }

            pthread_mutex_unlock(&coord->stats_mutex);
        }

        /* Send ack */
        send_msg_ex(worker->socket_fd, worker->ssl, "{\"type\":\"ack\"}");

    } else if (strcmp(type, "found") == 0) {
        char privkey[65] = {0}, address[36] = {0};
        json_get_string(msg, "private_key", privkey, sizeof(privkey));
        json_get_string(msg, "address", address, sizeof(address));

        if (coord->result_count < coord->result_capacity) {
            dist_result_t *result = &coord->results[coord->result_count++];
            strncpy(result->private_key, privkey, sizeof(result->private_key)-1);
            strncpy(result->address, address, sizeof(result->address)-1);
            result->worker_id = worker->id;
            result->found_time = time_ms();

            printf("\n" LOG_SERVER LOG_FOUND CLR_BOLD CLR_MAGENTA "KEY FOUND!" CLR_RESET " Worker #%d\n", worker->id);
            printf(LOG_INFO "Private Key: " CLR_BOLD "%s" CLR_RESET "\n", privkey);
            printf(LOG_INFO "Address:     " CLR_BOLD "%s" CLR_RESET "\n\n", address);
        }

        send_msg_ex(worker->socket_fd, worker->ssl, "{\"type\":\"ack\"}");

    } else if (strcmp(type, "heartbeat") == 0) {
        worker->last_heartbeat = time_ms();
        send_msg_ex(worker->socket_fd, worker->ssl, "{\"type\":\"ack\"}");
    }

    return 0;
}

/* ============================================================================
 * Coordinator Main Processing Loop
 * ============================================================================
 *
 * ARCHITECTURE: Thread-per-client model for non-blocking operation
 *
 * The main process loop ONLY handles:
 * 1. Accepting new connections (fast, non-blocking)
 * 2. Initial client registration and authentication
 * 3. Spawning dedicated handler threads for each client
 * 4. Periodic health checks and statistics updates
 *
 * Each client's messages are handled by its own dedicated thread, ensuring:
 * - One slow client doesn't block others
 * - Work assignment happens in parallel
 * - CPU performance remains constant regardless of client count
 * - Network I/O never blocks the coordinator
 * ============================================================================ */

int dist_coordinator_process(dist_coordinator_t *coord, int timeout_ms) {
    if (!coord->running) return -1;

    struct pollfd pfd;
    pfd.fd = coord->listen_socket;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int ready = poll(&pfd, 1, timeout_ms);
    if (ready < 0) {
        if (errno == EINTR) return 0;
        return -1;
    }

    /* Check for new connections - this is the only blocking part now */
    if (ready > 0 && (pfd.revents & POLLIN)) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(coord->listen_socket, (struct sockaddr*)&client_addr, &client_len);

        if (client_fd >= 0) {
            /* Rate limiting check */
            uint32_t client_ip = ntohl(client_addr.sin_addr.s_addr);
            if (!rate_limiter_check_connection(&coord->rate_limiter, client_ip)) {
                /* Rate limited - reject connection silently */
                close(client_fd);
                return 0;
            }
        }

        pthread_mutex_lock(&coord->worker_mutex);
        if (client_fd >= 0 && coord->worker_count < DIST_MAX_WORKERS) {
            int opt = 1;
            setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

            /* Short timeout for registration - don't let malicious clients hang us */
            set_socket_timeout(client_fd, 10);  /* 10 second timeout for initial handshake */

            /* TLS handshake if enabled */
            void *client_ssl = NULL;
#ifdef HAVE_OPENSSL
            if (coord->tls_enabled && coord->ssl_ctx) {
                client_ssl = tls_accept(coord->ssl_ctx, client_fd);
                if (!client_ssl) {
                    printf(LOG_SERVER LOG_WARN "TLS handshake failed for new connection\n");
                    close(client_fd);
                    pthread_mutex_unlock(&coord->worker_mutex);
                    return 0;  /* Reject connection */
                }
            }
#endif

            /* Read registration message */
            char msg[DIST_MAX_MSG_SIZE];
            if (recv_msg_ex(client_fd, client_ssl, msg, sizeof(msg)) > 0) {
                /* Sanitize JSON input to prevent injection attacks */
                sanitize_json_string(msg, sizeof(msg));

                char type[32] = {0};
                json_get_string(msg, "type", type, sizeof(type));

                if (strcmp(type, "register") == 0) {
                    /* Validate authentication token if enabled */
                    bool auth_passed = true;
                    if (coord->auth_enabled) {
                        char provided_token[DIST_AUTH_TOKEN_MAX] = {0};
                        json_get_string(msg, "auth_token", provided_token, sizeof(provided_token));

                        /* Constant-time comparison to prevent timing attacks */
                        size_t expected_len = strlen(coord->auth_token);
                        size_t provided_len = strlen(provided_token);

                        if (expected_len != provided_len) {
                            auth_passed = false;
                        } else {
                            volatile int diff = 0;
                            for (size_t i = 0; i < expected_len; i++) {
                                diff |= coord->auth_token[i] ^ provided_token[i];
                            }
                            auth_passed = (diff == 0);
                        }

                        if (!auth_passed) {
                            char hostname[64] = {0};
                            json_get_string(msg, "hostname", hostname, sizeof(hostname));
                            printf(LOG_SERVER LOG_ERR "Authentication failed from %s - invalid token\n",
                                   hostname[0] ? hostname : "unknown");
                            send_msg_ex(client_fd, client_ssl, "{\"type\":\"auth_failed\",\"message\":\"Invalid authentication token\"}");
#ifdef HAVE_OPENSSL
                            if (client_ssl) SSL_free((SSL *)client_ssl);
#endif
                            close(client_fd);
                        }
                    }

                    if (auth_passed) {
                        dist_worker_t *worker = &coord->workers[coord->worker_count];
                        worker->id = coord->worker_count;
                        worker->socket_fd = client_fd;
                        worker->ssl = client_ssl;  /* Store SSL handle (NULL if TLS disabled) */
                        worker->connected = true;
                        worker->last_heartbeat = time_ms();
                        worker->current_work_id = -1;
                        worker->handler_running = false;
                        worker->perf_score = json_get_double(msg, "perf_score");
                        json_get_string(msg, "hostname", worker->hostname, sizeof(worker->hostname));

                        /* Parse detailed hardware info */
                        worker->cpu_cores = (int)json_get_int(msg, "cpu_cores");
                        worker->cpu_threads = (int)json_get_int(msg, "cpu_threads");
                        json_get_string(msg, "cpu_name", worker->cpu_name, sizeof(worker->cpu_name));
                        json_get_string(msg, "gpu_name", worker->gpu_name, sizeof(worker->gpu_name));
                        worker->gpu_memory_mb = (int)json_get_int(msg, "gpu_memory_mb");
                        worker->cpu_speed_mkeys = json_get_double(msg, "cpu_speed_mkeys");
                        worker->gpu_speed_mkeys = json_get_double(msg, "gpu_speed_mkeys");

                        coord->worker_count++;

                        /* Send welcome with job config */
                        char welcome[DIST_MAX_MSG_SIZE];
                        snprintf(welcome, sizeof(welcome),
                                 "{\"type\":\"welcome\",\"worker_id\":%d,\"work_units\":%d,"
                                 "\"target_address\":\"%s\",\"mode\":\"%s\",\"key_type\":\"%s\","
                                 "\"puzzle_number\":%d,\"bits\":%d,\"heartbeat_interval\":%d,\"tls\":%s}",
                                 worker->id, coord->work_unit_count,
                                 coord->job_target_address,
                                 coord->job_mode,
                                 coord->job_key_type,
                                 coord->job_puzzle_number,
                                 coord->job_bits,
                                 coord->heartbeat_interval_sec > 0 ? coord->heartbeat_interval_sec : 30,
                                 coord->tls_enabled ? "true" : "false");
                        send_msg_ex(worker->socket_fd, worker->ssl, welcome);

                        /* Print connection info with hardware details and speeds */
                        printf(LOG_SERVER LOG_OK "Worker " CLR_GREEN "#%d" CLR_RESET " connected from " CLR_BOLD "%s" CLR_RESET "\n",
                               worker->id, worker->hostname[0] ? worker->hostname : "localhost");
                        if (worker->cpu_name[0]) {
                            printf(LOG_SERVER LOG_INFO "  Hardware: %s", worker->cpu_name);
                            if (worker->gpu_name[0]) {
                                printf(" + %s", worker->gpu_name);
                            }
                            printf("\n");
                        }
                        /* Show speeds */
                        double worker_total = worker->cpu_speed_mkeys + worker->gpu_speed_mkeys;
                        printf(LOG_SERVER LOG_INFO "  Speed: CPU: " CLR_CYAN "%.2f" CLR_RESET " Mkeys/s",
                               worker->cpu_speed_mkeys);
                        if (worker->gpu_speed_mkeys > 0) {
                            printf(" | GPU: " CLR_GREEN "%.2f" CLR_RESET " Mkeys/s", worker->gpu_speed_mkeys);
                        }
                        printf(" | Total: " CLR_BOLD CLR_GREEN "%.2f" CLR_RESET " Mkeys/s\n", worker_total);

                        /* Start dedicated handler thread for this worker */
                        if (start_worker_handler_thread(coord, worker) != 0) {
                            printf(LOG_SERVER LOG_ERR "Failed to start handler thread, falling back to polling\n");
                            /* Worker is still registered, will be handled by fallback in health check */
                        }
                    }  /* end if (auth_passed) */
                }
            } else {
#ifdef HAVE_OPENSSL
                if (client_ssl) SSL_free((SSL *)client_ssl);
#endif
                close(client_fd);
            }
        } else if (client_fd >= 0) {
            close(client_fd);  /* Too many workers */
        }
        pthread_mutex_unlock(&coord->worker_mutex);
    }

    /* ==========================================================================
     * Periodic health checks and statistics updates
     * These run in the main loop but don't block client handling
     * ========================================================================== */

    uint64_t now_ms = time_ms();
    static uint64_t last_health_check_ms = 0;

    /* Only do health checks every 5 seconds to avoid overhead */
    if (now_ms - last_health_check_ms > 5000) {
        last_health_check_ms = now_ms;

        uint64_t stale_timeout_ms = 5 * 60 * 1000;  /* 5 minutes */

        /* Check for stale work units (assigned but worker unresponsive) */
        pthread_mutex_lock(&coord->work_mutex);
        for (int i = 0; i < coord->work_unit_count; i++) {
            dist_work_unit_t *unit = &coord->work_units[i];
            if (unit->status == WORK_STATUS_ASSIGNED) {
                /* Check if assigned worker is still connected and responsive */
                bool worker_connected = false;
                bool worker_responsive = false;

                for (int w = 0; w < coord->worker_count; w++) {
                    if (coord->workers[w].id == unit->assigned_worker) {
                        worker_connected = coord->workers[w].connected;
                        if (worker_connected) {
                            /* Check if heartbeat is recent */
                            worker_responsive = (now_ms - coord->workers[w].last_heartbeat < stale_timeout_ms);
                        }
                        break;
                    }
                }

                /* Reassign if:
                 * 1. Worker is disconnected, OR
                 * 2. Worker connected but unresponsive AND assignment is old */
                bool should_reassign = false;
                if (!worker_connected) {
                    should_reassign = true;
                } else if (!worker_responsive && (now_ms - unit->assigned_time > stale_timeout_ms)) {
                    should_reassign = true;
                }

                if (should_reassign) {
                    printf(LOG_SERVER LOG_WARN "Work unit #%d timed out, reassigning\n", unit->id);
                    unit->status = WORK_STATUS_PENDING;
                    unit->assigned_worker = -1;
                    coord->work_units_pending++;
                    /* Update hint for faster lookup */
                    if (unit->id < coord->next_pending_hint) {
                        coord->next_pending_hint = unit->id;
                    }
                }
            }
        }
        pthread_mutex_unlock(&coord->work_mutex);

        /* Check for stuck workers (connected but no progress for too long) */
        uint64_t stuck_timeout_ms = 3 * 60 * 1000;  /* 3 minutes no progress */

        pthread_mutex_lock(&coord->worker_mutex);
        for (int i = 0; i < coord->worker_count; i++) {
            dist_worker_t *worker = &coord->workers[i];
            if (worker->connected && worker->socket_fd >= 0) {
                /* Worker is stuck if no heartbeat for 3 minutes */
                if (now_ms - worker->last_heartbeat > stuck_timeout_ms) {
                    printf(LOG_SERVER LOG_WARN "Worker #%d (%s) stuck for 3+ min, forcing disconnect\n",
                           worker->id, worker->hostname[0] ? worker->hostname : "localhost");

                    /* Stop the handler thread - it will clean up the socket */
                    worker->handler_running = false;
                    worker->connected = false;
                    worker->throughput = 0.0;

                    /* Reassign any work this worker had */
                    pthread_mutex_lock(&coord->work_mutex);
                    if (worker->current_work_id >= 0 &&
                        worker->current_work_id < coord->work_unit_count) {
                        dist_work_unit_t *unit = &coord->work_units[worker->current_work_id];
                        if (unit->status == WORK_STATUS_ASSIGNED) {
                            unit->status = WORK_STATUS_PENDING;
                            unit->assigned_worker = -1;
                            coord->work_units_pending++;
                            /* Update hint for faster lookup */
                            if (unit->id < coord->next_pending_hint) {
                                coord->next_pending_hint = unit->id;
                            }
                            printf(LOG_SERVER LOG_INFO "Work unit #%d reassigned to pool\n", unit->id);
                        }
                    }
                    pthread_mutex_unlock(&coord->work_mutex);
                }
            }
        }
        pthread_mutex_unlock(&coord->worker_mutex);

        /* Update total throughput - only during health check to avoid overhead */
        coord->total_throughput = 0.0;
        for (int i = 0; i < coord->worker_count; i++) {
            if (coord->workers[i].connected) {
                coord->total_throughput += coord->workers[i].throughput;
            }
        }
    }  /* end health check block */

    /* Check if all done */
    if (coord->work_units_completed >= coord->work_unit_count) {
        coord->all_work_done = true;
        return 1;
    }

    return 0;
}

void dist_coordinator_stats(const dist_coordinator_t *coord,
                            int *workers_active, int *work_pending,
                            int *work_completed, double *throughput) {
    int active = 0;
    for (int i = 0; i < coord->worker_count; i++) {
        if (coord->workers[i].connected) active++;
    }

    if (workers_active) *workers_active = active;
    if (work_pending) *work_pending = coord->work_units_pending;
    if (work_completed) *work_completed = coord->work_units_completed;
    if (throughput) *throughput = coord->total_throughput;
}

void dist_coordinator_get_speed_stats(const dist_coordinator_t *coord,
                                      double *total_cpu_speed,
                                      double *total_gpu_speed,
                                      double *total_combined) {
    double cpu_sum = 0.0;
    double gpu_sum = 0.0;

    /* Lock stats_mutex to ensure consistent reads with worker updates.
     * We need to cast away const since we're only reading, but the mutex
     * requires non-const access. This is safe because we're not modifying
     * any data, just synchronizing reads. */
    dist_coordinator_t *mutable_coord = (dist_coordinator_t *)coord;
    pthread_mutex_lock(&mutable_coord->stats_mutex);

    for (int i = 0; i < coord->worker_count; i++) {
        if (coord->workers[i].connected) {
            cpu_sum += coord->workers[i].cpu_speed_mkeys;
            gpu_sum += coord->workers[i].gpu_speed_mkeys;
        }
    }

    pthread_mutex_unlock(&mutable_coord->stats_mutex);

    if (total_cpu_speed) *total_cpu_speed = cpu_sum;
    if (total_gpu_speed) *total_gpu_speed = gpu_sum;
    if (total_combined) *total_combined = cpu_sum + gpu_sum;
}

void dist_coordinator_print_worker_stats(const dist_coordinator_t *coord) {
    double total_cpu = 0.0;
    double total_gpu = 0.0;
    int active_count = 0;

    printf("\n" CLR_CYAN "┌──────────────────────────────────────────────────────────────────┐" CLR_RESET "\n");
    printf(CLR_CYAN "│" CLR_RESET CLR_BOLD "  Worker Statistics                                               " CLR_RESET CLR_CYAN "│" CLR_RESET "\n");
    printf(CLR_CYAN "├──────┬────────────────┬────────────────┬────────────────┬─────────┤" CLR_RESET "\n");
    printf(CLR_CYAN "│" CLR_RESET " ID   " CLR_CYAN "│" CLR_RESET " CPU (Mkeys/s) " CLR_CYAN "│" CLR_RESET " GPU (Mkeys/s) " CLR_CYAN "│" CLR_RESET " Total (Mkeys/s)" CLR_CYAN "│" CLR_RESET " Status " CLR_CYAN "│" CLR_RESET "\n");
    printf(CLR_CYAN "├──────┼────────────────┼────────────────┼────────────────┼─────────┤" CLR_RESET "\n");

    for (int i = 0; i < coord->worker_count; i++) {
        const dist_worker_t *w = &coord->workers[i];
        double worker_total = w->cpu_speed_mkeys + w->gpu_speed_mkeys;
        const char *status = w->connected ? CLR_GREEN "Online" CLR_RESET : CLR_RED "Offline" CLR_RESET;

        printf(CLR_CYAN "│" CLR_RESET " %4d " CLR_CYAN "│" CLR_RESET " %14.2f " CLR_CYAN "│" CLR_RESET " %14.2f " CLR_CYAN "│" CLR_RESET " %14.2f " CLR_CYAN "│" CLR_RESET " %s " CLR_CYAN "│" CLR_RESET "\n",
               w->id, w->cpu_speed_mkeys, w->gpu_speed_mkeys, worker_total, status);

        if (w->connected) {
            total_cpu += w->cpu_speed_mkeys;
            total_gpu += w->gpu_speed_mkeys;
            active_count++;
        }
    }

    printf(CLR_CYAN "├──────┴────────────────┴────────────────┴────────────────┴─────────┤" CLR_RESET "\n");
    printf(CLR_CYAN "│" CLR_RESET CLR_BOLD " TOTAL (%d workers): CPU: %.2f | GPU: %.2f | Combined: " CLR_GREEN "%.2f" CLR_RESET " Mkeys/s " CLR_CYAN "│" CLR_RESET "\n",
           active_count, total_cpu, total_gpu, total_cpu + total_gpu);
    printf(CLR_CYAN "└──────────────────────────────────────────────────────────────────┘" CLR_RESET "\n\n");
}

void dist_coordinator_shutdown(dist_coordinator_t *coord) {
    coord->running = false;

    /* Stop all worker handler threads first */
    for (int i = 0; i < coord->worker_count; i++) {
        coord->workers[i].handler_running = false;
    }

    /* Give threads a moment to notice the shutdown flag */
    usleep(100000);  /* 100ms */

    /* Close worker connections */
    for (int i = 0; i < coord->worker_count; i++) {
        if (coord->workers[i].socket_fd >= 0) {
            send_msg_ex(coord->workers[i].socket_fd, coord->workers[i].ssl, "{\"type\":\"shutdown\"}");
#ifdef HAVE_OPENSSL
            if (coord->workers[i].ssl) {
                SSL_shutdown((SSL *)coord->workers[i].ssl);
                SSL_free((SSL *)coord->workers[i].ssl);
                coord->workers[i].ssl = NULL;
            }
#endif
            close(coord->workers[i].socket_fd);
            coord->workers[i].socket_fd = -1;
        }
    }

    if (coord->listen_socket >= 0) {
        close(coord->listen_socket);
    }

#ifdef HAVE_OPENSSL
    /* Free SSL context */
    if (coord->ssl_ctx) {
        SSL_CTX_free(coord->ssl_ctx);
        coord->ssl_ctx = NULL;
    }
#endif

    free(coord->work_units);
    free(coord->results);

    /* Destroy all mutexes */
    pthread_mutex_destroy(&coord->work_mutex);
    pthread_mutex_destroy(&coord->stats_mutex);
    pthread_mutex_destroy(&coord->worker_mutex);

    /* Destroy rate limiter */
    rate_limiter_destroy(&coord->rate_limiter);

    printf(LOG_SERVER LOG_INFO "Shutdown complete. " CLR_BOLD "%d" CLR_RESET " results found.\n", coord->result_count);
}

/* ============================================================================
 * Worker Client Implementation
 * ============================================================================ */

int dist_worker_init(dist_worker_client_t *client,
                     const char *coordinator_host, int coordinator_port,
                     double perf_score) {
    memset(client, 0, sizeof(*client));
    strncpy(client->coordinator_host, coordinator_host, sizeof(client->coordinator_host)-1);
    client->coordinator_port = (coordinator_port > 0) ? coordinator_port : DIST_DEFAULT_PORT;
    client->perf_score = perf_score;
    client->socket_fd = -1;

    /* Generate worker ID */
    char hostname[48] = {0};  /* Leave room for dash and PID in worker_id */
    gethostname(hostname, sizeof(hostname)-1);
    hostname[sizeof(hostname)-1] = '\0';
    snprintf(client->worker_id, sizeof(client->worker_id), "%s-%d", hostname, getpid());

    return 0;
}

int dist_worker_connect(dist_worker_client_t *client) {
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", client->coordinator_port);

    if (getaddrinfo(client->coordinator_host, port_str, &hints, &res) != 0) {
        return -1;
    }

    client->socket_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (client->socket_fd < 0) {
        freeaddrinfo(res);
        return -1;
    }

    /* Use non-blocking connect with 10-second timeout to avoid kernel
     * default (75-150s) that freezes the client with no feedback. */
    {
        int flags = fcntl(client->socket_fd, F_GETFL, 0);
        fcntl(client->socket_fd, F_SETFL, flags | O_NONBLOCK);

        int rc = connect(client->socket_fd, res->ai_addr, res->ai_addrlen);
        if (rc < 0 && errno != EINPROGRESS) {
            close(client->socket_fd);
            client->socket_fd = -1;
            freeaddrinfo(res);
            return -1;
        }
        if (rc < 0) {  /* EINPROGRESS: wait for completion */
            struct pollfd pfd = { .fd = client->socket_fd, .events = POLLOUT };
            int poll_rc = poll(&pfd, 1, 10000);  /* 10-second timeout */
            if (poll_rc <= 0) {
                close(client->socket_fd);
                client->socket_fd = -1;
                freeaddrinfo(res);
                return -1;
            }
            /* Check if connect actually succeeded */
            int so_error = 0;
            socklen_t len = sizeof(so_error);
            getsockopt(client->socket_fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
            if (so_error != 0) {
                close(client->socket_fd);
                client->socket_fd = -1;
                freeaddrinfo(res);
                return -1;
            }
        }
        /* Restore blocking mode */
        fcntl(client->socket_fd, F_SETFL, flags);
    }

    freeaddrinfo(res);

    int opt = 1;
    setsockopt(client->socket_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

#ifdef HAVE_OPENSSL
    /* Perform TLS handshake if enabled */
    if (client->tls_enabled && client->ssl_ctx) {
        client->ssl = tls_connect(client->ssl_ctx, client->socket_fd);
        if (!client->ssl) {
            printf(LOG_CLIENT LOG_ERR "TLS handshake failed\n");
            close(client->socket_fd);
            client->socket_fd = -1;
            return -1;
        }
        printf(LOG_CLIENT LOG_OK "TLS connection established\n");
    }
#endif

    /* Send registration with hardware info */
    char hostname[64] = {0};
    gethostname(hostname, sizeof(hostname)-1);

    char msg[DIST_MAX_MSG_SIZE];
    snprintf(msg, sizeof(msg), "{");
    json_add_string(msg, sizeof(msg), "type", "register");
    json_add_string(msg, sizeof(msg), "id", client->worker_id);
    json_add_string(msg, sizeof(msg), "hostname", hostname);
    json_add_double(msg, sizeof(msg), "perf_score", client->perf_score);
    json_add_int(msg, sizeof(msg), "cpu_cores", client->cpu_cores);
    json_add_int(msg, sizeof(msg), "cpu_threads", client->cpu_threads);
    json_add_string(msg, sizeof(msg), "cpu_name", client->cpu_name);
    json_add_string(msg, sizeof(msg), "gpu_name", client->gpu_name);
    json_add_int(msg, sizeof(msg), "gpu_memory_mb", client->gpu_memory_mb);
    json_add_double(msg, sizeof(msg), "cpu_speed_mkeys", client->cpu_speed_mkeys);
    json_add_double(msg, sizeof(msg), "gpu_speed_mkeys", client->gpu_speed_mkeys);
    /* Include local completed count for cross-execution resume */
    json_add_int(msg, sizeof(msg), "local_completed_count", client->local_completed_count);
    /* Include auth token if set */
    if (client->auth_token[0] != '\0') {
        json_add_string(msg, sizeof(msg), "auth_token", client->auth_token);
    }
    size_t len = strlen(msg);
    if (len > 0 && msg[len-1] == ',') msg[len-1] = '\0';
    len = strlen(msg);
    if (len + 2 <= sizeof(msg)) {
        msg[len] = '}';
        msg[len + 1] = '\0';
    }

    if (send_msg_ex(client->socket_fd, client->ssl, msg) != 0) {
        dist_worker_disconnect(client);
        return -1;
    }

    /* Wait for welcome with job config */
    char response[DIST_MAX_MSG_SIZE];
    if (recv_msg_ex(client->socket_fd, client->ssl, response, sizeof(response)) <= 0) {
        dist_worker_disconnect(client);
        return -1;
    }

    /* Check for authentication failure */
    char resp_type[32] = {0};
    json_get_string(response, "type", resp_type, sizeof(resp_type));
    if (strcmp(resp_type, "auth_failed") == 0) {
        char error_msg[256] = {0};
        json_get_string(response, "message", error_msg, sizeof(error_msg));
        printf(LOG_CLIENT LOG_ERR "Authentication failed: %s\n",
               error_msg[0] ? error_msg : "Invalid token");
        dist_worker_disconnect(client);
        return -1;
    }

    /* Parse job config from welcome/ack message.
     * Support both formats:
     * 1. Flat format: {"type":"welcome","target_address":"...","puzzle_number":66,...}
     * 2. Nested format: {"type":"ack","job":{"target":"...","puzzle":66,...},...}
     */
    char job_object[DIST_MAX_MSG_SIZE] = {0};
    const char *config_source = response;  /* Default to parsing from response directly */

    /* Check if there's a nested "job" object */
    if (json_get_object(response, "job", job_object, sizeof(job_object)) == 0) {
        /* Use the nested job object for parsing config */
        config_source = job_object;

        /* Nested format uses different field names: "target" instead of "target_address" */
        json_get_string(config_source, "target", client->received_target_address,
                        sizeof(client->received_target_address));
        json_get_string(config_source, "mode", client->received_mode, sizeof(client->received_mode));
        json_get_string(config_source, "key_type", client->received_key_type, sizeof(client->received_key_type));
        /* Nested format uses "puzzle" instead of "puzzle_number" */
        client->received_puzzle_number = (int)json_get_int(config_source, "puzzle");
        /* Try "bits" first, fall back to calculating from puzzle number if not present */
        client->received_bits = (int)json_get_int(config_source, "bits");
        if (client->received_bits == 0 && client->received_puzzle_number > 0) {
            client->received_bits = client->received_puzzle_number;  /* For puzzles, bits == puzzle number */
        }
    } else {
        /* Flat format with original field names */
        json_get_string(config_source, "target_address", client->received_target_address,
                        sizeof(client->received_target_address));
        json_get_string(config_source, "mode", client->received_mode, sizeof(client->received_mode));
        json_get_string(config_source, "key_type", client->received_key_type, sizeof(client->received_key_type));
        client->received_puzzle_number = (int)json_get_int(config_source, "puzzle_number");
        client->received_bits = (int)json_get_int(config_source, "bits");
    }

    /* Heartbeat interval is always at the top level */
    client->heartbeat_interval_sec = (int)json_get_int(response, "heartbeat_interval");
    if (client->heartbeat_interval_sec <= 0) client->heartbeat_interval_sec = 30;

    client->connected = true;
    printf(LOG_CLIENT LOG_OK "Connected to " CLR_BOLD "%s:%d" CLR_RESET "\n",
           client->coordinator_host, client->coordinator_port);

    if (client->received_target_address[0]) {
        printf(LOG_CLIENT LOG_INFO "Job: Puzzle " CLR_BOLD "#%d" CLR_RESET " (%d bits) | Mode: %s\n",
               client->received_puzzle_number, client->received_bits, client->received_mode);
        printf(LOG_CLIENT LOG_INFO "Target: %.40s%s\n", client->received_target_address,
               strlen(client->received_target_address) > 40 ? "..." : "");
    }

    return 0;
}

int dist_worker_request_work(dist_worker_client_t *client,
                             char *range_start, char *range_end) {
    if (!client->connected) return -1;

    if (send_msg_ex(client->socket_fd, client->ssl, "{\"type\":\"request_work\"}") != 0) {
        return -1;
    }

    char response[DIST_MAX_MSG_SIZE];
    int recv_len = recv_msg_ex(client->socket_fd, client->ssl, response, sizeof(response));
    if (recv_len <= 0) {
        return -1;
    }

    char type[32] = {0};
    json_get_string(response, "type", type, sizeof(type));

    /* Debug: log received response type if KEYHUNT_DEBUG is set */
    if (getenv("KEYHUNT_DEBUG")) {
        printf(LOG_CLIENT " [DEBUG] Request work response type='%s'\n", type);
    }

    /* Accept both "work_assignment" (canonical) and "work" (alternative) */
    if (strcmp(type, "work_assignment") == 0 || strcmp(type, "work") == 0) {
        client->current_work_id = (int)json_get_int(response, "work_id");
        json_get_string(response, "range_start", range_start, 65);
        json_get_string(response, "range_end", range_end, 65);
        strncpy(client->current_range_start, range_start, sizeof(client->current_range_start)-1);
        strncpy(client->current_range_end, range_end, sizeof(client->current_range_end)-1);
        client->has_work = true;
        return 0;
    } else if (strcmp(type, "no_work") == 0) {
        client->has_work = false;
        return 1;  /* No more work */
    }

    /* Unknown response type - log it for debugging */
    if (getenv("KEYHUNT_DEBUG")) {
        printf(LOG_CLIENT LOG_WARN "Unknown work response type '%s', raw: %.100s\n",
               type, response);
    }

    return -1;
}

int dist_worker_report_done(dist_worker_client_t *client,
                            uint64_t keys_processed, uint64_t elapsed_ms) {
    if (!client->connected || !client->has_work) return -1;

    char msg[512];
    snprintf(msg, sizeof(msg), "{");
    json_add_string(msg, sizeof(msg), "type", "work_done");
    json_add_int(msg, sizeof(msg), "work_id", client->current_work_id);
    json_add_int(msg, sizeof(msg), "keys_processed", keys_processed);
    json_add_int(msg, sizeof(msg), "elapsed_ms", elapsed_ms);
    /* Include CPU and GPU speeds for hybrid mode dashboard display */
    json_add_double(msg, sizeof(msg), "cpu_speed_mkeys", client->cpu_speed_mkeys);
    json_add_double(msg, sizeof(msg), "gpu_speed_mkeys", client->gpu_speed_mkeys);
    size_t len = strlen(msg);
    if (len > 0 && msg[len-1] == ',') msg[len-1] = '\0';
    len = strlen(msg);
    if (len + 2 <= sizeof(msg)) {
        msg[len] = '}';
        msg[len + 1] = '\0';
    }

    if (send_msg_ex(client->socket_fd, client->ssl, msg) != 0) return -1;

    char response[DIST_MAX_MSG_SIZE];
    if (recv_msg_ex(client->socket_fd, client->ssl, response, sizeof(response)) <= 0) return -1;

    client->keys_processed += keys_processed;
    client->has_work = false;
    return 0;
}

int dist_worker_report_found(dist_worker_client_t *client,
                             const char *private_key, const char *address) {
    if (!client->connected) return -1;

    char msg[512];
    snprintf(msg, sizeof(msg), "{");
    json_add_string(msg, sizeof(msg), "type", "found");
    json_add_string(msg, sizeof(msg), "private_key", private_key);
    json_add_string(msg, sizeof(msg), "address", address);
    size_t len = strlen(msg);
    if (len > 0 && msg[len-1] == ',') msg[len-1] = '\0';
    len = strlen(msg);
    if (len + 2 <= sizeof(msg)) {
        msg[len] = '}';
        msg[len + 1] = '\0';
    }

    if (send_msg_ex(client->socket_fd, client->ssl, msg) != 0) return -1;

    char response[DIST_MAX_MSG_SIZE];
    return (recv_msg_ex(client->socket_fd, client->ssl, response, sizeof(response)) > 0) ? 0 : -1;
}

int dist_worker_heartbeat(dist_worker_client_t *client, uint64_t keys_since_last) {
    if (!client->connected) return -1;

    char msg[256];
    snprintf(msg, sizeof(msg), "{\"type\":\"heartbeat\",\"keys\":%llu}",
             (unsigned long long)keys_since_last);

    if (send_msg_ex(client->socket_fd, client->ssl, msg) != 0) return -1;

    char response[DIST_MAX_MSG_SIZE];
    return (recv_msg_ex(client->socket_fd, client->ssl, response, sizeof(response)) > 0) ? 0 : -1;
}

void dist_worker_disconnect(dist_worker_client_t *client) {
#ifdef HAVE_OPENSSL
    /* Shutdown and free SSL connection */
    if (client->ssl) {
        SSL_shutdown((SSL *)client->ssl);
        SSL_free((SSL *)client->ssl);
        client->ssl = NULL;
    }
    /* Free SSL context */
    if (client->ssl_ctx) {
        SSL_CTX_free(client->ssl_ctx);
        client->ssl_ctx = NULL;
    }
#endif
    if (client->socket_fd >= 0) {
        close(client->socket_fd);
        client->socket_fd = -1;
    }
    client->connected = false;
    client->tls_enabled = false;
    printf(LOG_CLIENT LOG_INFO "Disconnected\n");
}

/* ============================================================================
 * New API Functions for Protocol Enhancements
 * ============================================================================ */

void dist_coordinator_set_job_config(dist_coordinator_t *coordinator,
                                     const char *target_address,
                                     const char *mode,
                                     const char *key_type,
                                     int puzzle_number,
                                     int bits) {
    if (!coordinator) return;

    if (target_address) {
        strncpy(coordinator->job_target_address, target_address,
                sizeof(coordinator->job_target_address) - 1);
    }
    if (mode) {
        strncpy(coordinator->job_mode, mode, sizeof(coordinator->job_mode) - 1);
    }
    if (key_type) {
        strncpy(coordinator->job_key_type, key_type, sizeof(coordinator->job_key_type) - 1);
    }
    coordinator->job_puzzle_number = puzzle_number;
    coordinator->job_bits = bits;

    printf(LOG_SERVER LOG_INFO "Job: Puzzle " CLR_BOLD "#%d" CLR_RESET " (%d bits) | Mode: %s | Keys: %s\n",
           puzzle_number, bits, mode ? mode : "?", key_type ? key_type : "?");
}

void dist_coordinator_set_heartbeat_interval(dist_coordinator_t *coordinator,
                                             int interval_sec) {
    if (!coordinator) return;
    coordinator->heartbeat_interval_sec = interval_sec > 0 ? interval_sec : 30;
}

void dist_coordinator_set_auth_token(dist_coordinator_t *coordinator,
                                     const char *token) {
    if (!coordinator) return;

    if (token && token[0] != '\0') {
        strncpy(coordinator->auth_token, token, DIST_AUTH_TOKEN_MAX - 1);
        coordinator->auth_token[DIST_AUTH_TOKEN_MAX - 1] = '\0';
        coordinator->auth_enabled = true;
        printf(LOG_SERVER LOG_INFO "Authentication enabled (token required)\n");
    } else {
        coordinator->auth_token[0] = '\0';
        coordinator->auth_enabled = false;
    }
}

void dist_coordinator_set_worker_timeout(dist_coordinator_t *coordinator,
                                         int timeout_sec) {
    if (!coordinator) return;
    coordinator->worker_timeout_sec = timeout_sec > 0 ? timeout_sec : 180;
}

void dist_coordinator_set_work_timeout(dist_coordinator_t *coordinator,
                                       int timeout_sec) {
    if (!coordinator) return;
    coordinator->work_timeout_sec = timeout_sec > 0 ? timeout_sec : 300;
}

void dist_coordinator_set_connection_timeout(dist_coordinator_t *coordinator,
                                             int timeout_sec) {
    if (!coordinator) return;
    coordinator->connection_timeout_sec = timeout_sec > 0 ? timeout_sec : 30;
}

void dist_worker_set_hardware_info(dist_worker_client_t *client,
                                   int cpu_cores, int cpu_threads,
                                   const char *cpu_name,
                                   const char *gpu_name, int gpu_memory_mb) {
    if (!client) return;

    client->cpu_cores = cpu_cores;
    client->cpu_threads = cpu_threads;
    if (cpu_name) {
        strncpy(client->cpu_name, cpu_name, sizeof(client->cpu_name) - 1);
    }
    if (gpu_name) {
        strncpy(client->gpu_name, gpu_name, sizeof(client->gpu_name) - 1);
    }
    client->gpu_memory_mb = gpu_memory_mb;
}

int dist_worker_get_job_config(const dist_worker_client_t *client,
                               char *target_address,
                               char *mode,
                               char *key_type) {
    if (!client || !client->connected) return -1;
    if (!client->received_target_address[0]) return -1;  /* No config received */

    if (target_address) {
        strncpy(target_address, client->received_target_address, 63);
        target_address[63] = '\0';
    }
    if (mode) {
        strncpy(mode, client->received_mode, 31);
        mode[31] = '\0';
    }
    if (key_type) {
        strncpy(key_type, client->received_key_type, 15);
        key_type[15] = '\0';
    }
    return 0;
}

int dist_worker_get_heartbeat_interval(const dist_worker_client_t *client) {
    if (!client) return 30;
    return client->heartbeat_interval_sec > 0 ? client->heartbeat_interval_sec : 30;
}

void dist_worker_set_auth_token(dist_worker_client_t *client, const char *token) {
    if (!client) return;

    if (token && token[0] != '\0') {
        strncpy(client->auth_token, token, DIST_AUTH_TOKEN_MAX - 1);
        client->auth_token[DIST_AUTH_TOKEN_MAX - 1] = '\0';
    } else {
        client->auth_token[0] = '\0';
    }
}

void dist_worker_set_local_progress(dist_worker_client_t *client, int count) {
    if (!client) return;
    client->local_completed_count = count;
}

int dist_worker_enable_tls(dist_worker_client_t *client, bool verify_server) {
    if (!client) return -1;

#ifdef HAVE_OPENSSL
    /* Create SSL context for client */
    SSL_CTX *ctx = tls_create_client_context(verify_server);
    if (!ctx) {
        printf(LOG_CLIENT LOG_ERR "Failed to create TLS context\n");
        return -1;
    }

    /* Clean up existing context if any */
    if (client->ssl_ctx) {
        SSL_CTX_free(client->ssl_ctx);
    }

    client->ssl_ctx = ctx;
    client->tls_enabled = true;

    printf(LOG_CLIENT LOG_OK "TLS enabled (verify=%s)\n", verify_server ? "yes" : "no");
    return 0;
#else
    printf(LOG_CLIENT LOG_ERR "TLS support not compiled in (requires OpenSSL)\n");
    printf(LOG_CLIENT LOG_INFO "Rebuild with: make ENABLE_TLS=1\n");
    (void)verify_server;
    return -1;
#endif
}

/* ============================================================================
 * Persistent State Functions
 * ============================================================================ */

/* State file format version - increment when format changes */
#define STATE_VERSION 1

void dist_coordinator_get_state_path(const dist_coordinator_t *coordinator,
                                     char *filepath, size_t filepath_size) {
    if (!coordinator || !filepath || filepath_size == 0) return;

    /* Generate path based on puzzle number and target */
    if (coordinator->job_puzzle_number > 0) {
        snprintf(filepath, filepath_size, "coordinator_state_puzzle%d.json",
                 coordinator->job_puzzle_number);
    } else {
        snprintf(filepath, filepath_size, "coordinator_state.json");
    }
}

int dist_coordinator_save_state(const dist_coordinator_t *coordinator,
                                const char *filepath) {
    if (!coordinator || !filepath) return -1;

    /* Open with restricted permissions (owner-only read/write) to protect
     * auth tokens and sensitive coordinator state from other users. */
    int fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        printf(LOG_SERVER LOG_ERR "Failed to save state to %s: %s\n",
               filepath, strerror(errno));
        return -1;
    }
    FILE *f = fdopen(fd, "w");
    if (!f) {
        printf(LOG_SERVER LOG_ERR "Failed to open state file stream: %s\n",
               strerror(errno));
        close(fd);
        return -1;
    }

    /* Write header */
    fprintf(f, "{\n");
    fprintf(f, "  \"version\": %d,\n", STATE_VERSION);
    fprintf(f, "  \"save_time\": %llu,\n", (unsigned long long)time(NULL));

    /* Job configuration - for validation on load */
    fprintf(f, "  \"job_target_address\": \"%s\",\n", coordinator->job_target_address);
    fprintf(f, "  \"job_mode\": \"%s\",\n", coordinator->job_mode);
    fprintf(f, "  \"job_key_type\": \"%s\",\n", coordinator->job_key_type);
    fprintf(f, "  \"job_puzzle_number\": %d,\n", coordinator->job_puzzle_number);
    fprintf(f, "  \"job_bits\": %d,\n", coordinator->job_bits);

    /* Progress statistics */
    fprintf(f, "  \"total_keys\": %llu,\n", (unsigned long long)coordinator->total_keys);
    fprintf(f, "  \"keys_processed\": %llu,\n", (unsigned long long)coordinator->keys_processed);
    fprintf(f, "  \"work_unit_count\": %d,\n", coordinator->work_unit_count);
    fprintf(f, "  \"work_units_completed\": %d,\n", coordinator->work_units_completed);

    // TODO: [MEDIUM] Auth token is stored in plaintext. File permissions are now
    //   restricted to 0600, but consider hashing (SHA-256) before persisting for
    //   defense-in-depth if the hash module gains a C-linkage API.
    /* Authentication (if enabled) */
    if (coordinator->auth_enabled && coordinator->auth_token[0]) {
        fprintf(f, "  \"auth_token\": \"%s\",\n", coordinator->auth_token);
    }

    /* Work units array - only save PENDING and COMPLETED status */
    fprintf(f, "  \"work_units\": [\n");
    int first_unit = 1;
    for (int i = 0; i < coordinator->work_unit_count; i++) {
        const dist_work_unit_t *unit = &coordinator->work_units[i];

        /* Only save completed units (PENDING units will be regenerated) */
        if (unit->status == WORK_STATUS_COMPLETED) {
            if (!first_unit) fprintf(f, ",\n");
            first_unit = 0;

            fprintf(f, "    {\"id\": %d, \"range_start\": \"%s\", \"range_end\": \"%s\", "
                       "\"status\": %d, \"keys_in_unit\": %llu}",
                    unit->id, unit->range_start, unit->range_end,
                    (int)unit->status, (unsigned long long)unit->keys_in_unit);
        }
    }
    fprintf(f, "\n  ],\n");

    /* Results array */
    fprintf(f, "  \"results\": [\n");
    for (int i = 0; i < coordinator->result_count; i++) {
        const dist_result_t *result = &coordinator->results[i];
        if (i > 0) fprintf(f, ",\n");
        fprintf(f, "    {\"private_key\": \"%s\", \"address\": \"%s\", "
                   "\"worker_id\": %d, \"found_time\": %llu}",
                result->private_key, result->address,
                result->worker_id, (unsigned long long)result->found_time);
    }
    fprintf(f, "\n  ]\n");

    fprintf(f, "}\n");
    fclose(f);

    printf(LOG_SERVER LOG_OK "State saved to %s (%d/%d units completed)\n",
           filepath, coordinator->work_units_completed, coordinator->work_unit_count);
    return 0;
}

int dist_coordinator_load_state(dist_coordinator_t *coordinator,
                                const char *filepath) {
    if (!coordinator || !filepath) return -1;

    FILE *f = fopen(filepath, "r");
    if (!f) {
        if (errno == ENOENT) {
            return 1;  /* File not found - not an error, just no state to restore */
        }
        printf(LOG_SERVER LOG_ERR "Failed to open state file %s: %s\n",
               filepath, strerror(errno));
        return -1;
    }

    /* Read entire file */
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 100 * 1024 * 1024) {  /* Max 100MB */
        printf(LOG_SERVER LOG_ERR "Invalid state file size: %ld\n", fsize);
        fclose(f);
        return -1;
    }

    char *json = (char *)malloc((size_t)fsize + 1);
    if (!json) {
        fclose(f);
        return -1;
    }

    size_t read_bytes = fread(json, 1, (size_t)fsize, f);
    fclose(f);
    json[read_bytes] = '\0';

    /* Validate version */
    int version = (int)json_get_int(json, "version");
    if (version != STATE_VERSION) {
        printf(LOG_SERVER LOG_WARN "State file version mismatch (got %d, expected %d)\n",
               version, STATE_VERSION);
        free(json);
        return -1;
    }

    /* Load job configuration */
    char saved_target[64] = {0};
    char saved_mode[32] = {0};
    json_get_string(json, "job_target_address", saved_target, sizeof(saved_target));
    json_get_string(json, "job_mode", saved_mode, sizeof(saved_mode));
    (void)json_get_int(json, "job_puzzle_number");  /* Read for validation, value unused */

    /* Validate job matches current configuration */
    if (coordinator->job_target_address[0] && saved_target[0]) {
        if (strcmp(coordinator->job_target_address, saved_target) != 0) {
            printf(LOG_SERVER LOG_WARN "State file target mismatch - not loading\n");
            printf(LOG_SERVER LOG_INFO "  Current: %s\n", coordinator->job_target_address);
            printf(LOG_SERVER LOG_INFO "  Saved:   %s\n", saved_target);
            free(json);
            return -1;
        }
    }

    /* Load authentication token if present */
    char saved_auth[DIST_AUTH_TOKEN_MAX] = {0};
    if (json_get_string(json, "auth_token", saved_auth, sizeof(saved_auth)) == 0) {
        if (saved_auth[0]) {
            strncpy(coordinator->auth_token, saved_auth, DIST_AUTH_TOKEN_MAX - 1);
            coordinator->auth_enabled = true;
        }
    }

    /* Load progress statistics */
    coordinator->keys_processed = (uint64_t)json_get_int(json, "keys_processed");
    /* Note: work_units_completed is recalculated from work_units array */

    /* Parse completed work units to mark them as done */
    const char *units_start = strstr(json, "\"work_units\":");
    if (units_start) {
        units_start = strchr(units_start, '[');
        if (units_start) {
            units_start++;

            int units_restored = 0;

            /* Parse each unit object */
            const char *unit_ptr = units_start;
            while ((unit_ptr = strchr(unit_ptr, '{')) != NULL) {
                const char *unit_end = strchr(unit_ptr, '}');
                if (!unit_end) break;

                /* Extract unit ID */
                char unit_json[512];
                size_t unit_len = (size_t)(unit_end - unit_ptr + 1);
                if (unit_len >= sizeof(unit_json)) {
                    unit_ptr = unit_end + 1;
                    continue;
                }
                memcpy(unit_json, unit_ptr, unit_len);
                unit_json[unit_len] = '\0';

                int unit_id = (int)json_get_int(unit_json, "id");
                int unit_status = (int)json_get_int(unit_json, "status");

                /* Mark this unit as completed in current work units */
                if (unit_id >= 0 && unit_id < coordinator->work_unit_count) {
                    if (unit_status == WORK_STATUS_COMPLETED) {
                        coordinator->work_units[unit_id].status = WORK_STATUS_COMPLETED;
                        units_restored++;
                    }
                }

                unit_ptr = unit_end + 1;
            }

            /* Recalculate pending/completed counts */
            coordinator->work_units_completed = 0;
            coordinator->work_units_pending = 0;
            for (int i = 0; i < coordinator->work_unit_count; i++) {
                if (coordinator->work_units[i].status == WORK_STATUS_COMPLETED) {
                    coordinator->work_units_completed++;
                } else {
                    coordinator->work_units_pending++;
                }
            }

            printf(LOG_SERVER LOG_OK "Restored %d completed work units from state file\n",
                   units_restored);
        }
    }

    /* Parse results */
    const char *results_start = strstr(json, "\"results\":");
    if (results_start) {
        results_start = strchr(results_start, '[');
        if (results_start) {
            results_start++;

            const char *result_ptr = results_start;
            while ((result_ptr = strchr(result_ptr, '{')) != NULL) {
                const char *result_end = strchr(result_ptr, '}');
                if (!result_end) break;

                char result_json[512];
                size_t result_len = (size_t)(result_end - result_ptr + 1);
                if (result_len >= sizeof(result_json)) {
                    result_ptr = result_end + 1;
                    continue;
                }
                memcpy(result_json, result_ptr, result_len);
                result_json[result_len] = '\0';

                /* Add to results */
                if (coordinator->result_count < coordinator->result_capacity) {
                    dist_result_t *r = &coordinator->results[coordinator->result_count];
                    json_get_string(result_json, "private_key", r->private_key, sizeof(r->private_key));
                    json_get_string(result_json, "address", r->address, sizeof(r->address));
                    r->worker_id = (int)json_get_int(result_json, "worker_id");
                    r->found_time = (uint64_t)json_get_int(result_json, "found_time");

                    if (r->private_key[0]) {
                        coordinator->result_count++;
                        printf(LOG_SERVER LOG_FOUND "Restored found key: %s\n", r->private_key);
                    }
                }

                result_ptr = result_end + 1;
            }
        }
    }

    free(json);

    printf(LOG_SERVER LOG_OK "State loaded: %d/%d work units completed, %d results\n",
           coordinator->work_units_completed, coordinator->work_unit_count,
           coordinator->result_count);

    return 0;
}

/* ============================================================================
 * Multi-Coordinator Federation Implementation
 * ============================================================================ */

#define LOG_FEDERATION CLR_MAGENTA "[FEDERATION]" CLR_RESET

int dist_federation_init_primary(dist_coordinator_t *coordinator,
                                 int federation_port) {
    if (!coordinator) return -1;

    coordinator->federation.role = FEDERATION_PRIMARY;
    coordinator->federation.peer_count = 0;
    coordinator->federation.sync_interval_sec = 30;
    coordinator->federation.results_shared = false;
    memset(coordinator->federation.peers, 0, sizeof(coordinator->federation.peers));

    printf(LOG_FEDERATION LOG_OK "Initialized as PRIMARY coordinator\n");
    printf(LOG_FEDERATION LOG_INFO "Federation port: %d (same as worker port)\n",
           federation_port > 0 ? federation_port : coordinator->port);

    return 0;
}

int dist_federation_init_secondary(dist_coordinator_t *coordinator,
                                   const char *primary_host, int primary_port) {
    if (!coordinator || !primary_host) return -1;

    coordinator->federation.role = FEDERATION_SECONDARY;
    strncpy(coordinator->federation.primary_host, primary_host,
            sizeof(coordinator->federation.primary_host) - 1);
    coordinator->federation.primary_port = primary_port > 0 ? primary_port : DIST_DEFAULT_PORT;
    coordinator->federation.peer_count = 0;
    coordinator->federation.sync_interval_sec = 30;
    coordinator->federation.results_shared = false;

    printf(LOG_FEDERATION LOG_OK "Initialized as SECONDARY coordinator\n");
    printf(LOG_FEDERATION LOG_INFO "Primary: %s:%d\n",
           coordinator->federation.primary_host,
           coordinator->federation.primary_port);

    return 0;
}

int dist_federation_add_peer(dist_coordinator_t *coordinator,
                             const char *host, int port) {
    if (!coordinator || !host) return -1;

    if (coordinator->federation.role != FEDERATION_PRIMARY) {
        printf(LOG_FEDERATION LOG_ERR "Only primary can add peers\n");
        return -1;
    }

    if (coordinator->federation.peer_count >= DIST_MAX_FEDERATION) {
        printf(LOG_FEDERATION LOG_ERR "Maximum federation peers reached (%d)\n",
               DIST_MAX_FEDERATION);
        return -1;
    }

    int idx = coordinator->federation.peer_count;
    dist_federation_peer_t *peer = &coordinator->federation.peers[idx];

    strncpy(peer->host, host, sizeof(peer->host) - 1);
    peer->port = port > 0 ? port : DIST_DEFAULT_PORT;
    peer->socket_fd = -1;
    peer->connected = false;
    peer->is_primary = false;
    peer->last_heartbeat = 0;
    peer->work_units_assigned = 0;
    peer->work_units_completed = 0;
    peer->keys_processed = 0;

    coordinator->federation.peer_count++;

    printf(LOG_FEDERATION LOG_OK "Added peer: %s:%d (index %d)\n",
           peer->host, peer->port, idx);

    return idx;
}

/* Internal: Connect to a federation peer */
static int federation_connect_peer(dist_federation_peer_t *peer) {
    if (!peer || peer->connected) return 0;

    struct hostent *server = gethostbyname(peer->host);
    if (!server) {
        printf(LOG_FEDERATION LOG_ERR "Cannot resolve %s\n", peer->host);
        return -1;
    }

    peer->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (peer->socket_fd < 0) {
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr.s_addr, server->h_addr, (size_t)server->h_length);
    addr.sin_port = htons((uint16_t)peer->port);

    /* Set connection timeout */
    set_socket_timeout(peer->socket_fd, 10);

    if (connect(peer->socket_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(peer->socket_fd);
        peer->socket_fd = -1;
        return -1;
    }

    peer->connected = true;
    peer->last_heartbeat = time_ms();

    printf(LOG_FEDERATION LOG_OK "Connected to peer %s:%d\n", peer->host, peer->port);
    return 0;
}

int dist_federation_connect(dist_coordinator_t *coordinator) {
    if (!coordinator) return -1;

    if (coordinator->federation.role != FEDERATION_SECONDARY) {
        printf(LOG_FEDERATION LOG_ERR "Only secondary can connect to primary\n");
        return -1;
    }

    /* Create a peer entry for the primary */
    if (coordinator->federation.peer_count == 0) {
        dist_federation_peer_t *primary = &coordinator->federation.peers[0];
        strncpy(primary->host, coordinator->federation.primary_host, sizeof(primary->host) - 1);
        primary->port = coordinator->federation.primary_port;
        primary->is_primary = true;
        primary->socket_fd = -1;
        primary->connected = false;
        coordinator->federation.peer_count = 1;
    }

    dist_federation_peer_t *primary = &coordinator->federation.peers[0];

    if (federation_connect_peer(primary) != 0) {
        printf(LOG_FEDERATION LOG_ERR "Failed to connect to primary %s:%d\n",
               primary->host, primary->port);
        return -1;
    }

    /* Send registration message */
    char msg[DIST_MAX_MSG_SIZE];
    snprintf(msg, sizeof(msg),
             "{\"type\":\"federation_register\","
             "\"role\":\"secondary\","
             "\"port\":%d,"
             "\"auth_token\":\"%s\"}",
             coordinator->port,
             coordinator->auth_token);

    if (send_msg(primary->socket_fd, msg) != 0) {
        printf(LOG_FEDERATION LOG_ERR "Failed to send registration\n");
        close(primary->socket_fd);
        primary->socket_fd = -1;
        primary->connected = false;
        return -1;
    }

    /* Wait for response with work range assignment */
    char response[DIST_MAX_MSG_SIZE];
    if (recv_msg(primary->socket_fd, response, sizeof(response)) <= 0) {
        printf(LOG_FEDERATION LOG_ERR "No response from primary\n");
        close(primary->socket_fd);
        primary->socket_fd = -1;
        primary->connected = false;
        return -1;
    }

    /* Parse assigned range */
    char resp_type[32] = {0};
    json_get_string(response, "type", resp_type, sizeof(resp_type));

    if (strcmp(resp_type, "federation_welcome") == 0) {
        json_get_string(response, "range_start",
                       coordinator->federation.assigned_range_start,
                       sizeof(coordinator->federation.assigned_range_start));
        json_get_string(response, "range_end",
                       coordinator->federation.assigned_range_end,
                       sizeof(coordinator->federation.assigned_range_end));

        printf(LOG_FEDERATION LOG_OK "Received range assignment:\n");
        printf(LOG_FEDERATION LOG_INFO "  Start: %s\n",
               coordinator->federation.assigned_range_start);
        printf(LOG_FEDERATION LOG_INFO "  End:   %s\n",
               coordinator->federation.assigned_range_end);

        return 0;
    } else if (strcmp(resp_type, "federation_rejected") == 0) {
        char reason[256] = {0};
        json_get_string(response, "reason", reason, sizeof(reason));
        printf(LOG_FEDERATION LOG_ERR "Registration rejected: %s\n", reason);
        close(primary->socket_fd);
        primary->socket_fd = -1;
        primary->connected = false;
        return -1;
    }

    return -1;
}

int dist_federation_process(dist_coordinator_t *coordinator, int timeout_ms) {
    (void)timeout_ms;  /* Reserved for future use */

    if (!coordinator) return -1;

    if (coordinator->federation.role == FEDERATION_STANDALONE) {
        return 0;  /* Nothing to do in standalone mode */
    }

    uint64_t now = time_ms();

    /* Check peer heartbeats and reconnect if needed */
    for (int i = 0; i < coordinator->federation.peer_count; i++) {
        dist_federation_peer_t *peer = &coordinator->federation.peers[i];

        if (!peer->connected) {
            /* Try to reconnect */
            if (now - peer->last_heartbeat > 30000) {  /* 30s between reconnect attempts */
                federation_connect_peer(peer);
            }
            continue;
        }

        /* Check for timeout */
        if (now - peer->last_heartbeat > 60000) {  /* 60s timeout */
            printf(LOG_FEDERATION LOG_WARN "Peer %s:%d timed out\n",
                   peer->host, peer->port);
            close(peer->socket_fd);
            peer->socket_fd = -1;
            peer->connected = false;
        }
    }

    /* Send periodic sync (every sync_interval) */
    if (now - coordinator->federation.last_sync_time >
        (uint64_t)coordinator->federation.sync_interval_sec * 1000) {

        coordinator->federation.last_sync_time = now;

        /* Send progress update to all peers */
        char msg[DIST_MAX_MSG_SIZE];
        snprintf(msg, sizeof(msg),
                 "{\"type\":\"federation_sync\","
                 "\"work_completed\":%d,"
                 "\"keys_processed\":%llu,"
                 "\"result_count\":%d}",
                 coordinator->work_units_completed,
                 (unsigned long long)coordinator->keys_processed,
                 coordinator->result_count);

        for (int i = 0; i < coordinator->federation.peer_count; i++) {
            dist_federation_peer_t *peer = &coordinator->federation.peers[i];
            if (peer->connected && peer->socket_fd >= 0) {
                send_msg(peer->socket_fd, msg);
            }
        }
    }

    return 0;
}

int dist_federation_share_result(dist_coordinator_t *coordinator,
                                 const char *private_key, const char *address) {
    if (!coordinator || !private_key || !address) return -1;

    if (coordinator->federation.role == FEDERATION_STANDALONE) {
        return 0;  /* Nothing to share in standalone mode */
    }

    char msg[DIST_MAX_MSG_SIZE];
    snprintf(msg, sizeof(msg),
             "{\"type\":\"federation_found\","
             "\"private_key\":\"%s\","
             "\"address\":\"%s\"}",
             private_key, address);

    int shared = 0;
    for (int i = 0; i < coordinator->federation.peer_count; i++) {
        dist_federation_peer_t *peer = &coordinator->federation.peers[i];
        if (peer->connected && peer->socket_fd >= 0) {
            if (send_msg(peer->socket_fd, msg) == 0) {
                shared++;
                printf(LOG_FEDERATION LOG_OK "Shared result with %s:%d\n",
                       peer->host, peer->port);
            }
        }
    }

    coordinator->federation.results_shared = (shared > 0);
    return shared > 0 ? 0 : -1;
}

void dist_federation_stats(const dist_coordinator_t *coordinator,
                           int *total_peers, int *total_units, int *total_completed) {
    if (!coordinator) return;

    int peers = 0;
    int units = coordinator->work_unit_count;
    int completed = coordinator->work_units_completed;

    for (int i = 0; i < coordinator->federation.peer_count; i++) {
        if (coordinator->federation.peers[i].connected) {
            peers++;
            completed += coordinator->federation.peers[i].work_units_completed;
        }
    }

    if (total_peers) *total_peers = peers;
    if (total_units) *total_units = units;
    if (total_completed) *total_completed = completed;
}

void dist_federation_shutdown(dist_coordinator_t *coordinator) {
    if (!coordinator) return;

    printf(LOG_FEDERATION LOG_INFO "Shutting down federation connections...\n");

    for (int i = 0; i < coordinator->federation.peer_count; i++) {
        dist_federation_peer_t *peer = &coordinator->federation.peers[i];
        if (peer->socket_fd >= 0) {
            /* Send goodbye message */
            send_msg(peer->socket_fd, "{\"type\":\"federation_goodbye\"}");
            close(peer->socket_fd);
            peer->socket_fd = -1;
            peer->connected = false;
        }
    }

    coordinator->federation.peer_count = 0;
    coordinator->federation.role = FEDERATION_STANDALONE;
}

/* ============================================================================
 * Security Configuration Functions
 * ============================================================================ */

void dist_coordinator_set_bind_address(dist_coordinator_t *coordinator,
                                       const char *address) {
    if (!coordinator) return;

    if (address && address[0] != '\0') {
        strncpy(coordinator->bind_address, address, sizeof(coordinator->bind_address) - 1);
        coordinator->bind_address[sizeof(coordinator->bind_address) - 1] = '\0';
    } else {
        coordinator->bind_address[0] = '\0';  /* Bind to all interfaces */
    }
}

void dist_coordinator_enable_rate_limiting(dist_coordinator_t *coordinator,
                                           int max_connections,
                                           int max_messages,
                                           int window_sec) {
    if (!coordinator) return;

    rate_limiter_t *rl = &coordinator->rate_limiter;

    rl->max_connections_per_window = max_connections > 0 ? max_connections : DIST_RATE_LIMIT_MAX_CONNECTIONS;
    rl->max_messages_per_window = max_messages > 0 ? max_messages : DIST_RATE_LIMIT_MAX_MESSAGES;
    rl->window_sec = window_sec > 0 ? window_sec : DIST_RATE_LIMIT_WINDOW_SEC;
    rl->enabled = true;

    printf(LOG_SERVER LOG_OK "Rate limiting configured: %d connections/%ds, %d messages/%ds\n",
           rl->max_connections_per_window, rl->window_sec,
           rl->max_messages_per_window, rl->window_sec);
}

int dist_coordinator_enable_tls(dist_coordinator_t *coordinator,
                                const char *cert_file,
                                const char *key_file) {
    if (!coordinator || !cert_file || !key_file) return -1;

#ifdef HAVE_OPENSSL
    /* Verify files exist and are readable */
    if (access(cert_file, R_OK) != 0) {
        printf(LOG_SERVER LOG_ERR "TLS certificate file not found: %s\n", cert_file);
        return -1;
    }
    if (access(key_file, R_OK) != 0) {
        printf(LOG_SERVER LOG_ERR "TLS key file not found: %s\n", key_file);
        return -1;
    }

    /* Create SSL context */
    SSL_CTX *ctx = tls_create_server_context(cert_file, key_file);
    if (!ctx) {
        printf(LOG_SERVER LOG_ERR "Failed to create TLS context\n");
        return -1;
    }

    /* Clean up existing context if any */
    if (coordinator->ssl_ctx) {
        SSL_CTX_free(coordinator->ssl_ctx);
    }

    coordinator->ssl_ctx = ctx;
    strncpy(coordinator->tls_cert_file, cert_file, sizeof(coordinator->tls_cert_file) - 1);
    strncpy(coordinator->tls_key_file, key_file, sizeof(coordinator->tls_key_file) - 1);
    coordinator->tls_enabled = true;

    printf(LOG_SERVER LOG_OK "TLS enabled with certificate: %s\n", cert_file);
    printf(LOG_SERVER LOG_INFO "TLS version: TLS 1.2+\n");
    return 0;
#else
    printf(LOG_SERVER LOG_ERR "TLS support not compiled in (requires OpenSSL)\n");
    printf(LOG_SERVER LOG_INFO "Rebuild with: make ENABLE_TLS=1\n");
    (void)cert_file;
    (void)key_file;
    return -1;
#endif
}
