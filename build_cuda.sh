#!/bin/bash
#
# Build script for keyhunt with CUDA GPU support
# Handles GCC version compatibility issues automatically
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== Keyhunt CUDA Build Script ===${NC}"

# Default values
CUDA_HOME="${CUDA_HOME:-/usr/local/cuda}"
CUDA_ARCH="${CUDA_ARCH:-sm_75}"
JOBS="${JOBS:-$(nproc)}"
GCC_VERSION=""
CCBIN_DIR=""

# Try to find CUDA
find_cuda() {
    local cuda_paths=(
        "$CUDA_HOME"
        "/usr/local/cuda"
        "/home/$USER/cuda-12.4"
        "/home/$USER/cuda-12.8"
        "/opt/cuda"
    )

    for path in "${cuda_paths[@]}"; do
        if [[ -x "$path/bin/nvcc" ]]; then
            CUDA_HOME="$path"
            echo -e "${GREEN}Found CUDA at: $CUDA_HOME${NC}"
            return 0
        fi
    done

    # Check if nvcc is in PATH
    if command -v nvcc &> /dev/null; then
        CUDA_HOME=$(dirname $(dirname $(which nvcc)))
        echo -e "${GREEN}Found nvcc in PATH, CUDA_HOME: $CUDA_HOME${NC}"
        return 0
    fi

    echo -e "${YELLOW}CUDA not found, building without GPU support${NC}"
    return 1
}

# Find compatible GCC for CUDA
find_compatible_gcc() {
    # CUDA 12.x supports up to GCC 13/14
    # Check for Homebrew GCC first
    local gcc_paths=(
        "$(brew --prefix 2>/dev/null)/bin/gcc-13"
        "$(brew --prefix 2>/dev/null)/bin/gcc-14"
        "/usr/bin/gcc-13"
        "/usr/bin/gcc-14"
        "/usr/bin/gcc-12"
    )

    for gcc in "${gcc_paths[@]}"; do
        if [[ -x "$gcc" ]]; then
            local version=$("$gcc" -dumpversion 2>/dev/null | cut -d. -f1)
            if [[ "$version" -le 14 ]]; then
                GCC_VERSION="$gcc"
                echo -e "${GREEN}Found compatible GCC: $GCC_VERSION (version $version)${NC}"
                return 0
            fi
        fi
    done

    # Check system GCC version
    local sys_gcc_version=$(gcc -dumpversion 2>/dev/null | cut -d. -f1)
    if [[ "$sys_gcc_version" -le 14 ]]; then
        echo -e "${GREEN}System GCC version $sys_gcc_version is compatible${NC}"
        return 0
    fi

    echo -e "${YELLOW}System GCC $sys_gcc_version may not be compatible with CUDA${NC}"
    return 1
}

# Setup compiler bindir for nvcc
setup_ccbin() {
    if [[ -z "$GCC_VERSION" ]]; then
        return 0
    fi

    CCBIN_DIR="/tmp/ccbin-keyhunt-$$"
    mkdir -p "$CCBIN_DIR"

    local gcc_dir=$(dirname "$GCC_VERSION")
    local gcc_base=$(basename "$GCC_VERSION" | sed 's/gcc//')

    ln -sf "$GCC_VERSION" "$CCBIN_DIR/gcc"
    ln -sf "${gcc_dir}/g++${gcc_base}" "$CCBIN_DIR/g++" 2>/dev/null || \
    ln -sf "${GCC_VERSION/gcc/g++}" "$CCBIN_DIR/g++"

    echo -e "${BLUE}Created compiler symlinks in $CCBIN_DIR${NC}"
}

# Cleanup
cleanup() {
    if [[ -n "$CCBIN_DIR" && -d "$CCBIN_DIR" ]]; then
        rm -rf "$CCBIN_DIR"
    fi
}
trap cleanup EXIT

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --cuda-home)
            CUDA_HOME="$2"
            shift 2
            ;;
        --arch)
            CUDA_ARCH="$2"
            shift 2
            ;;
        --jobs|-j)
            JOBS="$2"
            shift 2
            ;;
        --clean)
            echo -e "${BLUE}Cleaning build artifacts...${NC}"
            make clean
            exit 0
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --cuda-home PATH   Path to CUDA toolkit (default: /usr/local/cuda)"
            echo "  --arch ARCH        CUDA architecture (default: sm_75)"
            echo "  --jobs N           Parallel jobs (default: nproc)"
            echo "  --clean            Clean build artifacts and exit"
            echo ""
            echo "Environment variables:"
            echo "  CUDA_HOME          Alternative to --cuda-home"
            echo "  CUDA_ARCH          Alternative to --arch"
            echo ""
            echo "Examples:"
            echo "  $0                                    # Auto-detect everything"
            echo "  $0 --cuda-home /home/user/cuda-12.4   # Specify CUDA path"
            echo "  $0 --arch sm_86                       # For RTX 30xx GPUs"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            exit 1
            ;;
    esac
done

# Main build logic
echo ""
echo -e "${BLUE}Step 1: Detecting CUDA...${NC}"
if ! find_cuda; then
    echo -e "${YELLOW}Building without CUDA support${NC}"
    make -B -j"$JOBS"
    echo -e "${GREEN}Build complete (CPU only)${NC}"
    exit 0
fi

echo ""
echo -e "${BLUE}Step 2: Checking GCC compatibility...${NC}"
if ! find_compatible_gcc; then
    echo -e "${YELLOW}Trying build anyway (may fail)...${NC}"
fi

# Setup compiler bindir if needed
if [[ -n "$GCC_VERSION" ]]; then
    setup_ccbin
fi

# Build NVCC flags
NVCC_FLAGS="-O3 -std=c++17 -arch=$CUDA_ARCH -allow-unsupported-compiler"
if [[ -n "$CCBIN_DIR" ]]; then
    NVCC_FLAGS="$NVCC_FLAGS --compiler-bindir=$CCBIN_DIR -Xcompiler -U_GNU_SOURCE"
fi

echo ""
echo -e "${BLUE}Step 3: Building keyhunt with CUDA...${NC}"
echo -e "  CUDA_HOME: $CUDA_HOME"
echo -e "  CUDA_ARCH: $CUDA_ARCH"
echo -e "  NVCC_FLAGS: $NVCC_FLAGS"
echo -e "  Jobs: $JOBS"
echo ""

# Clean and rebuild
make clean 2>/dev/null || true

# Build with CUDA
make -B -j"$JOBS" \
    NVCC="$CUDA_HOME/bin/nvcc" \
    CUDA_HOME="$CUDA_HOME" \
    NVCCFLAGS="$NVCC_FLAGS"

# Verify build
echo ""
if [[ -x ./keyhunt ]]; then
    echo -e "${GREEN}Build successful!${NC}"
    echo ""
    echo -e "${BLUE}Binary info:${NC}"
    ls -lh keyhunt
    echo ""

    # Check CUDA linking
    if ldd keyhunt | grep -q libcudart; then
        echo -e "${GREEN}CUDA runtime linked successfully${NC}"
        ldd keyhunt | grep cuda
    else
        echo -e "${YELLOW}Warning: CUDA runtime not linked${NC}"
    fi

    echo ""
    echo -e "${BLUE}Quick test:${NC}"
    timeout 3 ./keyhunt -m address -f tests/1to32.txt -r 1:FF -G auto 2>&1 | head -15 || true
else
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi
