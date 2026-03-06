/*
 * bsgs_globals.h - BSGS-owned global state declarations
 *
 * This header declares all BSGS-specific global variables. These variables
 * are DEFINED (owned) in mode_bsgs.cpp and accessed via extern from any
 * other translation unit that needs them (e.g., keyhunt.cpp for atexit cleanup,
 * search_bsgs_threads.cpp for bPload threads).
 *
 * BSGS-specific state: bloom filters, bP tables, Int/Point BSGS parameters,
 * checksum buffers, bloom mutexes, target points, etc.
 *
 * Shared state (secp, N, addressTable, FLAG*, range vars, counters) is NOT
 * declared here -- those remain in keyhunt.cpp.
 *
 * Created during Phase 4, Plan 07: Gap closure for BSGS extern elimination.
 */

#ifndef BSGS_GLOBALS_H
#define BSGS_GLOBALS_H

#include "../secp256k1/Int.h"
#include "../secp256k1/Point.h"
#include "../bloom/bloom.h"
#include "../bloom/bloom_wrapper.h"
#include "../platform/platform.h"

#include <atomic>
#include <vector>
#include <cstdint>

/* Forward declaration */
struct bsgs_xvalue;

/* ============================================================================
 * Bloom checksum structure (used for cache file integrity)
 * Also defined in search/search_context.h -- guarded to avoid redefinition.
 * ============================================================================ */
#ifndef CHECKSUMSHA256_DEFINED
#define CHECKSUMSHA256_DEFINED
struct checksumsha256 {
    char data[32];
    char backup[32];
};
#endif

/* ============================================================================
 * BSGS Int parameters
 * ============================================================================ */
extern Int BSGS_GROUP_SIZE;
extern Int BSGS_CURRENT;
extern Int BSGS_R;
extern Int BSGS_AUX;
extern Int BSGS_N;
extern Int BSGS_N_double;
extern Int BSGS_M;
extern Int BSGS_M_double;
extern Int BSGS_M2;
extern Int BSGS_M2_double;
extern Int BSGS_M3;
extern Int BSGS_M3_double;

/* ============================================================================
 * BSGS Point parameters
 * ============================================================================ */
extern Point BSGS_P;
extern Point BSGS_MP;
extern Point BSGS_MP2;
extern Point BSGS_MP3;
extern Point BSGS_MP_double;
extern Point BSGS_MP2_double;
extern Point BSGS_MP3_double;

/* ============================================================================
 * BSGS vectors
 * ============================================================================ */
extern std::vector<Point> BSGS_AMP2;
extern std::vector<Point> BSGS_AMP3;
extern std::vector<Point> GSn;
extern Point _2GSn;

/* ============================================================================
 * Target points (BSGS-specific)
 * ============================================================================ */
extern std::vector<Point> OriginalPointsBSGS;
extern bool *OriginalPointsBSGScompressed;
extern std::atomic<int> *bsgs_found;

/* ============================================================================
 * BSGS bloom filters and bP table
 * ============================================================================ */
extern bloom_extended_t *bloom_bP;
extern bloom_extended_t *bloom_bPx2nd;
extern bloom_extended_t *bloom_bPx3rd;
extern struct bsgs_xvalue *bPtable;

/* ============================================================================
 * Bloom checksums
 * ============================================================================ */
extern struct checksumsha256 *bloom_bP_checksums;
extern struct checksumsha256 *bloom_bPx2nd_checksums;
extern struct checksumsha256 *bloom_bPx3rd_checksums;

/* ============================================================================
 * Bloom mutexes
 * ============================================================================ */
extern platform_mutex_t *bloom_bP_mutex;
extern platform_mutex_t *bloom_bPx2nd_mutex;
extern platform_mutex_t *bloom_bPx3rd_mutex;

/* ============================================================================
 * BSGS scalar parameters
 * ============================================================================ */
extern uint64_t BSGS_XVALUE_RAM;
extern uint64_t BSGS_BUFFERXPOINTLENGTH;
extern uint64_t BSGS_BUFFERREGISTERLENGTH;
extern uint64_t bloom_bP_totalbytes;
extern uint64_t bloom_bP2_totalbytes;
extern uint64_t bloom_bP3_totalbytes;
extern uint64_t bsgs_m;
extern uint64_t bsgs_m2;
extern uint64_t bsgs_m3;
extern uint64_t bsgs_aux;
extern uint32_t bsgs_point_number;

/* ============================================================================
 * BSGS file I/O state
 * ============================================================================ */
extern uint64_t bytes;
extern char checksum[32];
extern char checksum_backup[32];
extern char buffer_bloom_file[1024];

/* ============================================================================
 * BSGS mode names
 * ============================================================================ */
extern const char *bsgs_modes[5];

/* ============================================================================
 * BSGS cleanup function (called via atexit or mode_bsgs_cleanup)
 * ============================================================================ */
void cleanup_bsgs_resources(void);

#endif /* BSGS_GLOBALS_H */
