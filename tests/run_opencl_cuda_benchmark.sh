#!/bin/bash
# OpenCL vs CUDA Performance Benchmark Runner
# This script automates the benchmark process for comparing OpenCL and CUDA backends

set -euo pipefail

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m' # No Color

# Default values
DURATION=30
OUTPUT_DIR="./tests/benchmark_results"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
RESULT_FILE="${OUTPUT_DIR}/benchmark_${TIMESTAMP}.txt"
RUN_AMD=false
RUN_NVIDIA=false
RUN_MIXED=false

# Function to print colored output
print_header() {
    echo -e "${CYAN}${BOLD}========================================${NC}"
    echo -e "${CYAN}${BOLD}$1${NC}"
    echo -e "${CYAN}${BOLD}========================================${NC}"
}

print_info() {
    echo -e "${BLUE}ℹ${NC} $1"
}

print_success() {
    echo -e "${GREEN}✓${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}⚠${NC} $1"
}

print_error() {
    echo -e "${RED}✗${NC} $1"
}

# Function to check if keyhunt binary exists
check_keyhunt() {
    if [ ! -x "./keyhunt" ]; then
        print_error "keyhunt binary not found or not executable"
        echo "Please build keyhunt first:"
        echo "  ./build_opencl.sh    # For OpenCL support"
        echo "  ./build_cuda.sh      # For CUDA support"
        echo "  make clean && make   # For both backends"
        exit 1
    fi
    print_success "keyhunt binary found"
}

# Function to detect available GPU backends
detect_backends() {
    print_header "Detecting GPU Backends"

    local gpu_list=$(./keyhunt -L 2>&1 || true)

    if echo "$gpu_list" | grep -qi "opencl"; then
        RUN_AMD=true
        print_success "OpenCL backend detected"
    else
        print_warning "OpenCL backend not detected"
    fi

    if echo "$gpu_list" | grep -qi "cuda"; then
        RUN_NVIDIA=true
        print_success "CUDA backend detected"
    else
        print_warning "CUDA backend not detected"
    fi

    if [ "$RUN_AMD" = true ] && [ "$RUN_NVIDIA" = true ]; then
        RUN_MIXED=true
        print_success "Mixed-vendor system detected (OpenCL + CUDA)"
    fi

    if [ "$RUN_AMD" = false ] && [ "$RUN_NVIDIA" = false ]; then
        print_error "No GPU backends detected. Please rebuild with GPU support."
        exit 1
    fi

    echo ""
}

# Function to collect system information
collect_system_info() {
    print_header "Collecting System Information"

    mkdir -p "$OUTPUT_DIR"

    {
        echo "==============================================================================="
        echo "KEYHUNT OPENCL vs CUDA BENCHMARK RESULTS"
        echo "==============================================================================="
        echo ""
        echo "Date: $(date +'%Y-%m-%d %H:%M:%S')"
        echo "Host: $(hostname)"
        echo "keyhunt Version: $(./keyhunt --version 2>&1 | head -1 || echo 'Unknown')"
        echo ""
        echo "==============================================================================="
        echo "SYSTEM CONFIGURATION"
        echo "==============================================================================="
        echo ""

        # CPU Info
        echo "CPU:"
        if [ -f /proc/cpuinfo ]; then
            echo "  Model: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | xargs)"
            echo "  Cores: $(grep -c '^processor' /proc/cpuinfo) logical"
            echo "  Physical: $(grep 'cpu cores' /proc/cpuinfo | head -1 | cut -d: -f2 | xargs || echo 'Unknown')"
        fi

        # RAM Info
        echo ""
        echo "RAM:"
        if [ -f /proc/meminfo ]; then
            echo "  Total: $(grep MemTotal /proc/meminfo | awk '{printf "%.1f GB", $2/1024/1024}')"
            echo "  Available: $(grep MemAvailable /proc/meminfo | awk '{printf "%.1f GB", $2/1024/1024}')"
        fi

        # GPU Info
        echo ""
        if [ "$RUN_AMD" = true ]; then
            echo "AMD GPU (OpenCL):"
            if command -v rocm-smi &> /dev/null; then
                rocm-smi --showproductname --showmeminfo --showvram 2>/dev/null || echo "  (rocm-smi not available)"
            elif command -v clinfo &> /dev/null; then
                clinfo | grep -A 3 "Device Name" | head -4 || echo "  (clinfo not available)"
            else
                echo "  Detected via ./keyhunt -L"
            fi
            echo ""
        fi

        if [ "$RUN_NVIDIA" = true ]; then
            echo "NVIDIA GPU (CUDA):"
            if command -v nvidia-smi &> /dev/null; then
                nvidia-smi --query-gpu=name,memory.total,compute_cap --format=csv,noheader 2>/dev/null || echo "  (nvidia-smi not available)"
            else
                echo "  Detected via ./keyhunt -L"
            fi
            echo ""
        fi

        # Software versions
        echo "Software:"
        if [ -f /etc/os-release ]; then
            echo "  OS: $(grep PRETTY_NAME /etc/os-release | cut -d= -f2 | tr -d '"')"
        else
            echo "  OS: $(uname -s) $(uname -r)"
        fi
        echo "  Kernel: $(uname -r)"
        if [ "$RUN_AMD" = true ] && [ -f /opt/rocm/.info/version ]; then
            echo "  ROCm: $(cat /opt/rocm/.info/version)"
        fi
        if [ "$RUN_NVIDIA" = true ] && command -v nvcc &> /dev/null; then
            echo "  CUDA: $(nvcc --version | grep release | awk '{print $5}' | tr -d ',')"
        fi
        echo "  GCC: $(gcc --version | head -1 | awk '{print $NF}')"
        echo ""

        # GPU Detection Output
        echo "GPU Detection Output (./keyhunt -L):"
        ./keyhunt -L 2>&1 || echo "Failed to enumerate GPUs"
        echo ""

    } | tee "$RESULT_FILE"

    print_success "System information collected"
    echo ""
}

# Function to run benchmark test
run_benchmark_test() {
    local test_name=$1
    local test_cmd=$2
    local backend=$3

    print_info "Running: $test_name ($backend)"

    {
        echo "==============================================================================="
        echo "TEST: $test_name - $backend"
        echo "==============================================================================="
        echo ""
        echo "Command: $test_cmd"
        echo ""
        echo "--- Output ---"
    } | tee -a "$RESULT_FILE"

    # Run the benchmark and capture output
    eval "$test_cmd" 2>&1 | tee -a "$RESULT_FILE" || {
        echo "" | tee -a "$RESULT_FILE"
        print_warning "Test failed or incomplete"
    }

    {
        echo ""
        echo "--- End of Test ---"
        echo ""
    } | tee -a "$RESULT_FILE"

    print_success "Test completed"
}

# Function to monitor GPU during benchmark
start_gpu_monitoring() {
    if [ "$RUN_AMD" = true ] && command -v rocm-smi &> /dev/null; then
        print_info "Starting AMD GPU monitoring..."
        rocm-smi --showuse --showpower --showtemp --csv > "${OUTPUT_DIR}/amd_gpu_monitor_${TIMESTAMP}.csv" 2>/dev/null &
        AMD_MONITOR_PID=$!
    fi

    if [ "$RUN_NVIDIA" = true ] && command -v nvidia-smi &> /dev/null; then
        print_info "Starting NVIDIA GPU monitoring..."
        nvidia-smi --query-gpu=timestamp,utilization.gpu,utilization.memory,power.draw,temperature.gpu --format=csv -l 1 > "${OUTPUT_DIR}/nvidia_gpu_monitor_${TIMESTAMP}.csv" 2>/dev/null &
        NVIDIA_MONITOR_PID=$!
    fi
}

# Function to stop GPU monitoring
stop_gpu_monitoring() {
    if [ -n "${AMD_MONITOR_PID:-}" ]; then
        kill $AMD_MONITOR_PID 2>/dev/null || true
        print_success "AMD GPU monitoring stopped"
    fi

    if [ -n "${NVIDIA_MONITOR_PID:-}" ]; then
        kill $NVIDIA_MONITOR_PID 2>/dev/null || true
        print_success "NVIDIA GPU monitoring stopped"
    fi
}

# Main benchmark execution
run_benchmarks() {
    print_header "Running Benchmarks"

    # Start GPU monitoring
    start_gpu_monitoring

    # Test 1: Hash-Only Mode
    if [ "$RUN_AMD" = true ]; then
        run_benchmark_test \
            "Hash-Only Mode" \
            "./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -G hash -s $DURATION -q" \
            "OpenCL (AMD)"
    fi

    if [ "$RUN_NVIDIA" = true ]; then
        run_benchmark_test \
            "Hash-Only Mode" \
            "./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -G hash -s $DURATION -q" \
            "CUDA (NVIDIA)"
    fi

    # Test 2: Full GPU Search Mode
    if [ "$RUN_AMD" = true ]; then
        run_benchmark_test \
            "Full GPU Search" \
            "./keyhunt -m address -f tests/66.txt -b 66 -G full -s $DURATION -R -q" \
            "OpenCL (AMD)"
    fi

    if [ "$RUN_NVIDIA" = true ]; then
        run_benchmark_test \
            "Full GPU Search" \
            "./keyhunt -m address -f tests/66.txt -b 66 -G full -s $DURATION -R -q" \
            "CUDA (NVIDIA)"
    fi

    # Test 3: Hybrid Mode
    local cpu_threads=$(nproc)

    if [ "$RUN_AMD" = true ]; then
        run_benchmark_test \
            "Hybrid CPU+GPU" \
            "./keyhunt -m address -f tests/66.txt -b 66 -G hybrid -t $cpu_threads -s $DURATION -R -q" \
            "OpenCL (AMD)"
    fi

    if [ "$RUN_NVIDIA" = true ]; then
        run_benchmark_test \
            "Hybrid CPU+GPU" \
            "./keyhunt -m address -f tests/66.txt -b 66 -G hybrid -t $cpu_threads -s $DURATION -R -q" \
            "CUDA (NVIDIA)"
    fi

    # Test 4: Mixed-Vendor (if both backends available)
    if [ "$RUN_MIXED" = true ]; then
        run_benchmark_test \
            "Mixed-Vendor Multi-GPU" \
            "./keyhunt -m address -f tests/66.txt -b 66 -G full -s $DURATION -R -q --all-gpus" \
            "Unified (CUDA + OpenCL)"
    fi

    # Stop GPU monitoring
    stop_gpu_monitoring

    print_success "All benchmarks completed"
    echo ""
}

# Function to analyze results
analyze_results() {
    print_header "Benchmark Analysis"

    {
        echo "==============================================================================="
        echo "PERFORMANCE SUMMARY"
        echo "==============================================================================="
        echo ""
        echo "Extracting performance metrics from test results..."
        echo ""

        # Try to extract throughput values (this is a simple grep, adjust as needed)
        if [ "$RUN_AMD" = true ]; then
            echo "AMD GPU (OpenCL) Performance:"
            grep -i "speed\|mkeys\|throughput" "$RESULT_FILE" | grep -i "opencl\|amd" | head -3 || echo "  (metrics not found in output)"
            echo ""
        fi

        if [ "$RUN_NVIDIA" = true ]; then
            echo "NVIDIA GPU (CUDA) Performance:"
            grep -i "speed\|mkeys\|throughput" "$RESULT_FILE" | grep -i "cuda\|nvidia" | head -3 || echo "  (metrics not found in output)"
            echo ""
        fi

        echo "For detailed analysis, review the full benchmark report:"
        echo "  $RESULT_FILE"
        echo ""
        echo "To calculate OpenCL efficiency:"
        echo "  Efficiency = (OpenCL Throughput / CUDA Throughput) × 100%"
        echo "  Acceptance Threshold: ≥ 50%"
        echo ""

    } | tee -a "$RESULT_FILE"
}

# Usage information
usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -d SECONDS    Duration for each benchmark test (default: 30)"
    echo "  -o DIR        Output directory for results (default: ./tests/benchmark_results)"
    echo "  -h            Show this help message"
    echo ""
    echo "Example:"
    echo "  $0 -d 60                    # Run 60-second benchmarks"
    echo "  $0 -o /tmp/bench            # Save results to /tmp/bench"
    echo ""
}

# Parse command-line arguments
while getopts "d:o:h" opt; do
    case $opt in
        d)
            DURATION=$OPTARG
            ;;
        o)
            OUTPUT_DIR=$OPTARG
            RESULT_FILE="${OUTPUT_DIR}/benchmark_${TIMESTAMP}.txt"
            ;;
        h)
            usage
            exit 0
            ;;
        *)
            usage
            exit 1
            ;;
    esac
done

# Main execution
main() {
    print_header "Keyhunt OpenCL vs CUDA Benchmark Runner"
    echo ""

    check_keyhunt
    detect_backends
    collect_system_info
    run_benchmarks
    analyze_results

    print_header "Benchmark Complete"
    echo ""
    print_success "Results saved to: $RESULT_FILE"
    echo ""
    print_info "Next steps:"
    echo "  1. Review the benchmark results file"
    echo "  2. Fill in the template: tests/opencl_cuda_benchmark_results.template.txt"
    echo "  3. Calculate OpenCL efficiency and verify ≥50% threshold"
    echo "  4. Document any issues or observations"
    echo ""
    print_info "For detailed benchmarking guide, see:"
    echo "  tests/OPENCL_BENCHMARK_GUIDE.md"
    echo ""
}

# Run main function
main
