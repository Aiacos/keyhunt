# ===========================================================================
# keyhunt - CMake Build System
# ===========================================================================
# Modern CMake alternative to the existing Makefile.
# The Makefile is preserved as a fallback build system.
#
# Basic usage (Linux/macOS):
#   mkdir build && cd build
#   cmake ..
#   cmake --build . -j$(nproc)
#
# Windows usage (Visual Studio):
#   mkdir build && cd build
#   cmake .. -G "Visual Studio 17 2022" -A x64
#   cmake --build . --config Release
#
# Windows usage (MinGW-w64):
#   mkdir build && cd build
#   cmake .. -G "MinGW Makefiles"
#   cmake --build . -j%NUMBER_OF_PROCESSORS%
#
# Options:
#   cmake .. -DENABLE_TLS=ON          # Enable TLS (OpenSSL)
#   cmake .. -DENABLE_LTO=OFF         # Disable Link-Time Optimization
#   cmake .. -DCMAKE_BUILD_TYPE=Debug # Debug build
#
# CUDA (auto-detected):
#   cmake .. -DCMAKE_CUDA_ARCHITECTURES=75  # Override GPU arch
#
# Run tests:
#   cmake --build . --target run_tests
#   ctest
# ===========================================================================

cmake_minimum_required(VERSION 3.18)

project(keyhunt
    VERSION 0.1.0
    DESCRIPTION "High-performance cryptocurrency private key search tool"
    LANGUAGES C CXX
)

# ---------------------------------------------------------------------------
# Global Standards
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)   # gnu++17 to match Makefile
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# Default to Release if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(ENABLE_TLS "Enable TLS support (requires OpenSSL)" OFF)
option(ENABLE_LTO "Enable Link-Time Optimization" ON)

# ---------------------------------------------------------------------------
# Platform Detection
# ---------------------------------------------------------------------------
if(WIN32)
    message(STATUS "Platform: Windows")
    set(PLATFORM_WINDOWS ON)
else()
    message(STATUS "Platform: POSIX (Linux/macOS)")
    set(PLATFORM_WINDOWS OFF)
endif()

# ---------------------------------------------------------------------------
# Compiler Feature Detection (SIMD)
# ---------------------------------------------------------------------------
include(CheckCXXCompilerFlag)
include(CheckCCompilerFlag)

# GCC/Clang flags
if(NOT MSVC)
    check_cxx_compiler_flag("-mavx2" COMPILER_SUPPORTS_AVX2)
    check_cxx_compiler_flag("-mavx512f" COMPILER_SUPPORTS_AVX512F)
    check_cxx_compiler_flag("-mavx512dq" COMPILER_SUPPORTS_AVX512DQ)
    check_cxx_compiler_flag("-msha" COMPILER_SUPPORTS_SHA_NI)
    check_cxx_compiler_flag("-msse4.1" COMPILER_SUPPORTS_SSE41)
    check_cxx_compiler_flag("-mssse3" COMPILER_SUPPORTS_SSSE3)
    check_cxx_compiler_flag("-march=native" COMPILER_SUPPORTS_MARCH_NATIVE)
else()
    # MSVC flags - note: MSVC doesn't have per-instruction set flags like GCC
    # /arch:AVX2 enables AVX, AVX2, and FMA
    # /arch:AVX512 enables AVX-512 (requires VS 2017 15.7+)
    check_cxx_compiler_flag("/arch:AVX2" COMPILER_SUPPORTS_AVX2)
    check_cxx_compiler_flag("/arch:AVX512" COMPILER_SUPPORTS_AVX512F)
    # MSVC AVX2 implies AVX512DQ support when AVX512 is available
    set(COMPILER_SUPPORTS_AVX512DQ ${COMPILER_SUPPORTS_AVX512F})
    # MSVC doesn't have separate SHA-NI flag - it's included with SSE4.1
    set(COMPILER_SUPPORTS_SHA_NI ON)
    set(COMPILER_SUPPORTS_SSE41 ON)
    set(COMPILER_SUPPORTS_SSSE3 ON)
    # MSVC doesn't have -march=native equivalent
    set(COMPILER_SUPPORTS_MARCH_NATIVE OFF)
endif()

# ---------------------------------------------------------------------------
# LTO Support
# ---------------------------------------------------------------------------
if(ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT LTO_SUPPORTED OUTPUT LTO_ERROR)
    if(LTO_SUPPORTED)
        message(STATUS "LTO enabled")
    else()
        message(WARNING "LTO requested but not supported: ${LTO_ERROR}")
    endif()
endif()

# ---------------------------------------------------------------------------
# CUDA Detection (Optional)
# ---------------------------------------------------------------------------
include(CheckLanguage)
check_language(CUDA)

set(HAVE_CUDA OFF)
if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    set(HAVE_CUDA ON)
    message(STATUS "CUDA found: ${CMAKE_CUDA_COMPILER}")
    if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
        set(CMAKE_CUDA_ARCHITECTURES 75)
    endif()
    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
else()
    message(STATUS "CUDA not found - building CPU-only version")
endif()

# ---------------------------------------------------------------------------
# TLS / OpenSSL
# ---------------------------------------------------------------------------
if(ENABLE_TLS)
    find_package(OpenSSL REQUIRED)
    message(STATUS "TLS enabled with OpenSSL ${OPENSSL_VERSION}")
endif()

# ---------------------------------------------------------------------------
# Common Compile Flags
# ---------------------------------------------------------------------------
if(NOT MSVC)
    # GCC/Clang flags
    # Architecture flags (match Makefile: -m64 -march=native -mtune=native -mssse3)
    set(KEYHUNT_ARCH_FLAGS "")
    if(COMPILER_SUPPORTS_MARCH_NATIVE)
        list(APPEND KEYHUNT_ARCH_FLAGS -m64 -march=native -mtune=native)
    endif()
    if(COMPILER_SUPPORTS_SSSE3)
        list(APPEND KEYHUNT_ARCH_FLAGS -mssse3)
    endif()

    # Optimization flags (match Makefile: -O2 -ftree-vectorize -funroll-loops -pipe)
    set(KEYHUNT_OPT_FLAGS
        -ftree-vectorize
        -funroll-loops
        -pipe
    )

    # Warning flags
    set(KEYHUNT_WARN_FLAGS -Wall -Wextra)
    set(KEYHUNT_CXX_WARN_FLAGS -Wno-deprecated-copy)
    set(KEYHUNT_C_WARN_FLAGS -Wno-unused-parameter -Wno-unused-result)
else()
    # MSVC flags
    # Architecture flags - MSVC uses /arch: for SIMD
    set(KEYHUNT_ARCH_FLAGS "")
    # Base AVX2 support (includes AVX, SSE4.2, etc.)
    if(COMPILER_SUPPORTS_AVX2)
        list(APPEND KEYHUNT_ARCH_FLAGS /arch:AVX2)
    endif()

    # Optimization flags
    set(KEYHUNT_OPT_FLAGS
        /O2          # Maximum optimization
        /Oi          # Enable intrinsic functions
        /Ot          # Favor fast code
        /GL          # Whole program optimization (similar to LTO)
    )

    # Warning flags
    set(KEYHUNT_WARN_FLAGS /W3)  # Warning level 3 (reasonable)
    set(KEYHUNT_CXX_WARN_FLAGS
        /wd4996      # Disable deprecation warnings (strcpy, etc.)
        /wd4244      # Disable conversion warnings
        /wd4267      # Disable size_t conversion warnings
    )
    set(KEYHUNT_C_WARN_FLAGS ${KEYHUNT_CXX_WARN_FLAGS})

    # MSVC-specific defines
    add_compile_definitions(
        _CRT_SECURE_NO_WARNINGS    # Disable secure CRT warnings
        _CRT_NONSTDC_NO_DEPRECATE  # Disable POSIX function warnings
        NOMINMAX                    # Prevent min/max macro pollution
    )
endif()

# ---------------------------------------------------------------------------
# Source Files
# ---------------------------------------------------------------------------

# -- secp256k1 elliptic curve library --
set(SECP256K1_SOURCES
    src/secp256k1/Int.cpp
    src/secp256k1/Point.cpp
    src/secp256k1/SECP256K1.cpp
    src/secp256k1/IntMod.cpp
    src/secp256k1/Random.cpp
    src/secp256k1/IntGroup.cpp
)

# -- Hash implementations --
set(HASH_SOURCES
    src/hash/ripemd160.cpp
    src/hash/ripemd160_sse.cpp
    src/hash/sha256.cpp
    src/hash/sha256_sse.cpp
    src/hash/sha512.cpp
)

# AVX2 hash sources (need -mavx2)
set(HASH_AVX2_SOURCES
    src/hash/ripemd160_avx2.cpp
    src/hash/sha256_avx2.cpp
    src/hash/sha512_avx2.cpp
)

# AVX-512 hash sources (need -mavx512f -mavx512dq)
set(HASH_AVX512_SOURCES
    src/hash/ripemd160_avx512.cpp
    src/hash/sha256_avx512.cpp
    src/hash/sha512_avx512.cpp
)

# SHA-NI hash sources (need -msha -msse4.1)
set(HASH_SHANI_SOURCES
    src/hash/sha256_shani.cpp
)

# -- Bloom filter --
set(BLOOM_SOURCES
    src/bloom/bloom.cpp
)

# Bloom SIMD (needs -mavx2)
set(BLOOM_AVX2_SOURCES
    src/bloom/bloom_simd.cpp
)

# -- BSGS --
set(BSGS_SOURCES
    src/bsgs/bsgs_sort.cpp
)

# BSGS AVX2 sources (need -mavx2)
set(BSGS_AVX2_SOURCES
    src/bsgs/bsgs_ops.cpp
    src/bsgs/bsgs_fast.cpp
)

# -- Platform abstraction (C files) --
set(PLATFORM_SOURCES
    src/platform/platform_thread.c
    src/platform/platform_mutex.c
    src/platform/platform_time.c
    src/platform/platform_terminal.c
    src/platform/platform_compat.c
    src/platform/platform_dir.c
)

# -- Search modules --
set(SEARCH_SOURCES
    src/search/search_xpoint.cpp
    src/search/search_rmd160.cpp
    src/search/search_bsgs.cpp
    src/search/search_bsgs_threads.cpp
    src/search/search_minikeys.cpp
    src/search/search_address.cpp
    src/search/search_vanity.cpp
)

# -- Core (C files, some compiled as C++) --
set(CORE_C_SOURCES
    src/core/sysinfo.c
    src/core/parameter_validator.c
    src/core/config.c
)

# C files that MUST be compiled as C++
set(CORE_CXX_AS_C_SOURCES
    src/core/util.c
)

# -- SHA3 (C files compiled as C++) --
set(SHA3_CXX_AS_C_SOURCES
    src/sha3/sha3.c
    src/sha3/keccak.c
)

# -- Config module --
set(CONFIG_SOURCES
    src/config/config.cpp
)

# -- Hybrid scheduler --
set(HYBRID_SOURCES
    src/hybrid/adaptive_scheduler.c
)

# -- Distributed --
set(DISTRIBUTED_SOURCES
    src/distributed/distributed.c
)

# -- Wizard --
set(WIZARD_SOURCES
    src/wizard/wizard.c
    src/wizard/wizard_config.c
    src/wizard/wizard_ui.c
    src/wizard/wizard_community.c
    src/wizard/wizard_server.c
    src/wizard/wizard_client.c
    src/wizard/wizard_http.c
    src/wizard/wizard_webhooks.c
)

# -- GPU backend --
if(HAVE_CUDA)
    set(GPU_SOURCES
        src/gpu/gpu_backend_cuda.cu
        src/gpu/gpu_autotune.c
        src/gpu/multi_gpu_scheduler.c
        src/gpu/async_pipeline.c
    )
else()
    set(GPU_SOURCES
        src/gpu/gpu_backend_none.cpp
        src/gpu/gpu_autotune.c
        src/gpu/multi_gpu_scheduler.c
        src/gpu/async_pipeline.c
    )
endif()

# -- Misc single-file modules --
set(MISC_SOURCES
    src/base58/base58.c
    src/rmd160/rmd160.c
    src/xxhash/xxhash.c
    src/util/mempool.c
    src/output.cpp
    src/progress.cpp
    src/benchmark.cpp
    src/cli.cpp
    src/sort/sort.cpp
    src/crypto/address_util.cpp
    src/crypto/bloom_init.cpp
    src/io/io.cpp
)

# -- Collect all common sources (everything except main and wizard) --
set(COMMON_SOURCES
    ${SECP256K1_SOURCES}
    ${HASH_SOURCES}
    ${HASH_AVX2_SOURCES}
    ${HASH_AVX512_SOURCES}
    ${HASH_SHANI_SOURCES}
    ${BLOOM_SOURCES}
    ${BLOOM_AVX2_SOURCES}
    ${BSGS_SOURCES}
    ${BSGS_AVX2_SOURCES}
    ${PLATFORM_SOURCES}
    ${SEARCH_SOURCES}
    ${CORE_C_SOURCES}
    ${CORE_CXX_AS_C_SOURCES}
    ${SHA3_CXX_AS_C_SOURCES}
    ${CONFIG_SOURCES}
    ${HYBRID_SOURCES}
    ${DISTRIBUTED_SOURCES}
    ${GPU_SOURCES}
    ${MISC_SOURCES}
)

# -- Main keyhunt sources --
set(KEYHUNT_SOURCES
    src/keyhunt.cpp
    ${COMMON_SOURCES}
    ${WIZARD_SOURCES}
)

# ===========================================================================
# Main Target: keyhunt
# ===========================================================================
add_executable(keyhunt ${KEYHUNT_SOURCES})

target_include_directories(keyhunt PRIVATE ${CMAKE_SOURCE_DIR}/src)

# ---------------------------------------------------------------------------
# Per-file SIMD flags (using set_source_files_properties)
# ---------------------------------------------------------------------------

# C files compiled as C++
set_source_files_properties(
    src/core/util.c
    src/sha3/sha3.c
    src/sha3/keccak.c
    PROPERTIES LANGUAGE CXX
)

# AVX2 flags
if(COMPILER_SUPPORTS_AVX2)
    if(NOT MSVC)
        set_source_files_properties(
            ${HASH_AVX2_SOURCES}
            ${BLOOM_AVX2_SOURCES}
            ${BSGS_AVX2_SOURCES}
            PROPERTIES COMPILE_OPTIONS "-mavx2"
        )
    else()
        set_source_files_properties(
            ${HASH_AVX2_SOURCES}
            ${BLOOM_AVX2_SOURCES}
            ${BSGS_AVX2_SOURCES}
            PROPERTIES COMPILE_OPTIONS "/arch:AVX2"
        )
    endif()
else()
    message(WARNING "Compiler does not support AVX2 - AVX2 optimizations disabled")
endif()

# AVX-512 flags
if(COMPILER_SUPPORTS_AVX512F AND COMPILER_SUPPORTS_AVX512DQ)
    if(NOT MSVC)
        set_source_files_properties(
            ${HASH_AVX512_SOURCES}
            PROPERTIES COMPILE_OPTIONS "-mavx512f;-mavx512dq"
        )
    else()
        set_source_files_properties(
            ${HASH_AVX512_SOURCES}
            PROPERTIES COMPILE_OPTIONS "/arch:AVX512"
        )
    endif()
else()
    message(WARNING "Compiler does not support AVX-512 - AVX-512 optimizations disabled")
endif()

# SHA-NI flags
if(COMPILER_SUPPORTS_SHA_NI AND COMPILER_SUPPORTS_SSE41)
    if(NOT MSVC)
        set_source_files_properties(
            ${HASH_SHANI_SOURCES}
            PROPERTIES COMPILE_OPTIONS "-msha;-msse4.1"
        )
    else()
        # MSVC: SHA-NI intrinsics are available when targeting modern CPUs
        # No specific flag needed - already included with /arch:AVX2
        set_source_files_properties(
            ${HASH_SHANI_SOURCES}
            PROPERTIES COMPILE_OPTIONS "/arch:AVX2"
        )
    endif()
else()
    message(WARNING "Compiler does not support SHA-NI - SHA-NI optimizations disabled")
endif()

# ---------------------------------------------------------------------------
# Compile options for main target
# ---------------------------------------------------------------------------
target_compile_options(keyhunt PRIVATE
    ${KEYHUNT_ARCH_FLAGS}
    ${KEYHUNT_OPT_FLAGS}
    ${KEYHUNT_WARN_FLAGS}
    $<$<COMPILE_LANGUAGE:CXX>:${KEYHUNT_CXX_WARN_FLAGS}>
    $<$<COMPILE_LANGUAGE:C>:${KEYHUNT_C_WARN_FLAGS}>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
)

# Release-mode adds -DNDEBUG automatically via CMAKE_CXX_FLAGS_RELEASE

# CUDA define
if(HAVE_CUDA)
    target_compile_definitions(keyhunt PRIVATE HAVE_CUDA_BACKEND=1)
endif()

# TLS define
if(ENABLE_TLS)
    target_compile_definitions(keyhunt PRIVATE HAVE_OPENSSL)
    target_link_libraries(keyhunt PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()

# LTO
if(ENABLE_LTO AND LTO_SUPPORTED)
    set_target_properties(keyhunt PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# Link libraries
if(NOT WIN32)
    # POSIX systems (Linux, macOS)
    target_link_libraries(keyhunt PRIVATE
        m          # Math library
        pthread    # POSIX threads
        ${CMAKE_DL_LIBS}  # dlopen/dlsym
    )
else()
    # Windows
    target_link_libraries(keyhunt PRIVATE
        ws2_32     # Winsock2 (network functions)
        bcrypt     # Windows crypto API (random number generation)
    )
endif()

# CUDA link
if(HAVE_CUDA)
    target_link_libraries(keyhunt PRIVATE CUDA::cudart)
endif()

# Linker flags to match Makefile behavior
if(NOT MSVC)
    target_link_options(keyhunt PRIVATE
        "LINKER:--as-needed"
    )
else()
    # MSVC linker flags
    target_link_options(keyhunt PRIVATE
        /LTCG      # Link-time code generation (works with /GL)
    )
endif()

# Output to project root (match Makefile behavior)
set_target_properties(keyhunt PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ===========================================================================
# Test Target: run_tests
# ===========================================================================
enable_testing()

set(TEST_SOURCES
    tests/run_tests.cpp
    tests/test_int.cpp
    tests/test_bloom.cpp
    tests/test_bsgs_integration.cpp
    tests/test_bsgs_sort.cpp
    tests/test_gpu_backend.cpp
    tests/test_distributed.cpp
    tests/test_wizard.cpp
    tests/test_hash.cpp
    tests/test_bsgs_ops.cpp
    tests/test_point.cpp
    tests/test_intgroup.cpp
    tests/test_sha512_simd.cpp
    tests/test_sha256_simd.cpp
    tests/test_search_xpoint.cpp
    tests/test_search_rmd160.cpp
    tests/test_search_mocks.cpp
    tests/test_fused_hash.cpp
)

# Test shared objects = common sources + wizard (matches Makefile TEST_SHARED_OBJS)
set(TEST_LIB_SOURCES
    ${COMMON_SOURCES}
    ${WIZARD_SOURCES}
)

add_executable(run_tests ${TEST_SOURCES} ${TEST_LIB_SOURCES})

target_include_directories(run_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Apply the same per-file properties to test target sources
# Note: set_source_files_properties is global per-directory scope, so the
# LANGUAGE and COMPILE_OPTIONS set above already apply to these files when
# used in run_tests as well.

target_compile_options(run_tests PRIVATE
    ${KEYHUNT_ARCH_FLAGS}
    ${KEYHUNT_OPT_FLAGS}
    ${KEYHUNT_WARN_FLAGS}
    $<$<COMPILE_LANGUAGE:CXX>:${KEYHUNT_CXX_WARN_FLAGS}>
    $<$<COMPILE_LANGUAGE:C>:${KEYHUNT_C_WARN_FLAGS}>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
)

if(HAVE_CUDA)
    target_compile_definitions(run_tests PRIVATE HAVE_CUDA_BACKEND=1)
endif()

if(ENABLE_TLS)
    target_compile_definitions(run_tests PRIVATE HAVE_OPENSSL)
    target_link_libraries(run_tests PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()

if(ENABLE_LTO AND LTO_SUPPORTED)
    set_target_properties(run_tests PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

if(NOT WIN32)
    # POSIX systems
    target_link_libraries(run_tests PRIVATE
        m
        pthread
        ${CMAKE_DL_LIBS}
    )
else()
    # Windows
    target_link_libraries(run_tests PRIVATE
        ws2_32
        bcrypt
    )
endif()

if(HAVE_CUDA)
    target_link_libraries(run_tests PRIVATE CUDA::cudart)
endif()

if(NOT MSVC)
    target_link_options(run_tests PRIVATE
        "LINKER:--as-needed"
    )
else()
    target_link_options(run_tests PRIVATE
        /LTCG
    )
endif()

# Output test binary to project root (match Makefile behavior)
set_target_properties(run_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Register with CTest
add_test(
    NAME keyhunt_tests
    COMMAND run_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ===========================================================================
# Summary
# ===========================================================================
message(STATUS "")
message(STATUS "=== keyhunt build configuration ===")
message(STATUS "  Platform:      ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Compiler:      ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "  Build type:    ${CMAKE_BUILD_TYPE}")
message(STATUS "  C compiler:    ${CMAKE_C_COMPILER}")
message(STATUS "  C++ compiler:  ${CMAKE_CXX_COMPILER}")
if(WIN32)
    message(STATUS "  Windows libs:  ws2_32, bcrypt")
endif()
message(STATUS "  CUDA:          ${HAVE_CUDA}")
if(HAVE_CUDA)
    message(STATUS "  CUDA arch:     ${CMAKE_CUDA_ARCHITECTURES}")
endif()
message(STATUS "  AVX2:          ${COMPILER_SUPPORTS_AVX2}")
message(STATUS "  AVX-512:       ${COMPILER_SUPPORTS_AVX512F}")
message(STATUS "  SHA-NI:        ${COMPILER_SUPPORTS_SHA_NI}")
message(STATUS "  LTO:           ${ENABLE_LTO} (supported: ${LTO_SUPPORTED})")
message(STATUS "  TLS (OpenSSL): ${ENABLE_TLS}")
message(STATUS "===================================")
message(STATUS "")
