# CPU Tuning Guide

This guide covers optimizing keyhunt for maximum CPU performance.

## SIMD Optimization

Keyhunt uses SIMD (Single Instruction Multiple Data) to process multiple keys in parallel.

### SIMD Tiers

| Feature | Parallel Keys | Speed Boost | CPUs |
|---------|---------------|-------------|------|
| Scalar | 1 | Baseline | All |
| SSE2 | 4 | ~4x | Pentium 4+ (2001) |
| AVX2 | 8 | ~8x | Haswell+ (2013) |
| AVX-512 | 16 | ~16x | Skylake-X+ (2017) |

### Detection

Keyhunt auto-detects SIMD at startup:

```
[+] CPU Features:
    SSE2: Yes
    AVX2: Yes
    AVX-512: No
    SHA-NI: Yes
[+] Using: AVX2 8-way parallel hashing
```

### Verify Your CPU

```bash
# Check SIMD support
grep -E 'sse2|avx2|avx512' /proc/cpuinfo | head -1

# Detailed CPU info
lscpu
```

### SHA-NI Acceleration

Modern CPUs (AMD Zen, Intel Ice Lake+) have hardware SHA256:

```
[+] SHA-NI: Enabled (hardware SHA256)
```

This accelerates the SHA256 step by ~3-5x.

## Thread Configuration

### Optimal Thread Count

Use all logical threads:

```bash
./keyhunt -m address -f target.txt -b 66 -t $(nproc)
```

Or specify explicitly:

```bash
./keyhunt -m address -f target.txt -b 66 -t 32
```

### Physical vs Logical Cores

| Core Type | Example | Recommendation |
|-----------|---------|----------------|
| Physical | 16 cores | Good baseline |
| Logical (HT) | 32 threads | Use all, ~20% boost |

Hyperthreading provides ~20% additional throughput.

### Over-subscription Warning

Using more threads than logical cores hurts performance:

```
[!] Warning: 64 threads requested but only 32 available
[+] Adjusted to 32 threads
```

## Batch Size Optimization

Batch size controls how many keys are processed per iteration.

### Recommended Values

| SIMD Level | Recommended Batch | Reason |
|------------|-------------------|--------|
| SSE2 | 1024 | Cache aligned, 4-way |
| AVX2 | 1024 | Cache aligned, 8-way |
| AVX-512 | 2048 | Larger vectors |

### Cache Alignment

Batch size should be:
- Multiple of 64 (cache line size)
- Multiple of SIMD width (8 for AVX2)

1024 = 64 * 16 = 8 * 128 (optimal)

### Setting Batch Size

```bash
./keyhunt -m address -f target.txt -b 66 --batch-size 1024
```

## CPU Frequency Scaling

### Disable Power Saving

For maximum performance, use "performance" governor:

```bash
# Check current governor
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor

# Set performance mode (temporary)
sudo cpupower frequency-set -g performance

# Verify
cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
```

### Persistent Setting

Create `/etc/systemd/system/cpupower.service`:

```ini
[Unit]
Description=CPU Performance Governor
After=multi-user.target

[Service]
Type=oneshot
ExecStart=/usr/bin/cpupower frequency-set -g performance
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
```

Enable:
```bash
sudo systemctl enable cpupower
sudo systemctl start cpupower
```

## NUMA Optimization

For multi-socket servers (2+ CPUs):

### Check NUMA Topology

```bash
numactl --hardware
```

### Bind to NUMA Node

```bash
# Run on NUMA node 0 only
numactl --cpunodebind=0 --membind=0 ./keyhunt -m address -f target.txt -b 66

# Run on NUMA node 1 only
numactl --cpunodebind=1 --membind=1 ./keyhunt -m address -f target.txt -b 66
```

### Multiple Instances

For optimal NUMA performance, run separate instances per node:

```bash
# Terminal 1 - Node 0
numactl --cpunodebind=0 --membind=0 ./keyhunt ... -t 16

# Terminal 2 - Node 1
numactl --cpunodebind=1 --membind=1 ./keyhunt ... -t 16
```

## Thermal Management

### Monitor Temperature

```bash
# Install sensors
sudo apt install lm-sensors
sudo sensors-detect

# Monitor
watch sensors
```

### Thermal Throttling Signs

- Speed fluctuates significantly
- Speed drops after initial burst
- CPU temperature > 90C

### Solutions

1. Improve cooling
2. Reduce thread count slightly
3. Reduce CPU frequency limit

## Memory Bandwidth

### Check Memory Speed

```bash
sudo dmidecode -t memory | grep -E "Speed|Size"
```

### Optimize for Memory-Bound Workloads

BSGS mode is memory-bound. Ensure:
- Dual-channel or better memory configuration
- Fastest supported memory speed
- Sufficient RAM to avoid swap

## Process Priority

### Nice Level

Run at higher priority:

```bash
sudo nice -n -10 ./keyhunt -m address -f target.txt -b 66
```

Nice values:
- -20: Highest priority
- 0: Normal (default)
- 19: Lowest priority

### Real-time Priority (Advanced)

```bash
sudo chrt -f 50 ./keyhunt -m address -f target.txt -b 66
```

Warning: Can make system unresponsive if keyhunt uses all CPU.

## Isolating CPU Cores

Reserve cores for keyhunt using isolcpus:

### Kernel Parameter

Add to `/etc/default/grub`:

```
GRUB_CMDLINE_LINUX="isolcpus=16-31"
```

Update and reboot:
```bash
sudo update-grub
sudo reboot
```

### Use Isolated Cores

```bash
taskset -c 16-31 ./keyhunt -m address -f target.txt -b 66 -t 16
```

## Performance Verification

### Run Benchmark

```bash
./keyhunt --benchmark
```

Example output:
```
CPU Benchmark Results:
┌──────────────┬─────────────────┬───────────────┐
│ Threads      │ Speed (Mkeys/s) │ Efficiency    │
├──────────────┼─────────────────┼───────────────┤
│ 1            │ 5.2             │ 100%          │
│ 4            │ 20.1            │ 97%           │
│ 8            │ 39.8            │ 96%           │
│ 16           │ 78.2            │ 94%           │
│ 32           │ 92.4            │ 56% (HT)      │
└──────────────┴─────────────────┴───────────────┘

Recommendation: Use 32 threads for 92.4 Mkeys/s
```

### Monitor During Search

```bash
# CPU usage
htop

# Per-core frequency
watch -n 1 "cat /proc/cpuinfo | grep MHz"

# Temperature
watch -n 1 sensors
```

## Troubleshooting

### Low Speed Despite Good CPU

1. Check SIMD detection at startup
2. Verify not thermal throttling
3. Check power governor
4. Ensure built with optimizations (`make`, not `make legacy`)

### Speed Drops Over Time

1. Check thermal throttling
2. Check for background processes
3. Verify power settings

### Inconsistent Speed

1. Check for power state transitions
2. Disable turbo boost for consistency:
   ```bash
   echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo
   ```

## See Also

- [GPU Setup](gpu-setup.md) - GPU acceleration
- [Hybrid Mode](hybrid-mode.md) - CPU+GPU parallel
- [Memory Optimization](memory-optimization.md) - BSGS memory tuning
