/*
 * fuzz_json.cpp - Fuzzing harness for JSON parsing functions
 *
 * This fuzzer tests the JSON parsing functions used in distributed.c
 * for robustness against malformed inputs.
 *
 * Build with libFuzzer:
 *   clang++ -g -O1 -fno-omit-frame-pointer -fsanitize=fuzzer,address \
 *           tests/fuzz_json.cpp -o fuzz_json
 *
 * Build with AFL:
 *   afl-g++ -g -O1 tests/fuzz_json.cpp -o fuzz_json_afl
 *
 * Run:
 *   ./fuzz_json corpus/           # libFuzzer
 *   afl-fuzz -i seeds/ -o out/ ./fuzz_json_afl @@   # AFL
 */

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>

/* ============================================================================
 * JSON Parsing Functions (copied from distributed.c for fuzzing)
 * These are the exact same implementations to ensure we test the real code
 * ============================================================================ */

static void json_add_string(char *buf, size_t sz, const char *key, const char *val) {
    size_t current_len = strlen(buf);
    if (current_len >= sz - 1) return;
    size_t remaining = sz - current_len;
    int written = snprintf(buf + current_len, remaining, "\"%s\":\"%s\",", key, val);
    if (written < 0 || (size_t)written >= remaining) {
        buf[sz - 1] = '\0';
    }
}

static void json_add_int(char *buf, size_t sz, const char *key, int64_t val) {
    size_t current_len = strlen(buf);
    if (current_len >= sz - 1) return;
    size_t remaining = sz - current_len;
    int written = snprintf(buf + current_len, remaining, "\"%s\":%lld,", key, (long long)val);
    if (written < 0 || (size_t)written >= remaining) {
        buf[sz - 1] = '\0';
    }
}

static void json_add_double(char *buf, size_t sz, const char *key, double val) {
    size_t current_len = strlen(buf);
    if (current_len >= sz - 1) return;
    size_t remaining = sz - current_len;
    int written = snprintf(buf + current_len, remaining, "\"%s\":%.3f,", key, val);
    if (written < 0 || (size_t)written >= remaining) {
        buf[sz - 1] = '\0';
    }
}

static int json_get_string(const char *json, const char *key, char *out, size_t outsz) {
    if (!json || !key || !out || outsz == 0) return -1;
    out[0] = '\0';

    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    const char *start = strstr(json, pattern);
    if (!start) return -1;
    start += strlen(pattern);
    const char *end = strchr(start, '"');
    if (!end || end < start) return -1;
    size_t len = (size_t)(end - start);
    if (len >= outsz) len = outsz - 1;
    memcpy(out, start, len);
    out[len] = '\0';
    return 0;
}

static int64_t json_get_int(const char *json, const char *key) {
    if (!json || !key) return 0;

    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *start = strstr(json, pattern);
    if (!start) return 0;
    start += strlen(pattern);
    while (*start == ' ' || *start == '\t') start++;
    return strtoll(start, NULL, 10);
}

static double json_get_double(const char *json, const char *key) {
    if (!json || !key) return 0.0;

    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *start = strstr(json, pattern);
    if (!start) return 0.0;
    start += strlen(pattern);
    while (*start == ' ' || *start == '\t') start++;
    return strtod(start, NULL);
}

/* sanitize_json_string - from distributed.c */
static void sanitize_json_string(char *str, size_t max_len) {
    if (!str || max_len == 0) return;

    size_t len = strnlen(str, max_len);

    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        /* Allow printable ASCII and common whitespace */
        if ((c >= 32 && c <= 126) || c == '\t' || c == '\n' || c == '\r') {
            continue;
        }
        /* Replace with space for safety */
        str[i] = ' ';
    }
    str[len < max_len ? len : max_len - 1] = '\0';
}

/* ============================================================================
 * Fuzzing Entry Point
 * ============================================================================ */

#ifdef __AFL_COMPILER
/* AFL mode - read from file or stdin */
int main(int argc, char **argv) {
    FILE *f = (argc > 1) ? fopen(argv[1], "rb") : stdin;
    if (!f) return 1;

    char buf[4096];
    size_t len = fread(buf, 1, sizeof(buf) - 1, f);
    if (f != stdin) fclose(f);
    buf[len] = '\0';

    /* Test all JSON functions */
    char out[256];
    sanitize_json_string(buf, sizeof(buf));
    json_get_string(buf, "type", out, sizeof(out));
    json_get_string(buf, "hostname", out, sizeof(out));
    json_get_string(buf, "auth_token", out, sizeof(out));
    json_get_string(buf, "private_key", out, sizeof(out));
    json_get_int(buf, "work_id");
    json_get_int(buf, "keys_processed");
    json_get_double(buf, "perf_score");

    /* Test json_add functions with fuzz data */
    char build[512];
    build[0] = '\0';
    json_add_string(build, sizeof(build), "test", out);
    json_add_int(build, sizeof(build), "num", json_get_int(buf, "work_id"));
    json_add_double(build, sizeof(build), "score", json_get_double(buf, "perf_score"));

    return 0;
}

#else
/* libFuzzer mode */
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    /* Limit size to prevent excessive memory usage */
    if (size > 8192) return 0;

    /* Create null-terminated copy of input */
    char *buf = (char *)malloc(size + 1);
    if (!buf) return 0;
    memcpy(buf, data, size);
    buf[size] = '\0';

    /* Test sanitize_json_string */
    char sanitized[4096];
    size_t copy_len = size < sizeof(sanitized) - 1 ? size : sizeof(sanitized) - 1;
    memcpy(sanitized, buf, copy_len);
    sanitized[copy_len] = '\0';
    sanitize_json_string(sanitized, sizeof(sanitized));

    /* Test json_get_string with various keys */
    char out[256];
    json_get_string(buf, "type", out, sizeof(out));
    json_get_string(buf, "hostname", out, sizeof(out));
    json_get_string(buf, "auth_token", out, sizeof(out));
    json_get_string(buf, "private_key", out, sizeof(out));
    json_get_string(buf, "address", out, sizeof(out));
    json_get_string(buf, "range_start", out, sizeof(out));
    json_get_string(buf, "range_end", out, sizeof(out));

    /* Test with empty key */
    json_get_string(buf, "", out, sizeof(out));

    /* Test with very long key */
    char long_key[300];
    memset(long_key, 'a', sizeof(long_key) - 1);
    long_key[sizeof(long_key) - 1] = '\0';
    json_get_string(buf, long_key, out, sizeof(out));

    /* Test with small output buffer */
    char tiny_out[2];
    json_get_string(buf, "type", tiny_out, sizeof(tiny_out));

    /* Test json_get_int */
    (void)json_get_int(buf, "work_id");
    (void)json_get_int(buf, "keys_processed");
    (void)json_get_int(buf, "worker_id");
    (void)json_get_int(buf, "elapsed_ms");
    (void)json_get_int(buf, "");
    (void)json_get_int(buf, long_key);

    /* Test json_get_double */
    (void)json_get_double(buf, "perf_score");
    (void)json_get_double(buf, "throughput");
    (void)json_get_double(buf, "");
    (void)json_get_double(buf, long_key);

    /* Test json_add functions */
    char build[1024];
    build[0] = '\0';
    json_add_string(build, sizeof(build), "key", out);
    json_add_int(build, sizeof(build), "num", 12345);
    json_add_double(build, sizeof(build), "score", 3.14159);

    /* Test with small build buffer */
    char tiny_build[16];
    tiny_build[0] = '\0';
    json_add_string(tiny_build, sizeof(tiny_build), "k", "v");
    json_add_int(tiny_build, sizeof(tiny_build), "n", 1);

    /* Test with NULL inputs */
    json_get_string(NULL, "type", out, sizeof(out));
    json_get_string(buf, NULL, out, sizeof(out));
    json_get_string(buf, "type", NULL, sizeof(out));
    json_get_string(buf, "type", out, 0);
    (void)json_get_int(NULL, "type");
    (void)json_get_int(buf, NULL);
    (void)json_get_double(NULL, "type");
    (void)json_get_double(buf, NULL);
    sanitize_json_string(NULL, 10);

    free(buf);
    return 0;
}
#endif
