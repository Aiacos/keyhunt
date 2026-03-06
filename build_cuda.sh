#!/bin/bash
#
# Build script for keyhunt with CUDA GPU support
# Handles GCC version compatibility issues automatically
#
# Supported CUDA versions: 11.0 - 12.x
# Supported GPU architectures: sm_50 (Maxwell) through sm_90 (Hopper)
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

echo -e "${BLUE}=== Keyhunt CUDA Build Script ===${NC}"
echo -e "${CYAN}Version 2.0 - Auto-detection for modern systems${NC}"
echo ""

# Default values
CUDA_HOME="${CUDA_HOME:-/usr/local/cuda}"
CUDA_ARCH="${CUDA_ARCH:-}"
JOBS="${JOBS:-$(nproc)}"
GCC_VERSION=""
CCBIN_DIR=""
VERBOSE=0
DETECTED_GPU_ARCH=""

# Print verbose message
verbose() {
    if [[ $VERBOSE -eq 1 ]]; then
        echo -e "${CYAN}[DEBUG] $1${NC}"
    fi
}

# Detect GPU architecture automatically
detect_gpu_arch() {
    if ! command -v nvidia-smi &> /dev/null; then
        echo -e "${YELLOW}nvidia-smi not found, using default architecture${NC}"
        return 1
    fi

    # Get compute capability from nvidia-smi
    local compute_cap
    compute_cap=$(nvidia-smi --query-gpu=compute_cap --format=csv,noheader 2>/dev/null | head -1)

    if [[ -z "$compute_cap" ]]; then
        echo -e "${YELLOW}Could not detect GPU compute capability${NC}"
        return 1
    fi

    # Convert compute capability (e.g., 7.5) to sm_xx format
    local major
    local minor
    major=$(echo "$compute_cap" | cut -d. -f1)
    minor=$(echo "$compute_cap" | cut -d. -f2)
    DETECTED_GPU_ARCH="sm_${major}${minor}"

    # Get GPU name for display
    local gpu_name
    gpu_name=$(nvidia-smi --query-gpu=name --format=csv,noheader 2>/dev/null | head -1)

    echo -e "${GREEN}Detected GPU: $gpu_name (Compute $compute_cap -> $DETECTED_GPU_ARCH)${NC}"
    return 0
}

# Try to find CUDA in common locations
find_cuda() {
    local cuda_paths=(
        "$CUDA_HOME"
        "/usr/local/cuda"
        "/usr/local/cuda-12.6"
        "/usr/local/cuda-12.4"
        "/usr/local/cuda-12.2"
        "/usr/local/cuda-12.0"
        "/usr/local/cuda-11.8"
        "/home/$USER/cuda-12.8"
        "/home/$USER/cuda-12.6"
        "/home/$USER/cuda-12.4"
        "/opt/cuda"
        "/usr/lib/cuda"
    )

    for path in "${cuda_paths[@]}"; do
        if [[ -x "$path/bin/nvcc" ]]; then
            CUDA_HOME="$path"
            local cuda_version
            cuda_version=$("$path/bin/nvcc" --version 2>/dev/null | grep "release" | sed 's/.*release \([0-9.]*\).*/\1/')
            echo -e "${GREEN}Found CUDA $cuda_version at: $CUDA_HOME${NC}"
            return 0
        fi
    done

    # Check if nvcc is in PATH
    if command -v nvcc &> /dev/null; then
        CUDA_HOME=$(dirname "$(dirname "$(which nvcc)")")
        local cuda_version
        cuda_version=$(nvcc --version 2>/dev/null | grep "release" | sed 's/.*release \([0-9.]*\).*/\1/')
        echo -e "${GREEN}Found nvcc in PATH (CUDA $cuda_version), CUDA_HOME: $CUDA_HOME${NC}"
        return 0
    fi

    echo -e "${YELLOW}CUDA not found in standard locations${NC}"
    echo -e "${YELLOW}Searched: /usr/local/cuda*, /opt/cuda, ~/cuda-*${NC}"
    echo ""
    echo -e "${BLUE}To install CUDA:${NC}"
    echo "  Ubuntu/Debian: sudo apt install nvidia-cuda-toolkit"
    echo "  Fedora:        sudo dnf install cuda-toolkit"
    echo "  Manual:        https://developer.nvidia.com/cuda-downloads"
    return 1
}

# Find compatible GCC for CUDA
# CUDA 12.x officially supports up to GCC 13, but GCC 14 works with -allow-unsupported-compiler
find_compatible_gcc() {
    # Check Homebrew GCC paths first (common on Fedora with Linuxbrew)
    local homebrew_prefix=""
    if command -v brew &> /dev/null; then
        homebrew_prefix="$(brew --prefix 2>/dev/null)"
    fi

    local gcc_paths=(
        "${homebrew_prefix}/bin/gcc-13"
        "${homebrew_prefix}/bin/gcc-14"
        "${homebrew_prefix}/bin/gcc-12"
        "/usr/bin/gcc-13"
        "/usr/bin/gcc-14"
        "/usr/bin/gcc-12"
        "/usr/bin/gcc-11"
    )

    for gcc in "${gcc_paths[@]}"; do
        if [[ -n "$gcc" && -x "$gcc" ]]; then
            local version
            version=$("$gcc" -dumpversion 2>/dev/null | cut -d. -f1)
            if [[ -n "$version" && "$version" -le 14 ]]; then
                GCC_VERSION="$gcc"
                echo -e "${GREEN}Found compatible GCC: $GCC_VERSION (version $version)${NC}"
                return 0
            fi
        fi
    done

    # Check system GCC version
    local sys_gcc_version
    sys_gcc_version=$(gcc -dumpversion 2>/dev/null | cut -d. -f1)
    if [[ -z "$sys_gcc_version" ]]; then
        echo -e "${RED}GCC not found! Please install build-essential or gcc${NC}"
        return 1
    fi

    if [[ "$sys_gcc_version" -le 14 ]]; then
        echo -e "${GREEN}System GCC version $sys_gcc_version is compatible${NC}"
        return 0
    fi

    echo -e "${YELLOW}System GCC $sys_gcc_version is newer than CUDA's officially supported versions${NC}"
    echo -e "${YELLOW}Will use -allow-unsupported-compiler flag (usually works)${NC}"
    echo ""
    echo -e "${BLUE}For best compatibility, install GCC 13:${NC}"
    echo "  Homebrew:      brew install gcc@13"
    echo "  Ubuntu/Debian: sudo apt install gcc-13 g++-13"
    echo "  Fedora:        sudo dnf install gcc13 gcc13-c++"
    return 1
}

# Setup compiler bindir for nvcc
setup_ccbin() {
    if [[ -z "$GCC_VERSION" ]]; then
        return 0
    fi

    CCBIN_DIR="/tmp/ccbin-keyhunt-$$"
    mkdir -p "$CCBIN_DIR"

    local gcc_dir
    local gcc_base
    gcc_dir=$(dirname "$GCC_VERSION")
    gcc_base=$(basename "$GCC_VERSION" | sed 's/gcc//')

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

# Print GPU architecture reference
print_arch_help() {
    echo ""
    echo -e "${BLUE}GPU Architecture Reference:${NC}"
    echo "  sm_50  - Maxwell    (GTX 900 series)"
    echo "  sm_60  - Pascal     (GTX 1000 series, P100)"
    echo "  sm_61  - Pascal     (GTX 1050/1060/1070/1080)"
    echo "  sm_70  - Volta      (V100, Titan V)"
    echo "  sm_75  - Turing     (RTX 2000 series, GTX 1660)"
    echo "  sm_80  - Ampere     (A100)"
    echo "  sm_86  - Ampere     (RTX 3000 series)"
    echo "  sm_89  - Ada        (RTX 4000 series)"
    echo "  sm_90  - Hopper     (H100)"
    echo ""
}

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
        --verbose|-v)
            VERBOSE=1
            shift
            ;;
        --clean)
            echo -e "${BLUE}Cleaning build artifacts...${NC}"
            make clean
            exit 0
            ;;
        --list-arch)
            print_arch_help
            exit 0
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Build keyhunt with CUDA GPU support. Automatically detects CUDA"
            echo "installation, compatible GCC version, and GPU architecture."
            echo ""
            echo "Options:"
            echo "  --cuda-home PATH   Path to CUDA toolkit (default: auto-detect)"
            echo "  --arch ARCH        CUDA architecture (default: auto-detect from GPU)"
            echo "  --jobs N           Parallel jobs (default: $(nproc))"
            echo "  --verbose, -v      Show detailed build information"
            echo "  --clean            Clean build artifacts and exit"
            echo "  --list-arch        Show GPU architecture reference"
            echo "  --help, -h         Show this help"
            echo ""
            echo "Environment variables:"
            echo "  CUDA_HOME          Alternative to --cuda-home"
            echo "  CUDA_ARCH          Alternative to --arch"
            echo "  JOBS               Alternative to --jobs"
            echo ""
            echo "Examples:"
            echo "  $0                                    # Auto-detect everything"
            echo "  $0 --cuda-home /usr/local/cuda-12.6   # Specify CUDA path"
            echo "  $0 --arch sm_86                       # For RTX 30xx GPUs"
            echo "  $0 --arch sm_89                       # For RTX 40xx GPUs"
            echo "  CUDA_ARCH=sm_75 $0                    # Environment variable"
            print_arch_help
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Main build logic
echo -e "${BLUE}Step 1: Detecting CUDA...${NC}"
if ! find_cuda; then
    echo ""
    echo -e "${YELLOW}Building without CUDA support (CPU only)${NC}"
    make -B -j"$JOBS"
    echo -e "${GREEN}Build complete (CPU only)${NC}"
    exit 0
fi

echo ""
echo -e "${BLUE}Step 2: Detecting GPU architecture...${NC}"
if [[ -z "$CUDA_ARCH" ]]; then
    if detect_gpu_arch; then
        CUDA_ARCH="$DETECTED_GPU_ARCH"
    else
        CUDA_ARCH="sm_75"
        echo -e "${YELLOW}Using default architecture: $CUDA_ARCH (Turing)${NC}"
    fi
else
    echo -e "${GREEN}Using specified architecture: $CUDA_ARCH${NC}"
fi

echo ""
echo -e "${BLUE}Step 3: Checking GCC compatibility...${NC}"
if ! find_compatible_gcc; then
    echo -e "${YELLOW}Continuing with system GCC and -allow-unsupported-compiler...${NC}"
fi

# Setup compiler bindir if needed
if [[ -n "$GCC_VERSION" ]]; then
    setup_ccbin
fi

# Build NVCC flags
NVCC_FLAGS="-O3 -std=c++17 -arch=$CUDA_ARCH -allow-unsupported-compiler -Isrc -DHAVE_CUDA_BACKEND=1"
if [[ -n "$CCBIN_DIR" ]]; then
    NVCC_FLAGS="$NVCC_FLAGS --compiler-bindir=$CCBIN_DIR -Xcompiler -U_GNU_SOURCE"
fi

# Handle GCC version mismatch between nvcc's compiler and the system compiler.
# If nvcc uses a different GCC (e.g., GCC 13 via ccbin) but the rest of the code
# is compiled by the system GCC (e.g., GCC 15), LTO bytecodes are incompatible
# and the linker will segfault. Solution: disable LTO when versions differ.
MAKE_CC_ARGS=()
if [[ -n "$GCC_VERSION" ]]; then
    sys_gcc_version=$(gcc -dumpversion 2>/dev/null | cut -d. -f1)
    cuda_gcc_version=$("$GCC_VERSION" -dumpversion 2>/dev/null | cut -d. -f1)
    if [[ "$sys_gcc_version" != "$cuda_gcc_version" ]]; then
        # Disable LTO and suppress truncation warnings that only appear without LTO
        # (with LTO, GCC can prove buffers are sufficient and suppresses these)
        MAKE_CC_ARGS=(LTO_FLAGS= EXTRA_DEFINES="-Wno-stringop-truncation -Wno-format-truncation")
        echo -e "${YELLOW}System GCC $sys_gcc_version != CUDA GCC $cuda_gcc_version — disabling LTO${NC}"
    fi
fi

echo ""
echo -e "${BLUE}Step 4: Building keyhunt with CUDA...${NC}"
echo -e "  CUDA_HOME:  $CUDA_HOME"
echo -e "  CUDA_ARCH:  $CUDA_ARCH"
echo -e "  NVCC_FLAGS: $NVCC_FLAGS"
echo -e "  Jobs:       $JOBS"
if [[ -n "$GCC_VERSION" ]]; then
    echo -e "  GCC:        $GCC_VERSION (used for all compilation)"
fi
echo ""

# Clean and rebuild
make clean 2>/dev/null || true

# Build with CUDA
make -B -j"$JOBS" \
    NVCC="$CUDA_HOME/bin/nvcc" \
    CUDA_HOME="$CUDA_HOME" \
    NVCCFLAGS="$NVCC_FLAGS" \
    "${MAKE_CC_ARGS[@]}"

# Verify build
echo ""
if [[ -x ./keyhunt ]]; then
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}Build successful!${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo ""
    echo -e "${BLUE}Binary info:${NC}"
    ls -lh keyhunt
    echo ""

    # Check CUDA linking
    if ldd keyhunt | grep -q libcudart; then
        echo -e "${GREEN}CUDA runtime linked successfully${NC}"
        ldd keyhunt | grep cuda
    else
        echo -e "${YELLOW}Warning: CUDA runtime not linked (GPU features may not work)${NC}"
    fi

    echo ""
    echo -e "${BLUE}Quick test:${NC}"
    timeout 5 ./keyhunt -m address -f tests/1to32.txt -r 1:FF -G auto 2>&1 | head -20 || true

    echo ""
    echo -e "${GREEN}========================================${NC}"
    echo -e "${BLUE}Usage examples:${NC}"
    echo "  # CPU-only search"
    echo "  ./keyhunt -m address -f targets.txt -b 66 -R"
    echo ""
    echo "  # GPU full mode (recommended)"
    echo "  ./keyhunt -m address -f targets.txt -b 66 -G full -R"
    echo ""
    echo "  # GPU hybrid mode (CPU + GPU combined)"
    echo "  ./keyhunt -m address -f targets.txt -b 66 -G hybrid -R"
    echo ""
    echo "  # Run benchmark"
    echo "  ./keyhunt --benchmark"
    echo -e "${GREEN}========================================${NC}"
else
    echo -e "${RED}========================================${NC}"
    echo -e "${RED}Build failed!${NC}"
    echo -e "${RED}========================================${NC}"
    echo ""
    echo -e "${YELLOW}Troubleshooting:${NC}"
    echo "  1. Check CUDA installation: nvcc --version"
    echo "  2. Check NVIDIA driver: nvidia-smi"
    echo "  3. Try with verbose: $0 --verbose"
    echo "  4. Check GCC version: gcc --version"
    echo ""
    echo "  If GCC is too new (>14), install an older version:"
    echo "    brew install gcc@13"
    echo "    sudo apt install gcc-13 g++-13"
    exit 1
fi
