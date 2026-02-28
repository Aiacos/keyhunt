#!/bin/bash
#
# Build script for keyhunt with OpenCL GPU support (AMD ROCm)
# Handles ROCm detection and OpenCL configuration automatically
#
# Supported ROCm versions: 5.0+
# Supported GPU architectures: gfx900 (Vega) through gfx1100 (RDNA3)
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

echo -e "${BLUE}=== Keyhunt OpenCL Build Script ===${NC}"
echo -e "${CYAN}Version 1.0 - Auto-detection for AMD ROCm${NC}"
echo ""

# Default values
ROCM_HOME="${ROCM_HOME:-/opt/rocm}"
GPU_ARCH="${GPU_ARCH:-}"
JOBS="${JOBS:-$(nproc)}"
VERBOSE=0
DETECTED_GPU_ARCH=""
OPENCL_FOUND=0

# Print verbose message
verbose() {
    if [[ $VERBOSE -eq 1 ]]; then
        echo -e "${CYAN}[DEBUG] $1${NC}"
    fi
}

# Detect AMD GPU architecture automatically
detect_gpu_arch() {
    # Try rocminfo first (most reliable)
    if command -v rocminfo &> /dev/null; then
        verbose "Using rocminfo for GPU detection"
        local gfx_name
        gfx_name=$(rocminfo 2>/dev/null | grep -oP "Name:\s+\Kgfx\w+" | head -1)

        if [[ -n "$gfx_name" ]]; then
            DETECTED_GPU_ARCH="$gfx_name"
            local gpu_name
            gpu_name=$(rocminfo 2>/dev/null | grep "Marketing Name:" | head -1 | sed 's/.*Marketing Name:\s*//')
            echo -e "${GREEN}Detected GPU: $gpu_name ($DETECTED_GPU_ARCH)${NC}"
            return 0
        fi
    fi

    # Try rocm-smi as fallback
    if command -v rocm-smi &> /dev/null; then
        verbose "Using rocm-smi for GPU detection"
        local gpu_name
        gpu_name=$(rocm-smi --showproductname 2>/dev/null | grep "GPU" | head -1 | awk '{$1=""; print $0}' | xargs)

        if [[ -n "$gpu_name" ]]; then
            # Map common GPU names to gfx architectures
            case "$gpu_name" in
                *"Vega"*|*"Radeon VII"*)
                    DETECTED_GPU_ARCH="gfx906"
                    ;;
                *"5700"*|*"5600"*)
                    DETECTED_GPU_ARCH="gfx1010"
                    ;;
                *"6900"*|*"6800"*|*"6700"*)
                    DETECTED_GPU_ARCH="gfx1030"
                    ;;
                *"6600"*|*"6500"*)
                    DETECTED_GPU_ARCH="gfx1032"
                    ;;
                *"7900"*|*"7800"*|*"7700"*|*"7600"*)
                    DETECTED_GPU_ARCH="gfx1100"
                    ;;
                *)
                    echo -e "${YELLOW}Unknown GPU model: $gpu_name${NC}"
                    return 1
                    ;;
            esac
            echo -e "${GREEN}Detected GPU: $gpu_name ($DETECTED_GPU_ARCH)${NC}"
            return 0
        fi
    fi

    # Try clinfo as last resort
    if command -v clinfo &> /dev/null; then
        verbose "Using clinfo for GPU detection"
        local device_name
        device_name=$(clinfo 2>/dev/null | grep "Device Name" | grep -i "AMD" | head -1 | sed 's/.*Device Name\s*//')
        if [[ -n "$device_name" ]]; then
            echo -e "${YELLOW}Detected AMD device via OpenCL: $device_name${NC}"
            echo -e "${YELLOW}Could not determine exact architecture, will use generic settings${NC}"
            DETECTED_GPU_ARCH="generic"
            return 0
        fi
    fi

    echo -e "${YELLOW}Could not detect AMD GPU architecture${NC}"
    return 1
}

# Try to find ROCm in common locations
find_rocm() {
    local rocm_paths=(
        "$ROCM_HOME"
        "/opt/rocm"
        "/opt/rocm-6.2.0"
        "/opt/rocm-6.1.0"
        "/opt/rocm-6.0.0"
        "/opt/rocm-5.7.0"
        "/opt/rocm-5.6.0"
        "/opt/rocm-5.5.0"
        "/usr/local/rocm"
    )

    for path in "${rocm_paths[@]}"; do
        if [[ -d "$path" && -d "$path/opencl" ]]; then
            ROCM_HOME="$path"
            local rocm_version
            if [[ -f "$path/.info/version" ]]; then
                rocm_version=$(cat "$path/.info/version" 2>/dev/null)
            else
                rocm_version="unknown"
            fi
            echo -e "${GREEN}Found ROCm $rocm_version at: $ROCM_HOME${NC}"
            return 0
        fi
    done

    # Check for system-wide OpenCL without ROCm
    if [[ -f "/usr/include/CL/cl.h" ]] || [[ -f "/usr/include/CL/opencl.h" ]]; then
        echo -e "${GREEN}Found system OpenCL headers (non-ROCm)${NC}"
        ROCM_HOME=""
        return 0
    fi

    echo -e "${YELLOW}ROCm not found in standard locations${NC}"
    echo -e "${YELLOW}Searched: /opt/rocm*, /usr/local/rocm${NC}"
    echo ""
    echo -e "${BLUE}To install ROCm:${NC}"
    echo "  Ubuntu/Debian: https://rocm.docs.amd.com/projects/install-on-linux/en/latest/how-to/native-install/ubuntu.html"
    echo "  Fedora:        https://rocm.docs.amd.com/projects/install-on-linux/en/latest/how-to/native-install/rhel.html"
    echo ""
    echo -e "${BLUE}Or install generic OpenCL:${NC}"
    echo "  Ubuntu/Debian: sudo apt install ocl-icd-opencl-dev opencl-headers"
    echo "  Fedora:        sudo dnf install ocl-icd-devel opencl-headers"
    return 1
}

# Check for OpenCL SDK/headers
check_opencl() {
    local opencl_include_paths=(
        "$ROCM_HOME/include"
        "/usr/include"
        "/usr/local/include"
    )

    for path in "${opencl_include_paths[@]}"; do
        if [[ -f "$path/CL/cl.h" ]] || [[ -f "$path/CL/opencl.h" ]]; then
            echo -e "${GREEN}Found OpenCL headers in: $path${NC}"
            OPENCL_FOUND=1
            return 0
        fi
    done

    echo -e "${RED}OpenCL headers not found!${NC}"
    echo -e "${YELLOW}Install OpenCL development files:${NC}"
    echo "  Ubuntu/Debian: sudo apt install ocl-icd-opencl-dev opencl-headers"
    echo "  Fedora:        sudo dnf install ocl-icd-devel opencl-headers"
    return 1
}

# Cleanup
cleanup() {
    # Placeholder for future cleanup tasks
    :
}
trap cleanup EXIT

# Print GPU architecture reference
print_arch_help() {
    echo ""
    echo -e "${BLUE}AMD GPU Architecture Reference:${NC}"
    echo "  gfx900  - Vega 10      (Vega 56/64, Radeon VII)"
    echo "  gfx906  - Vega 20      (Radeon VII, MI50/MI60)"
    echo "  gfx1010 - RDNA 1       (RX 5000 series)"
    echo "  gfx1030 - RDNA 2       (RX 6900/6800/6700 XT)"
    echo "  gfx1032 - RDNA 2       (RX 6600/6500 XT)"
    echo "  gfx1100 - RDNA 3       (RX 7900/7800/7700/7600 XT)"
    echo "  gfx1101 - RDNA 3       (Mobile GPUs)"
    echo ""
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --rocm-home)
            ROCM_HOME="$2"
            shift 2
            ;;
        --arch)
            GPU_ARCH="$2"
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
            echo "Build keyhunt with OpenCL GPU support for AMD GPUs. Automatically"
            echo "detects ROCm installation and GPU architecture."
            echo ""
            echo "Options:"
            echo "  --rocm-home PATH   Path to ROCm (default: auto-detect)"
            echo "  --arch ARCH        GPU architecture (default: auto-detect)"
            echo "  --jobs N           Parallel jobs (default: $(nproc))"
            echo "  --verbose, -v      Show detailed build information"
            echo "  --clean            Clean build artifacts and exit"
            echo "  --list-arch        Show GPU architecture reference"
            echo "  --help, -h         Show this help"
            echo ""
            echo "Environment variables:"
            echo "  ROCM_HOME          Alternative to --rocm-home"
            echo "  GPU_ARCH           Alternative to --arch"
            echo "  JOBS               Alternative to --jobs"
            echo ""
            echo "Examples:"
            echo "  $0                                # Auto-detect everything"
            echo "  $0 --rocm-home /opt/rocm-6.2.0   # Specify ROCm path"
            echo "  $0 --arch gfx1030                # For RX 6900 XT"
            echo "  $0 --arch gfx1100                # For RX 7900 XTX"
            echo "  GPU_ARCH=gfx1030 $0              # Environment variable"
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
echo -e "${BLUE}Step 1: Detecting ROCm/OpenCL...${NC}"
if ! find_rocm; then
    echo ""
    echo -e "${YELLOW}Building without GPU support (CPU only)${NC}"
    make -B -j"$JOBS"
    echo -e "${GREEN}Build complete (CPU only)${NC}"
    exit 0
fi

echo ""
echo -e "${BLUE}Step 2: Checking OpenCL headers...${NC}"
if ! check_opencl; then
    echo ""
    echo -e "${RED}Cannot build with OpenCL - missing headers${NC}"
    exit 1
fi

echo ""
echo -e "${BLUE}Step 3: Detecting GPU architecture...${NC}"
if [[ -z "$GPU_ARCH" ]]; then
    if detect_gpu_arch; then
        GPU_ARCH="$DETECTED_GPU_ARCH"
    else
        GPU_ARCH="generic"
        echo -e "${YELLOW}Using generic OpenCL settings${NC}"
    fi
else
    echo -e "${GREEN}Using specified architecture: $GPU_ARCH${NC}"
fi

# Build compiler flags for OpenCL
OPENCL_FLAGS="-DHAVE_OPENCL_BACKEND=1"
OPENCL_LIBS="-lOpenCL"
OPENCL_INCLUDES=""

if [[ -n "$ROCM_HOME" ]]; then
    OPENCL_INCLUDES="-I$ROCM_HOME/include"
    OPENCL_LIBS="-L$ROCM_HOME/lib -lOpenCL"
fi

if [[ "$GPU_ARCH" != "generic" ]]; then
    OPENCL_FLAGS="$OPENCL_FLAGS -DGPU_ARCH=$GPU_ARCH"
fi

echo ""
echo -e "${BLUE}Step 4: Building keyhunt with OpenCL...${NC}"
echo -e "  ROCM_HOME:      ${ROCM_HOME:-system}"
echo -e "  GPU_ARCH:       $GPU_ARCH"
echo -e "  OPENCL_FLAGS:   $OPENCL_FLAGS"
echo -e "  OPENCL_LIBS:    $OPENCL_LIBS"
echo -e "  OPENCL_INCLUDES: $OPENCL_INCLUDES"
echo -e "  Jobs:           $JOBS"
echo ""

# Clean and rebuild
make clean 2>/dev/null || true

# Build with OpenCL
CXXFLAGS="$OPENCL_FLAGS $OPENCL_INCLUDES" \
LDFLAGS="$OPENCL_LIBS" \
make -B -j"$JOBS"

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

    # Check OpenCL linking
    if ldd keyhunt 2>/dev/null | grep -q libOpenCL; then
        echo -e "${GREEN}OpenCL runtime linked successfully${NC}"
        ldd keyhunt | grep -i opencl
    else
        echo -e "${YELLOW}Warning: OpenCL runtime not linked (GPU features may not work)${NC}"
    fi

    echo ""
    echo -e "${BLUE}Detected OpenCL devices:${NC}"
    if command -v clinfo &> /dev/null; then
        clinfo 2>/dev/null | grep -A 3 "Platform Name\|Device Name" | head -20 || true
    elif command -v rocminfo &> /dev/null; then
        rocminfo 2>/dev/null | grep -E "Name:|Marketing Name:" | head -10 || true
    else
        echo -e "${YELLOW}Install clinfo or rocminfo to list OpenCL devices${NC}"
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
    echo "  # GPU full mode (recommended for AMD)"
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
    echo "  1. Check ROCm installation: ls /opt/rocm"
    echo "  2. Check AMD GPU: rocm-smi or rocminfo"
    echo "  3. Check OpenCL: clinfo"
    echo "  4. Try with verbose: $0 --verbose"
    echo ""
    echo "  Install ROCm from: https://rocm.docs.amd.com/"
    echo "  Or install generic OpenCL: apt install ocl-icd-opencl-dev"
    exit 1
fi
