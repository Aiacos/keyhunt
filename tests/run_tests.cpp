/*
 * run_tests.cpp - Main test runner for all keyhunt unit tests
 *
 * This file serves as the entry point for running all unit tests.
 * Individual test files can also be run separately.
 *
 * Usage:
 *   ./run_tests              # Run all tests
 *   ./run_tests int          # Run only Int tests
 *   ./run_tests hash         # Run only hash/crypto tests
 *   ./run_tests bloom        # Run only Bloom filter tests
 *   ./run_tests bsgs_ops     # Run only BSGS operations tests
 *   ./run_tests bsgs         # Run only BSGS tests
 *   ./run_tests bsgs_sort    # Run only BSGS sort tests
 *   ./run_tests gpu          # Run only GPU backend tests
 *   ./run_tests multi_gpu    # Run only multi-GPU integration tests
 *   ./run_tests distributed  # Run only distributed mode tests
 *   ./run_tests wizard       # Run only wizard tests
 *   ./run_tests point        # Run only Point tests
 *   ./run_tests intgroup     # Run only IntGroup tests
 *   ./run_tests sha512       # Run only SHA512 SIMD tests
 *   ./run_tests search_xpoint # Run only XPOINT search mode tests
 *   ./run_tests search_rmd160 # Run only RMD160 search mode tests
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declarations for test modules (C++ linkage) */
int run_int_tests(void);
int run_hash_tests(void);
int run_bloom_tests(void);
int run_bsgs_ops_tests(void);
int run_bsgs_tests(void);
int run_bsgs_sort_tests(void);
int run_gpu_backend_tests(void);
int run_multi_gpu_integration_tests(void);
int run_distributed_tests(void);
int run_wizard_tests(void);
int run_point_tests(void);
int run_intgroup_tests(void);
int run_sha512_simd_tests(void);
int run_sha256_simd_tests(void);
int run_search_xpoint_tests(void);
int run_search_rmd160_tests(void);
int run_fused_hash_tests(void);

/* Color codes */
#define CLR_CYAN    "\033[36m"
#define CLR_GREEN   "\033[32m"
#define CLR_RED     "\033[31m"
#define CLR_BOLD    "\033[1m"
#define CLR_RESET   "\033[0m"

static void print_banner(void) {
    printf("\n");
    printf(CLR_CYAN "╔═══════════════════════════════════════════════════════════════╗" CLR_RESET "\n");
    printf(CLR_CYAN "║" CLR_RESET CLR_BOLD "           KEYHUNT UNIT TEST SUITE                            " CLR_RESET CLR_CYAN "║" CLR_RESET "\n");
    printf(CLR_CYAN "╚═══════════════════════════════════════════════════════════════╝" CLR_RESET "\n");
    printf("\n");
}

static void print_usage(const char *prog) {
    printf("Usage: %s [module]\n", prog);
    printf("\n");
    printf("Modules:\n");
    printf("  (none)       Run all tests\n");
    printf("  int          Run Int (256-bit integer) tests\n");
    printf("  hash         Run hash and cryptographic function tests\n");
    printf("  bloom        Run Bloom filter tests\n");
    printf("  bsgs_ops     Run BSGS operations tests\n");
    printf("  bsgs         Run BSGS integration tests\n");
    printf("  bsgs_sort    Run BSGS sort and search tests\n");
    printf("  gpu          Run GPU backend tests\n");
    printf("  multi_gpu    Run multi-GPU integration tests\n");
    printf("  distributed  Run distributed mode tests\n");
    printf("  wizard       Run wizard tests\n");
    printf("  point        Run Point operation tests\n");
    printf("  intgroup     Run IntGroup batch inversion tests\n");
    printf("  sha512       Run SHA512 SIMD tests\n");
    printf("  sha256       Run SHA256 SIMD tests\n");
    printf("  search_xpoint Run XPOINT search mode tests\n");
    printf("  search_rmd160 Run RMD160 search mode tests\n");
    printf("  fused        Run fused hash pipeline tests\n");
    printf("  help         Show this help\n");
    printf("\n");
}

int main(int argc, char *argv[]) {
    print_banner();

    const char *module = NULL;
    if (argc > 1) {
        module = argv[1];

        if (strcmp(module, "help") == 0 || strcmp(module, "-h") == 0 ||
            strcmp(module, "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    int total_failures = 0;

    if (module == NULL || strcmp(module, "int") == 0) {
        printf(CLR_BOLD "\n>>> Running Int Tests\n" CLR_RESET);
        total_failures += run_int_tests();
    }

    if (module == NULL || strcmp(module, "hash") == 0) {
        printf(CLR_BOLD "\n>>> Running Hash Tests\n" CLR_RESET);
        total_failures += run_hash_tests();
    }

    if (module == NULL || strcmp(module, "bloom") == 0) {
        printf(CLR_BOLD "\n>>> Running Bloom Filter Tests\n" CLR_RESET);
        total_failures += run_bloom_tests();
    }

    if (module == NULL || strcmp(module, "bsgs_ops") == 0) {
        printf(CLR_BOLD "\n>>> Running BSGS Operations Tests\n" CLR_RESET);
        total_failures += run_bsgs_ops_tests();
    }

    if (module == NULL || strcmp(module, "bsgs") == 0) {
        printf(CLR_BOLD "\n>>> Running BSGS Integration Tests\n" CLR_RESET);
        total_failures += run_bsgs_tests();
    }

    if (module == NULL || strcmp(module, "bsgs_sort") == 0) {
        printf(CLR_BOLD "\n>>> Running BSGS Sort Tests\n" CLR_RESET);
        total_failures += run_bsgs_sort_tests();
    }

    if (module == NULL || strcmp(module, "gpu") == 0) {
        printf(CLR_BOLD "\n>>> Running GPU Backend Tests\n" CLR_RESET);
        total_failures += run_gpu_backend_tests();
    }

    if (module == NULL || strcmp(module, "gpu") == 0 || strcmp(module, "multi_gpu") == 0) {
        printf(CLR_BOLD "\n>>> Running Multi-GPU Integration Tests\n" CLR_RESET);
        total_failures += run_multi_gpu_integration_tests();
    }

    if (module == NULL || strcmp(module, "distributed") == 0) {
        printf(CLR_BOLD "\n>>> Running Distributed Mode Tests\n" CLR_RESET);
        total_failures += run_distributed_tests();
    }

    if (module == NULL || strcmp(module, "wizard") == 0) {
        printf(CLR_BOLD "\n>>> Running Wizard Tests\n" CLR_RESET);
        total_failures += run_wizard_tests();
    }

    if (module == NULL || strcmp(module, "point") == 0) {
        printf(CLR_BOLD "\n>>> Running Point Tests\n" CLR_RESET);
        total_failures += run_point_tests();
    }

    if (module == NULL || strcmp(module, "intgroup") == 0) {
        printf(CLR_BOLD "\n>>> Running IntGroup Tests\n" CLR_RESET);
        total_failures += run_intgroup_tests();
    }

    if (module == NULL || strcmp(module, "sha512") == 0) {
        printf(CLR_BOLD "\n>>> Running SHA512 SIMD Tests\n" CLR_RESET);
        total_failures += run_sha512_simd_tests();
    }

    if (module == NULL || strcmp(module, "sha256") == 0) {
        printf(CLR_BOLD "\n>>> Running SHA256 SIMD Tests\n" CLR_RESET);
        total_failures += run_sha256_simd_tests();
    }

    if (module == NULL || strcmp(module, "search_xpoint") == 0) {
        printf(CLR_BOLD "\n>>> Running XPOINT Search Mode Tests\n" CLR_RESET);
        total_failures += run_search_xpoint_tests();
    }

    if (module == NULL || strcmp(module, "search_rmd160") == 0) {
        printf(CLR_BOLD "\n>>> Running RMD160 Search Mode Tests\n" CLR_RESET);
        total_failures += run_search_rmd160_tests();
    }

    if (module == NULL || strcmp(module, "fused") == 0) {
        printf(CLR_BOLD "\n>>> Running Fused Hash Pipeline Tests\n" CLR_RESET);
        total_failures += run_fused_hash_tests();
    }

    /* Final summary */
    printf("\n");
    printf(CLR_CYAN "═══════════════════════════════════════════════════════════════" CLR_RESET "\n");
    if (total_failures == 0) {
        printf(CLR_GREEN CLR_BOLD "  ALL TESTS PASSED!" CLR_RESET "\n");
    } else {
        printf(CLR_RED CLR_BOLD "  %d MODULE(S) HAD FAILURES" CLR_RESET "\n", total_failures);
    }
    printf(CLR_CYAN "═══════════════════════════════════════════════════════════════" CLR_RESET "\n");
    printf("\n");

    return total_failures > 0 ? 1 : 0;
}
