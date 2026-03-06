/*
 * profiling.cpp - Performance profiling infrastructure extracted from keyhunt.cpp
 *
 * Defines g_profile_enabled, tls_prof, and profiling functions.
 */

#include "profiling.h"
#include "../platform/platform_time.h"
#include "../error/enhanced_error.h"
#include "../output.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * Global State
 * ============================================================================ */

bool g_profile_enabled = false;

thread_local profile_counters_t *tls_prof = NULL;

static profile_counters_t *g_profile_counters = NULL;
static int g_profile_thread_count = 0;
static profile_counters_t g_profile_prev_agg;

/* ============================================================================
 * Profiling Functions
 * ============================================================================ */

void profile_init_threads(int nthreads) {
	if (!g_profile_enabled || nthreads <= 0 || g_profile_counters) return;
	g_profile_counters = (profile_counters_t *)calloc((size_t)nthreads, sizeof(profile_counters_t));
	if (!g_profile_counters) {
		error_report_t report;
		size_t required_bytes = (size_t)nthreads * sizeof(profile_counters_t);
		error_context_t ctx = ERROR_CONTEXT_VALUES(
			ERROR_CAT_MEMORY,
			ERROR_SEV_WARNING,
			"profiling allocation",
			"Failed to allocate memory for profiling counters",
			required_bytes / (1024 * 1024),  // Convert to MB
			0  // We don't have available memory here
		);
		error_report(&ctx, &report);
		error_print(&report);
		output_warning("Profiling disabled due to memory allocation failure\n");
		g_profile_enabled = false;
		return;
	}
	g_profile_thread_count = nthreads;
	memset(&g_profile_prev_agg, 0, sizeof(g_profile_prev_agg));
}

void profile_set_thread(int idx) {
	if (!g_profile_enabled || !g_profile_counters || idx < 0 || idx >= g_profile_thread_count) {
		tls_prof = NULL;
		return;
	}
	tls_prof = &g_profile_counters[idx];
}

void profile_aggregate(profile_counters_t *out) {
	memset(out, 0, sizeof(*out));
	if (!g_profile_enabled || !g_profile_counters) return;
	for (int i = 0; i < g_profile_thread_count; i++) {
		out->ns_ec += g_profile_counters[i].ns_ec;
		out->ns_hash += g_profile_counters[i].ns_hash;
		out->ns_bloom += g_profile_counters[i].ns_bloom;
		out->ns_binsearch += g_profile_counters[i].ns_binsearch;
		out->ns_write += g_profile_counters[i].ns_write;
		out->keys += g_profile_counters[i].keys;
	}
}

void append_profile_info(char *buffer, size_t bufferSize) {
	if (!g_profile_enabled || !g_profile_counters || bufferSize < 4) return;

	profile_counters_t cur;
	profile_aggregate(&cur);

	profile_counters_t delta;
	delta.ns_ec = cur.ns_ec - g_profile_prev_agg.ns_ec;
	delta.ns_hash = cur.ns_hash - g_profile_prev_agg.ns_hash;
	delta.ns_bloom = cur.ns_bloom - g_profile_prev_agg.ns_bloom;
	delta.ns_binsearch = cur.ns_binsearch - g_profile_prev_agg.ns_binsearch;
	delta.ns_write = cur.ns_write - g_profile_prev_agg.ns_write;
	delta.keys = cur.keys - g_profile_prev_agg.keys;
	g_profile_prev_agg = cur;

	const uint64_t total_ns = delta.ns_ec + delta.ns_hash + delta.ns_bloom + delta.ns_binsearch + delta.ns_write;
	if (delta.keys == 0 || total_ns == 0) return;

	const unsigned ec_pct = (unsigned)((delta.ns_ec * 100ULL) / total_ns);
	const unsigned hash_pct = (unsigned)((delta.ns_hash * 100ULL) / total_ns);
	const unsigned bloom_pct = (unsigned)((delta.ns_bloom * 100ULL) / total_ns);
	const unsigned bin_pct = (unsigned)((delta.ns_binsearch * 100ULL) / total_ns);
	const unsigned write_pct = (unsigned)((delta.ns_write * 100ULL) / total_ns);
	const uint64_t ns_per_key = total_ns / delta.keys;

	char addition[256];
	snprintf(addition, sizeof(addition),
	         " | prof %luns/key EC%u Hash%u Bloom%u Bin%u Write%u",
	         (unsigned long)ns_per_key, ec_pct, hash_pct, bloom_pct, bin_pct, write_pct);

	size_t len = strlen(buffer);
	char tail = 0;
	if (len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
		tail = buffer[len - 1];
		buffer[len - 1] = '\0';
		len--;
	}
	size_t remaining = (len < bufferSize) ? bufferSize - len : 0;
	if (remaining > 1) {
		strncat(buffer, addition, remaining - 1);
		len = strlen(buffer);
	}
	if (tail != 0 && len + 1 < bufferSize) {
		buffer[len] = tail;
		buffer[len + 1] = '\0';
	}
}
