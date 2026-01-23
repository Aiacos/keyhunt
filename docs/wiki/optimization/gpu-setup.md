# GPU Setup Guide

This guide covers configuring NVIDIA GPU acceleration for keyhunt.

## Requirements

### Hardware
- NVIDIA GPU with Compute Capability 5.0+ (Maxwell or newer)
- Minimum: GTX 900 series
- Recommended: RTX 2060+, RTX 3060+, RTX 4060+

### Software
- NVIDIA Driver 450.0+ (525+ recommended)
- CUDA Toolkit 11.0+ (12.x recommended)

## Checking GPU Support

### Verify GPU

```bash
nvidia-smi
```

Expected output:
```
+-----------------------------------------------------------------------------+
| NVIDIA-SMI 525.116.04   Driver Version: 525.116.04   CUDA Version: 12.0    |
|-------------------------------+----------------------+----------------------+
| GPU  Name        Persistence-M| Bus-Id        Disp.A | Volatile Uncorr. ECC |
| Fan  Temp  Perf  Pwr:Usage/Cap|         Memory-Usage | GPU-Util  Compute M. |
|===============================+======================+======================|
|   0  NVIDIA GeForce ...  Off  | 00000000:01:00.0  On |                  N/A |
| 30%   35C    P8    15W / 320W |    512MiB / 10240MiB |      0%      Default |
+-------------------------------+----------------------+----------------------+
```

### Verify CUDA

```bash
nvcc --version
```

Expected output:
```
nvcc: NVIDIA (R) Cuda compiler driver
Copyright (c) 2005-2024 NVIDIA Corporation
Built on ...
Cuda compilation tools, release 12.6, V12.6.85
```

## Installing CUDA

### Ubuntu/Debian

```bash
# Add NVIDIA repository
wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2204/x86_64/cuda-keyring_1.0-1_all.deb
sudo dpkg -i cuda-keyring_1.0-1_all.deb

# Install CUDA
sudo apt update
sudo apt install cuda-toolkit-12-6

# Add to PATH
echo 'export PATH=/usr/local/cuda/bin:$PATH' >> ~/.bashrc
echo 'export LD_LIBRARY_PATH=/usr/local/cuda/lib64:$LD_LIBRARY_PATH' >> ~/.bashrc
source ~/.bashrc
```

### Fedora

```bash
# Enable RPM Fusion (if not already)
sudo dnf install https://download1.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm

# Install NVIDIA driver
sudo dnf install akmod-nvidia

# Install CUDA toolkit
sudo dnf config-manager --add-repo https://developer.download.nvidia.com/compute/cuda/repos/fedora39/x86_64/cuda-fedora39.repo
sudo dnf install cuda-toolkit-12-6

# Add to PATH
echo 'export PATH=/usr/local/cuda/bin:$PATH' >> ~/.bashrc
echo 'export LD_LIBRARY_PATH=/usr/local/cuda/lib64:$LD_LIBRARY_PATH' >> ~/.bashrc
source ~/.bashrc
```

### Manual Installation (Any Linux)

Download from NVIDIA:
1. Visit https://developer.nvidia.com/cuda-downloads
2. Select your OS, architecture, and distribution
3. Download the `.run` file
4. Install:

```bash
sudo sh cuda_12.6.0_xxx.xxx_linux.run
```

## Building with GPU Support

### Automatic Build (Recommended)

The `build_cuda.sh` script handles everything:

```bash
./build_cuda.sh
```

Features:
- Auto-detects CUDA installation
- Auto-detects GPU architecture
- Handles GCC compatibility issues
- Provides clear error messages

### Manual Build

```bash
make clean
make NVCC=/usr/local/cuda/bin/nvcc \
     CUDA_HOME=/usr/local/cuda \
     CUDA_ARCH=sm_75
```

### Verify CUDA is Linked

```bash
ldd keyhunt | grep cuda
```

Should show:
```
libcudart.so.12 => /usr/local/cuda/lib64/libcudart.so.12 (0x...)
```

## GPU Modes

Keyhunt supports several GPU modes:

```bash
./keyhunt -m address -f target.txt -b 66 -G <mode>
```

| Mode | Description | CPU Load | GPU Load | Best For |
|------|-------------|----------|----------|----------|
| `off` | CPU only | 100% | 0% | No GPU / debugging |
| `auto` | Auto-detect | Varies | Varies | Default |
| `hash` | GPU hashing | High | Medium | Weak GPU |
| `full` | GPU everything | Low | 100% | Strong GPU |
| `hybrid` | CPU + GPU | 100% | 100% | Maximum throughput |

### Full Mode (Recommended for Modern GPUs)

GPU handles entire pipeline: key generation through bloom check.

```bash
./keyhunt -m address -f target.txt -b 66 -G full -l compress
```

- CPU: Minimal work (result collection)
- GPU: Everything (ECC + SHA256 + RIPEMD160 + matching)
- Best when: Strong GPU, many targets

### Hybrid Mode (Maximum Throughput)

CPU and GPU work in parallel on different key ranges.

```bash
./keyhunt -m address -f target.txt -b 66 -G hybrid -l compress
```

- CPU: Full pipeline on subset of range
- GPU: Full pipeline on different subset
- Best when: Want maximum throughput

### Hash Mode (Legacy)

GPU handles SHA256 + RIPEMD160 hashing only.

```bash
./keyhunt -m address -f target.txt -b 66 -G hash
```

- CPU: Key generation, bloom filter checks
- GPU: Hash computation
- Best when: GPU is slower than CPU for full pipeline

## Performance Benchmarking

Run the built-in benchmark:

```bash
./keyhunt --benchmark
```

Example output:
```
GPU Benchmark: NVIDIA RTX 3080
+------------------+----------------+-----------------+
| Mode             | Speed          | Efficiency      |
+------------------+----------------+-----------------+
| hash             | 180 Mkeys/s    | 45%             |
| full             | 520 Mkeys/s    | 85%             |
| hybrid           | 612 Mkeys/s    | 100%            |
+------------------+----------------+-----------------+

Recommendation: Use hybrid mode for 612 Mkeys/s
```

## GPU Architecture Selection

Select the correct architecture for your GPU:

| Architecture | GPUs | Code |
|--------------|------|------|
| Maxwell | GTX 900 series | `sm_50` |
| Pascal | GTX 1000 series | `sm_60`/`sm_61` |
| Volta | V100, Titan V | `sm_70` |
| Turing | RTX 2000 series | `sm_75` |
| Ampere | RTX 3000 series | `sm_86` |
| Ampere | A100 | `sm_80` |
| Ada | RTX 4000 series | `sm_89` |
| Hopper | H100 | `sm_90` |

Build with specific architecture:

```bash
./build_cuda.sh --arch sm_86    # RTX 3000 series
./build_cuda.sh --arch sm_89    # RTX 4000 series
```

## Runtime Tuning

### Environment Variables

```bash
# Blocks per SM (4-64)
export KEYHUNT_GPU_BLOCKS_PER_SM=32

# Keys per thread (64-65536)
export KEYHUNT_GPU_KEYS_PER_THREAD=2048

# Enable auto-tuning
export KEYHUNT_GPU_AUTOTUNE=1
```

### Monitor GPU Usage

```bash
watch -n 1 nvidia-smi
```

Or detailed monitoring:

```bash
nvidia-smi dmon -s u
```

## Multi-GPU Setup

### List GPUs

```bash
nvidia-smi -L
```

### Select GPU

```bash
# Use GPU 0
CUDA_VISIBLE_DEVICES=0 ./keyhunt -m address -f target.txt -b 66 -G full

# Use GPU 1
CUDA_VISIBLE_DEVICES=1 ./keyhunt -m address -f target.txt -b 66 -G full
```

### Use All GPUs

Run multiple instances:

```bash
# Terminal 1 - GPU 0
CUDA_VISIBLE_DEVICES=0 ./keyhunt ... --client-name gpu0

# Terminal 2 - GPU 1
CUDA_VISIBLE_DEVICES=1 ./keyhunt ... --client-name gpu1
```

Or use distributed mode with multiple GPU workers.

## Troubleshooting

### "CUDA not found"

```bash
# Check CUDA installation
ls /usr/local/cuda

# Verify PATH
echo $PATH | grep cuda
echo $LD_LIBRARY_PATH | grep cuda

# Add to environment
export PATH=/usr/local/cuda/bin:$PATH
export LD_LIBRARY_PATH=/usr/local/cuda/lib64:$LD_LIBRARY_PATH
```

### "GPU compute capability too low"

Your GPU is too old. Minimum required: Compute Capability 5.0

Check your GPU:
```bash
nvidia-smi --query-gpu=compute_cap --format=csv
```

### GCC Version Too New

CUDA may not support the latest GCC. Solutions:

1. Use `build_cuda.sh` (handles automatically)
2. Install older GCC:
   ```bash
   # Homebrew
   brew install gcc@13

   # Ubuntu
   sudo apt install gcc-13 g++-13

   # Fedora
   sudo dnf install gcc13 gcc13-c++
   ```

### GPU Crashes or Hangs

1. **Reduce workload**:
   ```bash
   export KEYHUNT_GPU_BLOCKS_PER_SM=16
   export KEYHUNT_GPU_KEYS_PER_THREAD=512
   ```

2. **Check power limit**:
   ```bash
   nvidia-smi -q -d POWER
   ```

3. **Check temperature**:
   ```bash
   nvidia-smi --query-gpu=temperature.gpu --format=csv -l 1
   ```

4. **Reduce power limit** (if overheating):
   ```bash
   sudo nvidia-smi -pl 250  # Set to 250W
   ```

### Slow GPU Performance

1. **Check GPU utilization**:
   ```bash
   nvidia-smi dmon -s u
   ```

2. **Check PCIe bandwidth**:
   ```bash
   nvidia-smi --query-gpu=pcie.link.gen.current,pcie.link.width.current --format=csv
   ```

3. **Disable display use** (if GPU is driving display):
   Use a different GPU for display or run headless.

### Driver Issues

```bash
# Check driver
nvidia-smi

# If fails, reinstall driver (Ubuntu)
sudo apt remove --purge nvidia-*
sudo apt install nvidia-driver-525
sudo reboot

# Fedora
sudo dnf remove akmod-nvidia
sudo dnf install akmod-nvidia
sudo reboot
```

## Power Efficiency

### Limit Power for Efficiency

```bash
# Query current power limit
nvidia-smi -q -d POWER

# Set power limit (reduces heat, minimal speed impact)
sudo nvidia-smi -pl 200  # 200W limit
```

### Memory Clock Boost (Advanced)

Memory clock often helps more than core clock:

```bash
# Check current clocks
nvidia-smi -q -d CLOCK

# Increase memory clock (example, adjust for your GPU)
sudo nvidia-settings -a "[gpu:0]/GPUMemoryTransferRateOffset[3]=1000"
```

Warning: Overclocking may cause instability.

## Cloud GPU Setup

### AWS EC2 (P3/P4 instances)

```bash
# Install NVIDIA driver
sudo apt install nvidia-driver-525

# Install CUDA
wget https://developer.download.nvidia.com/compute/cuda/12.6.0/local_installers/cuda_12.6.0_xxx.xx.xx_linux.run
sudo sh cuda_12.6.0_xxx.xx.xx_linux.run

# Verify
nvidia-smi
```

### Google Cloud (GPU VMs)

```bash
# NVIDIA driver auto-installed on GPU VMs
# Just install CUDA toolkit
sudo apt install cuda-toolkit-12-6
```

### Vast.ai / RunPod

Most cloud GPU providers have CUDA pre-installed. Just:

```bash
git clone https://github.com/albertobsd/keyhunt.git
cd keyhunt
./build_cuda.sh
```

## See Also

- [CPU Tuning](cpu-tuning.md) - CPU optimization
- [Hybrid Mode](hybrid-mode.md) - CPU+GPU parallel
- [Memory Optimization](memory-optimization.md) - BSGS memory management
- [GPU Backend Documentation](../../GPU_BACKEND.md) - Technical details
