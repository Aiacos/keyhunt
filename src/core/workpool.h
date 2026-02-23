/*
 * workpool.h - Work-stealing pool for GPU/CPU collaboration
 *
 * Extracted from keyhunt.cpp to allow shared access from search modules.
 * Both GPU and CPU threads pull work blocks from the same pool for dynamic
 * load balancing. The faster processor (GPU) naturally gets more work.
 */

#ifndef WORKPOOL_H
#define WORKPOOL_H

#include <atomic>
#include <cstdint>
#include <cstdio>
#include "../secp256k1/Int.h"

struct WorkPool {
	std::atomic<uint64_t> next_block;      // Next available block index
	uint64_t block_size;                    // Keys per block (immutable after init)
	Int range_base;                         // Starting point of range (IMMUTABLE after init)
	Int range_end;                          // End of range (IMMUTABLE after init)
	std::atomic<bool> enabled;              // Work pool is active
	std::atomic<bool> exhausted;            // All work has been taken
	std::atomic<bool> initialized;          // Set true after init() completes (barrier for readers)

	WorkPool() : next_block(0), block_size(0), enabled(false), exhausted(false), initialized(false) {}

	// Initialize work pool with a range
	// NOTE: range_base, range_end, and block_size are IMMUTABLE after init()
	void init(Int *start, Int *end, uint64_t blk_size) {
		range_base.Set(start);
		range_end.Set(end);
		block_size = blk_size;

		next_block.store(0, std::memory_order_release);
		exhausted.store(false, std::memory_order_release);
		enabled.store(true, std::memory_order_release);
		initialized.store(true, std::memory_order_release);
	}

	// Get next work block (thread-safe, lock-free)
	bool get_block(Int &start_out, Int &end_out) {
		if (!initialized.load(std::memory_order_acquire)) return false;
		if (!enabled.load(std::memory_order_acquire) ||
		    exhausted.load(std::memory_order_acquire)) return false;

		uint64_t block_idx = next_block.fetch_add(1, std::memory_order_acq_rel);

		char tmp[32];
		Int offset;
		snprintf(tmp, sizeof(tmp), "%llu", (unsigned long long)block_size);
		offset.SetBase10(tmp);
		Int mult;
		snprintf(tmp, sizeof(tmp), "%llu", (unsigned long long)block_idx);
		mult.SetBase10(tmp);
		offset.Mult(&mult);

		start_out.Set(&range_base);
		start_out.Add(&offset);
		if (!start_out.IsLower(&range_end)) {
			exhausted.store(true, std::memory_order_release);
			return false;
		}

		end_out.Set(&start_out);
		Int blk;
		snprintf(tmp, sizeof(tmp), "%llu", (unsigned long long)block_size);
		blk.SetBase10(tmp);
		end_out.Add(&blk);

		if (end_out.IsGreater(&range_end)) {
			end_out.Set(&range_end);
		}

		return true;
	}

	// Check if pool is exhausted
	bool is_exhausted() const {
		return exhausted.load(std::memory_order_acquire);
	}

	// Disable the pool
	void disable() {
		enabled.store(false, std::memory_order_release);
		exhausted.store(true, std::memory_order_release);
	}
};

#endif /* WORKPOOL_H */
