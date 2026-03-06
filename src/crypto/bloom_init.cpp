/*
 * bloom_init.cpp - Bloom filter initialization and memory validation
 *
 * Extracted from keyhunt.cpp to modularize bloom filter setup.
 */

#include "bloom_init.h"

#include <cstdio>
#include <cinttypes>

#include "../output.h"
#include "../core/sysinfo.h"

/* External globals from keyhunt.cpp */
extern int FLAGBLOOMMULTIPLIER;
extern system_info_t g_sysinfo;

bool initBloomFilter(struct bloom *bloom_arg, uint64_t items_bloom) {
	bool r = true;
	output_success("Bloom filter for %" PRIu64 " elements.\n", items_bloom);
	if (items_bloom <= 10000) {
		if (bloom_init2(bloom_arg, 10000, 0.000001) == 1) {
			output_error("error bloom_init for 10000 elements.\n");
			r = false;
		}
	}
	else {
		if (bloom_init2(bloom_arg, FLAGBLOOMMULTIPLIER * items_bloom, 0.000001) == 1) {
			output_error("error bloom_init for %" PRIu64 " elements.\n", items_bloom);
			r = false;
		}
	}
	output_success("Loading data to the bloomfilter total: %.2f MB\n", (double)(((double)bloom_arg->bytes) / (double)1048576));

	/* Memory check: verify bloom filter fits in available RAM */
	if (r) {
		uint64_t bloom_mb = bloom_arg->bytes / (1024 * 1024);
		uint64_t available_ram_mb = g_sysinfo.ram_available;
		uint64_t safe_limit_mb = (available_ram_mb * 80) / 100; /* 80% safety margin */

		if (bloom_mb > safe_limit_mb) {
			fprintf(stderr, "\n");
			output_warning("========================================================\n");
			output_warning("INSUFFICIENT MEMORY FOR BLOOM FILTER\n");
			output_warning("========================================================\n");
			output_warning("Bloom filter: %" PRIu64 " MB (~%.1f GB)\n", bloom_mb, (double)bloom_mb / 1024);
			output_warning("Available:    %" PRIu64 " MB (~%.1f GB)\n", available_ram_mb, (double)available_ram_mb / 1024);
			output_warning("Safe limit:   %" PRIu64 " MB (80%% of available)\n", safe_limit_mb);
			output_warning("\n");
			output_warning("Current settings:\n");
			output_warning("  Items:      %" PRIu64 "\n", items_bloom);
			output_warning("  Multiplier: %d (-z parameter)\n", FLAGBLOOMMULTIPLIER);
			output_warning("  Total bloom elements: %" PRIu64 "\n", FLAGBLOOMMULTIPLIER * items_bloom);
			output_warning("\n");
			output_warning("SUGGESTIONS:\n");
			output_warning("--------------------------------------------------------\n");

			/* Calculate optimal multiplier that fits */
			int suggested_multiplier = (int)((double)safe_limit_mb * 1024.0 * 1024.0 / (double)items_bloom / 3.59);
			if (suggested_multiplier < 1) suggested_multiplier = 1;

			output_warning("Try reducing -z parameter to: %d\n", suggested_multiplier);
			output_warning("  Command: add -z %d to your command line\n", suggested_multiplier);
			output_warning("  This will use ~%" PRIu64 " MB\n",
				(uint64_t)(items_bloom * suggested_multiplier * 3.59 / 1024 / 1024));
			output_warning("\n");
			output_warning("Or reduce the number of items in your input file\n");
			output_warning("========================================================\n\n");

			/* Free the bloom filter we just allocated */
			bloom_free(bloom_arg);
			bloom_arg->bf = NULL;

			r = false;
		}
		else {
			/* Show memory usage info */
			double percent_used = (double)bloom_mb * 100.0 / (double)available_ram_mb;
			output_info("Memory check: %" PRIu64 " MB bloom filter, %" PRIu64 " MB available (%.1f%% used)\n",
				bloom_mb, available_ram_mb, percent_used);
		}
	}

	return r;
}

/*
 * Initialize bloom filter using the fast extended wrapper
 * This provides ~2x speedup on bloom lookups
 */
bool initBloomFilterExt(bloom_extended_t *bloom_arg, uint64_t items_bloom) {
	bool r = true;
	uint64_t effective_items = items_bloom <= 10000 ? 10000 : FLAGBLOOMMULTIPLIER * items_bloom;

	output_success("Bloom filter for %" PRIu64 " elements.\n", items_bloom);

	if (bloom_ext_init(bloom_arg, effective_items, 0.000001) != 0) {
		output_error("error bloom_init for %" PRIu64 " elements.\n", effective_items);
		return false;
	}

	uint64_t bloom_bytes = bloom_ext_bytes(bloom_arg);
	output_success("Loading data to the bloomfilter total: %.2f MB\n", (double)bloom_bytes / 1048576.0);

	if (bloom_ext_is_fast(bloom_arg)) {
		output_success("Using FAST bloom filter (XXH3 + bitmask optimization)\n");
	}

	/* Memory check */
	uint64_t bloom_mb = bloom_bytes / (1024 * 1024);
	uint64_t available_ram_mb = g_sysinfo.ram_available;
	uint64_t safe_limit_mb = (available_ram_mb * 80) / 100;

	if (bloom_mb > safe_limit_mb) {
		fprintf(stderr, "\n");
		output_warning("========================================================\n");
		output_warning("INSUFFICIENT MEMORY FOR BLOOM FILTER\n");
		output_warning("========================================================\n");
		output_warning("Bloom filter: %" PRIu64 " MB (~%.1f GB)\n", bloom_mb, (double)bloom_mb / 1024);
		output_warning("Available:    %" PRIu64 " MB (~%.1f GB)\n", available_ram_mb, (double)available_ram_mb / 1024);
		output_warning("Safe limit:   %" PRIu64 " MB (80%% of available)\n", safe_limit_mb);
		output_warning("\n");
		output_warning("Try reducing -z parameter or input file size\n");
		output_warning("========================================================\n\n");

		bloom_ext_free(bloom_arg);
		r = false;
	} else {
		double percent_used = (double)bloom_mb * 100.0 / (double)available_ram_mb;
		output_info("Memory check: %" PRIu64 " MB bloom filter, %" PRIu64 " MB available (%.1f%% used)\n",
			bloom_mb, available_ram_mb, percent_used);
	}

	return r;
}
