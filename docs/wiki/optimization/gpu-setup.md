# GPU Setup Guide

This guide covers configuring NVIDIA GPU acceleration for keyhunt.

## Requirements

### Hardware
- NVIDIA GPU with Compute Capability 5.0+ (Maxwell or newer)
- Recommended: GTX 1060+, RTX 2060+, RTX 3060+

### Software
- NVIDIA Driver 450.0+
- CUDA Toolkit 11.0+

## Checking GPU Support

### Verify GPU

```bash
nvidia-smi
```

Expected output:
```
+-----------------------------------------------------------------------------+
| NVIDIA-SMI 525.116.04   Driver Version: 525.116.04   CUDA Version: 12.0     |
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
Copyright (c) 2005-2022 NVIDIA Corporation
Built on ...
Cuda compilation tools, release 12.0, V12.0.0
```

## Installing CUDA

### Ubuntu/Debian

```bash
# Add NVIDIA repository
wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2204/x86_64/cuda-keyring_1.0-1_all.deb
sudo dpkg -i cuda-keyring_1.0-1_all.deb

# Install CUDA
sudo apt update
sudo apt install cuda-toolkit-12-0

# Add to PATH
echo 'export PATH=/usr/local/cuda/bin:$PATH' >> ~/.bashrc
echo 'export LD_LIBRARY_PATH=/usr/local/cuda/lib64:$LD_LIBRARY_PATH' >> ~/.bashrc
source ~/.bashrc
```

### Fedora/RHEL

```bash
sudo dnf config-manager --add-repo https://developer.download.nvidia.com/compute/cuda/repos/rhel8/x86_64/cuda-rhel8.repo
sudo dnf install cuda-toolkit-12-0
```

## Building with GPU Support

```bash
make clean
make gpu
```

Or with specific CUDA path:

```bash
make gpu CUDA_PATH=/usr/local/cuda-12.0
```

## GPU Modes

Keyhunt supports several GPU modes:

```bash
./keyhunt -m address -f target.txt -b 66 -G <mode>
```

| Mode | Description | CPU Load | GPU Load | Best For |
|------|-------------|----------|----------|----------|
| `off` | CPU only | 100% | 0% | No GPU / debugging |
| `auto` | Auto-detect | Varies | Varies | Unknown workload |
| `hash` | GPU hashing | High | Medium | Weak GPU |
| `full` | GPU everything | Low | 100% | Strong GPU |
| `hybrid` | CPU + GPU | 100% | 100% | Maximum throughput |

### Mode Details

#### hash Mode

GPU handles SHA256 + RIPEMD160 hashing only.

```bash
./keyhunt -m address -f target.txt -b 66 -G hash
```

- CPU: Key generation, bloom filter checks
- GPU: Hash computation
- Best when: GPU is slower than CPU for full pipeline

#### full Mode

GPU handles entire pipeline: key generation through bloom check.

```bash
./keyhunt -m address -f target.txt -b 66 -G full
```

- CPU: Minimal work (result collection)
- GPU: Everything
- Best when: Strong GPU, many targets

#### hybrid Mode

CPU and GPU work in parallel on different key ranges.

```bash
./keyhunt -m address -f target.txt -b 66 -G hybrid
```

- CPU: Full pipeline on subset of range
- GPU: Full pipeline on different subset
- Best when: Want maximum throughput

## Performance Optimization

### GPU Thread Configuration

```bash
./keyhunt -m address -f target.txt -b 66 -G full --gpu-threads 256 --gpu-blocks 1024
```

| Parameter | Description | Default |
|-----------|-------------|---------|
| `--gpu-threads` | Threads per block | 256 |
| `--gpu-blocks` | Number of blocks | Auto |

### Finding Optimal Settings

Run benchmark:

```bash
./keyhunt --benchmark
```

Example output:
```
GPU Benchmark: NVIDIA RTX 3080
┌──────────────┬──────────────┬─────────────────┐
│ Mode         │ Speed        │ Efficiency      │
├──────────────┼──────────────┼─────────────────┤
│ hash         │ 180 Mkeys/s  │ 45%             │
│ full         │ 320 Mkeys/s  │ 80%             │
│ hybrid       │ 412 Mkeys/s  │ 100%            │
└──────────────┴──────────────┴─────────────────┘

Recommendation: Use hybrid mode for 412 Mkeys/s
```

### GPU Memory Usage

Monitor during operation:

```bash
watch -n 1 nvidia-smi
```

Reduce memory if needed:

```bash
./keyhunt ... -G full --gpu-blocks 512
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

### GPU Crashes or Hangs

1. **Reduce workload**:
   ```bash
   ./keyhunt ... -G full --gpu-blocks 256 --gpu-threads 128
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

   Low utilization suggests bottleneck elsewhere.

2. **Check PCIe bandwidth**:
   ```bash
   nvidia-smi --query-gpu=pcie.link.gen.current,pcie.link.width.current --format=csv
   ```

   Should be Gen3 x16 or better.

3. **Disable display use** (if GPU is driving display):
   Use a different GPU for display or run headless.

### Driver Issues

```bash
# Check driver
nvidia-smi

# If fails, reinstall driver
sudo apt remove --purge nvidia-*
sudo apt install nvidia-driver-525
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

### Overclock Memory (Advanced)

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
wget https://developer.download.nvidia.com/compute/cuda/12.0.0/local_installers/cuda_12.0.0_525.60.13_linux.run
sudo sh cuda_12.0.0_525.60.13_linux.run

# Verify
nvidia-smi
```

### Google Cloud (GPU VMs)

```bash
# NVIDIA driver auto-installed on GPU VMs
# Just install CUDA toolkit
sudo apt install cuda-toolkit-12-0
```

## See Also

- [CPU Tuning](cpu-tuning.md) - CPU optimization
- [Hybrid Mode](hybrid-mode.md) - CPU+GPU parallel
- [Memory Optimization](memory-optimization.md) - BSGS memory management
