# Hybrid Mode Guide

Hybrid mode combines CPU and GPU for maximum search throughput by running both in parallel on different portions of the key range.

## Overview

| Aspect | Details |
|--------|---------|
| Command | `-G hybrid` |
| CPU Usage | 100% (all threads) |
| GPU Usage | 100% |
| Best For | Maximum throughput when both CPU and GPU are available |

## How It Works

```
                    Search Range
    ┌─────────────────────────────────────────────┐
    │                                             │
    │  ┌─────────────────┐  ┌─────────────────┐   │
    │  │   CPU Search    │  │   GPU Search    │   │
    │  │                 │  │                 │   │
    │  │  Threads: 16    │  │  CUDA Cores     │   │
    │  │  ~90 Mkeys/s    │  │  ~320 Mkeys/s   │   │
    │  └─────────────────┘  └─────────────────┘   │
    │                                             │
    └─────────────────────────────────────────────┘
              Combined: ~410 Mkeys/s
```

CPU and GPU work on non-overlapping portions of the range simultaneously.

## Enabling Hybrid Mode

```bash
./keyhunt -m address -f target.txt -b 66 -G hybrid -t 16
```

## Configuration

### Thread Count

Specify CPU threads (GPU has separate configuration):

```bash
./keyhunt -m address -f target.txt -b 66 -G hybrid -t $(nproc)
```

### GPU Parameters

```bash
./keyhunt -m address -f target.txt -b 66 -G hybrid \
  --gpu-threads 256 --gpu-blocks 1024
```

### Work Distribution

By default, work is distributed based on relative performance. Auto-tuning adjusts distribution during runtime.

## Performance Expectations

### Ideal Scenario

When CPU and GPU are both fully utilized:

```
CPU Speed: 90 Mkeys/s
GPU Speed: 320 Mkeys/s
Hybrid Speed: 410 Mkeys/s (CPU + GPU)
```

### Real-World Factors

Actual hybrid performance depends on:

1. **PCIe bandwidth**: Data transfer between CPU and GPU
2. **Memory bandwidth**: Both compete for system memory
3. **Coordination overhead**: Synchronizing results

Typical efficiency: 85-95% of theoretical maximum.

## When to Use Hybrid Mode

### Use Hybrid When:
- You have both capable CPU and GPU
- Want maximum throughput
- Power consumption is not a concern

### Use GPU-Only When:
- GPU is much faster than CPU (>5x)
- CPU is needed for other tasks
- Want simpler configuration

### Use CPU-Only When:
- No GPU available
- GPU is slow/old
- GPU is busy with other work

## Benchmark Comparison

Run benchmark to compare modes:

```bash
./keyhunt --benchmark
```

Example output:
```
Performance Comparison:
┌──────────────┬─────────────────┬───────────────┐
│ Mode         │ Speed (Mkeys/s) │ Power (W)     │
├──────────────┼─────────────────┼───────────────┤
│ CPU only     │ 92              │ 125           │
│ GPU hash     │ 180             │ 180           │
│ GPU full     │ 324             │ 250           │
│ Hybrid       │ 412             │ 350           │
└──────────────┴─────────────────┴───────────────┘

Efficiency (Keys per Watt):
  CPU only: 0.74 Mkeys/W
  GPU full: 1.30 Mkeys/W
  Hybrid:   1.18 Mkeys/W

Recommendation for maximum speed: Hybrid mode
Recommendation for efficiency: GPU full mode
```

## Load Balancing

### Automatic Balancing

Keyhunt automatically adjusts work distribution based on observed speeds:

```
[+] Initial distribution: CPU 20%, GPU 80%
[+] After calibration: CPU 18%, GPU 82%
```

### Manual Distribution

Force specific distribution:

```bash
./keyhunt ... -G hybrid --cpu-ratio 0.2
```

Sets CPU to handle 20% of work, GPU handles 80%.

## Monitoring Hybrid Operation

### Runtime Output

```
[+] Mode: HYBRID
[+] CPU: 16 threads @ 92.4 Mkeys/s
[+] GPU: RTX 3080 @ 324.5 Mkeys/s
[+] Combined: 416.9 Mkeys/s
[+] Progress: [████████░░░░░░░░░░░░] 42.3%
```

### System Monitoring

```bash
# Watch both CPU and GPU
htop &
watch -n 1 nvidia-smi
```

## Troubleshooting

### Low Combined Speed

**Symptom**: Hybrid slower than GPU alone

**Causes**:
1. Memory bandwidth saturation
2. PCIe bottleneck
3. Thermal throttling

**Solutions**:
1. Reduce CPU threads: `-t 8` instead of `-t 16`
2. Check PCIe generation: `nvidia-smi --query-gpu=pcie.link.gen.current --format=csv`
3. Monitor temperatures: `sensors` and `nvidia-smi`

### GPU Underutilized in Hybrid

**Symptom**: GPU utilization drops in hybrid mode

**Causes**:
1. Work distribution favors CPU too much
2. Memory contention

**Solutions**:
1. Adjust ratio: `--cpu-ratio 0.1`
2. Reduce CPU threads

### CPU Underutilized in Hybrid

**Symptom**: CPU utilization drops

**Causes**:
1. Work distribution favors GPU too much
2. GPU work finishing faster than CPU batch

**Solutions**:
1. Adjust ratio: `--cpu-ratio 0.3`
2. Increase CPU batch size

## Multi-GPU Hybrid

For systems with multiple GPUs:

```bash
# GPU 0 + GPU 1 + CPU
CUDA_VISIBLE_DEVICES=0,1 ./keyhunt -m address -f target.txt -b 66 -G hybrid -t 16
```

Or run separate processes:

```bash
# Terminal 1: GPU 0 + half CPU
CUDA_VISIBLE_DEVICES=0 taskset -c 0-15 ./keyhunt ... -G hybrid -t 16

# Terminal 2: GPU 1 + half CPU
CUDA_VISIBLE_DEVICES=1 taskset -c 16-31 ./keyhunt ... -G hybrid -t 16
```

## Power Considerations

Hybrid mode uses maximum power:

| Component | Typical Power |
|-----------|---------------|
| CPU (full load) | 125W |
| GPU (RTX 3080) | 320W |
| System (total) | 500W+ |

For 24/7 operation, consider:
- Power costs
- Cooling capacity
- Component longevity

## See Also

- [CPU Tuning](cpu-tuning.md) - CPU optimization
- [GPU Setup](gpu-setup.md) - GPU configuration
- [Memory Optimization](memory-optimization.md) - BSGS memory
