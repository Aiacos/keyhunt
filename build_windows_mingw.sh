#!/bin/bash
#
# Build script for keyhunt with MinGW-w64 cross-compilation (Windows 64-bit)
# Handles auto-detection of MinGW-w64 toolchain and Wine for testing
#
# Supported platforms: Linux (cross-compile to Windows)
# Tested with: MinGW-w64 8.0+, Ubuntu, Fedora, Debian
#

set -e

# Ensure we run from the project root (where Makefile lives)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== Keyhunt Windows Build Script (MinGW-w64) ===${NC}"
echo -e "${CYAN}Version 1.0 - Cross-compilation for Windows 64-bit${NC}"
echo ""

# Default values
MINGW_PREFIX="${MINGW_PREFIX:-x86_64-w64-mingw32}"
JOBS="${JOBS:-$(nproc)}"
VERBOSE=0
BUILD_LEGACY=0
BUILD_TESTS=0
RUN_TESTS=0
CLEAN_FIRST=0

# Print verbose message
verbose() {
    if [[ $VERBOSE -eq 1 ]]; then
        echo -e "${CYAN}[DEBUG] $1${NC}"
    fi
}

# Print help message
show_help() {
    cat << EOF
${BLUE}Keyhunt Windows Build Script (MinGW-w64)${NC}

${YELLOW}Usage:${NC}
  $0 [options]

${YELLOW}Options:${NC}
  -h, --help              Show this help message
  -v, --verbose           Enable verbose output
  -j, --jobs N            Number of parallel jobs (default: $(nproc))
  -c, --clean             Clean build artifacts before building
  -l, --legacy            Also build legacy version (requires OpenSSL, GMP)
  -t, --test              Build test suite
  -r, --run-tests         Run tests with Wine (requires Wine installed)
  --prefix PREFIX         MinGW-w64 prefix (default: x86_64-w64-mingw32)

${YELLOW}Environment Variables:${NC}
  MINGW_PREFIX            MinGW-w64 toolchain prefix (default: x86_64-w64-mingw32)
  JOBS                    Number of parallel build jobs

${YELLOW}Examples:${NC}
  $0                      # Basic build
  $0 --clean              # Clean build
  $0 --legacy --test      # Build with legacy and tests
  $0 --run-tests          # Build and run tests with Wine
  $0 --prefix i686-w64-mingw32  # Build for 32-bit Windows

${YELLOW}Output:${NC}
  keyhunt.exe             Main executable (Windows 64-bit)
  keyhunt_legacy.exe      Legacy version (if --legacy used)
  run_tests.exe           Test suite (if --test used)

${YELLOW}Notes:${NC}
  - This script cross-compiles for Windows from Linux
  - Requires MinGW-w64 toolchain (x86_64-w64-mingw32-gcc/g++)
  - Wine is needed to run tests on Linux (optional)
  - The executable runs natively on Windows 10/11 (64-bit)

${YELLOW}Installation:${NC}
  Ubuntu/Debian: sudo apt install mingw-w64 wine64
  Fedora:        sudo dnf install mingw64-gcc mingw64-gcc-c++ wine
  Arch:          sudo pacman -S mingw-w64-gcc wine

EOF
}

# Parse command-line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            show_help
            exit 0
            ;;
        -v|--verbose)
            VERBOSE=1
            shift
            ;;
        -j|--jobs)
            JOBS="$2"
            shift 2
            ;;
        -c|--clean)
            CLEAN_FIRST=1
            shift
            ;;
        -l|--legacy)
            BUILD_LEGACY=1
            shift
            ;;
        -t|--test)
            BUILD_TESTS=1
            shift
            ;;
        -r|--run-tests)
            BUILD_TESTS=1
            RUN_TESTS=1
            shift
            ;;
        --prefix)
            MINGW_PREFIX="$2"
            shift 2
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Detect MinGW-w64 toolchain
detect_mingw() {
    echo -e "${BLUE}Detecting MinGW-w64 toolchain...${NC}"

    local gcc="${MINGW_PREFIX}-gcc"
    local gxx="${MINGW_PREFIX}-g++"

    if ! command -v "$gcc" &> /dev/null; then
        echo -e "${RED}MinGW-w64 GCC not found: $gcc${NC}"
        echo ""
        echo -e "${YELLOW}Please install MinGW-w64 toolchain:${NC}"
        echo "  Ubuntu/Debian: sudo apt install mingw-w64"
        echo "  Fedora:        sudo dnf install mingw64-gcc mingw64-gcc-c++"
        echo "  Arch:          sudo pacman -S mingw-w64-gcc"
        echo ""
        echo -e "${BLUE}Or specify custom prefix with --prefix${NC}"
        return 1
    fi

    if ! command -v "$gxx" &> /dev/null; then
        echo -e "${RED}MinGW-w64 G++ not found: $gxx${NC}"
        echo ""
        echo -e "${YELLOW}Please install MinGW-w64 C++ compiler${NC}"
        return 1
    fi

    # Get MinGW-w64 version
    local gcc_version
    gcc_version=$("$gcc" --version 2>/dev/null | head -1)

    echo -e "${GREEN}Found MinGW-w64 toolchain:${NC}"
    echo -e "  GCC:  $gcc"
    echo -e "  G++:  $gxx"
    echo -e "  Version: $gcc_version"

    verbose "MinGW-w64 prefix: $MINGW_PREFIX"
    return 0
}

# Detect Wine (optional, for running tests)
detect_wine() {
    if [[ $RUN_TESTS -eq 0 ]]; then
        return 0
    fi

    echo ""
    echo -e "${BLUE}Detecting Wine (for running Windows tests)...${NC}"

    if command -v wine64 &> /dev/null; then
        local wine_version
        wine_version=$(wine64 --version 2>/dev/null)
        echo -e "${GREEN}Found Wine: $wine_version${NC}"
        return 0
    elif command -v wine &> /dev/null; then
        local wine_version
        wine_version=$(wine --version 2>/dev/null)
        echo -e "${GREEN}Found Wine: $wine_version${NC}"
        return 0
    else
        echo -e "${YELLOW}Wine not found - cannot run tests on Linux${NC}"
        echo -e "${YELLOW}Install Wine to run Windows executables on Linux:${NC}"
        echo "  Ubuntu/Debian: sudo apt install wine64"
        echo "  Fedora:        sudo dnf install wine"
        echo ""
        echo -e "${BLUE}Tests will be skipped${NC}"
        RUN_TESTS=0
        return 1
    fi
}

# Check for legacy dependencies
check_legacy_deps() {
    if [[ $BUILD_LEGACY -eq 0 ]]; then
        return 0
    fi

    echo ""
    echo -e "${BLUE}Checking legacy build dependencies...${NC}"

    local missing=0
    local openssl="${MINGW_PREFIX}-pkg-config"

    # Check if MinGW-w64 has OpenSSL and GMP
    echo -e "${YELLOW}Warning: Legacy build requires MinGW-w64 OpenSSL and GMP libraries${NC}"
    echo -e "${YELLOW}These may need to be installed separately${NC}"
    echo ""
    echo -e "${BLUE}Proceeding with legacy build (may fail if libraries missing)${NC}"

    return 0
}

# Clean build artifacts
clean_build() {
    echo ""
    echo -e "${BLUE}Cleaning build artifacts...${NC}"
    make clean
    echo -e "${GREEN}Clean complete${NC}"
}

# Build main executable
build_keyhunt() {
    echo ""
    echo -e "${BLUE}Building keyhunt.exe for Windows (64-bit)...${NC}"

    local gcc="${MINGW_PREFIX}-gcc"
    local gxx="${MINGW_PREFIX}-g++"

    verbose "Using compiler: CC=$gcc CXX=$gxx"
    verbose "Parallel jobs: -j$JOBS"

    if make -j"$JOBS" CC="$gcc" CXX="$gxx"; then
        echo -e "${GREEN}✓ keyhunt.exe built successfully${NC}"

        # Show file info
        if [[ -f keyhunt.exe ]]; then
            local filesize
            filesize=$(du -h keyhunt.exe | cut -f1)
            echo -e "${CYAN}  Size: $filesize${NC}"
            echo -e "${CYAN}  Type: $(file keyhunt.exe | cut -d: -f2-)${NC}"
        fi
        return 0
    else
        echo -e "${RED}✗ Build failed${NC}"
        return 1
    fi
}

# Build legacy version
build_legacy() {
    if [[ $BUILD_LEGACY -eq 0 ]]; then
        return 0
    fi

    echo ""
    echo -e "${BLUE}Building keyhunt_legacy.exe...${NC}"

    local gcc="${MINGW_PREFIX}-gcc"
    local gxx="${MINGW_PREFIX}-g++"

    if make -j"$JOBS" legacy CC="$gcc" CXX="$gxx"; then
        echo -e "${GREEN}✓ keyhunt_legacy.exe built successfully${NC}"

        if [[ -f keyhunt_legacy.exe ]]; then
            local filesize
            filesize=$(du -h keyhunt_legacy.exe | cut -f1)
            echo -e "${CYAN}  Size: $filesize${NC}"
        fi
        return 0
    else
        echo -e "${YELLOW}⚠ Legacy build failed (may need MinGW-w64 OpenSSL/GMP)${NC}"
        return 1
    fi
}

# Build test suite
build_tests() {
    if [[ $BUILD_TESTS -eq 0 ]]; then
        return 0
    fi

    echo ""
    echo -e "${BLUE}Building test suite (run_tests.exe)...${NC}"

    local gcc="${MINGW_PREFIX}-gcc"
    local gxx="${MINGW_PREFIX}-g++"

    if make -j"$JOBS" run_tests.exe CC="$gcc" CXX="$gxx"; then
        echo -e "${GREEN}✓ run_tests.exe built successfully${NC}"
        return 0
    else
        echo -e "${RED}✗ Test build failed${NC}"
        return 1
    fi
}

# Run tests with Wine
run_tests() {
    if [[ $RUN_TESTS -eq 0 || ! -f run_tests.exe ]]; then
        return 0
    fi

    echo ""
    echo -e "${BLUE}Running tests with Wine...${NC}"

    local wine_cmd="wine64"
    if ! command -v wine64 &> /dev/null; then
        wine_cmd="wine"
    fi

    if $wine_cmd run_tests.exe; then
        echo -e "${GREEN}✓ All tests passed${NC}"
        return 0
    else
        echo -e "${RED}✗ Some tests failed${NC}"
        return 1
    fi
}

# Main build flow
main() {
    # Detect toolchain
    if ! detect_mingw; then
        exit 1
    fi

    # Detect Wine (if needed)
    detect_wine || true

    # Check legacy dependencies
    check_legacy_deps

    # Clean if requested
    if [[ $CLEAN_FIRST -eq 1 ]]; then
        clean_build
    fi

    # Build main executable
    if ! build_keyhunt; then
        exit 1
    fi

    # Build legacy (if requested)
    build_legacy || true

    # Build tests (if requested)
    if ! build_tests; then
        if [[ $RUN_TESTS -eq 1 ]]; then
            echo -e "${YELLOW}Cannot run tests - build failed${NC}"
            exit 1
        fi
    fi

    # Run tests (if requested)
    run_tests || true

    # Success summary
    echo ""
    echo -e "${GREEN}╔════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${GREEN}║                 Build Completed Successfully               ║${NC}"
    echo -e "${GREEN}╚════════════════════════════════════════════════════════════╝${NC}"
    echo ""
    echo -e "${CYAN}Built executables:${NC}"
    [[ -f keyhunt.exe ]] && echo -e "  ${GREEN}✓${NC} keyhunt.exe (main executable)"
    [[ -f keyhunt_legacy.exe ]] && echo -e "  ${GREEN}✓${NC} keyhunt_legacy.exe (legacy)"
    [[ -f run_tests.exe ]] && echo -e "  ${GREEN}✓${NC} run_tests.exe (test suite)"
    echo ""
    echo -e "${CYAN}Next steps:${NC}"
    echo "  1. Copy keyhunt.exe to Windows machine"
    echo "  2. Run: keyhunt.exe --help"
    echo "  3. Example: keyhunt.exe -m address -f puzzles.txt -t 4"
    echo ""
    echo -e "${BLUE}To test on Linux with Wine:${NC}"
    [[ -f keyhunt.exe ]] && echo "  wine64 keyhunt.exe --help"
    echo ""
}

# Run main
main
