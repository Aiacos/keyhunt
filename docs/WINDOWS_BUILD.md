# Windows Build and Installation Guide

## Overview

This guide covers building **keyhunt** for Windows (64-bit) using three different methods:

1. **MinGW-w64 Cross-Compilation** (from Linux) - Recommended for CI/CD and automated builds
2. **Native MSVC Build** (on Windows) - Recommended for native Windows development
3. **CUDA GPU Support** (on Windows) - For GPU-accelerated searches

**Platform Support:**
- Windows 10 (64-bit) or later
- Windows 11 (64-bit)
- Windows Server 2019/2022

**CPU Requirements:**
- x86-64 (AMD64/Intel 64) architecture
- SSE2 support (standard on all 64-bit CPUs)
- AVX2 recommended for optimal performance

## Quick Start

### For Windows Users (Native Build)

**Prerequisites:**
- Visual Studio 2019 or 2022 with "Desktop development with C++" workload
- Windows 10 SDK or Windows 11 SDK

**Build:**
```cmd
build_windows.bat
```

**Run:**
```cmd
keyhunt.exe --help
keyhunt.exe -m address -f tests\1to32.txt -t 4
```

### For Linux Users (Cross-Compile)

**Prerequisites:**
```bash
# Ubuntu/Debian
sudo apt install mingw-w64

# Fedora
sudo dnf install mingw64-gcc mingw64-gcc-c++

# Arch
sudo pacman -S mingw-w64-gcc
```

**Build:**
```bash
./build_windows_mingw.sh
```

**Test with Wine:**
```bash
wine64 keyhunt.exe --help
```

## Method 1: MinGW-w64 Cross-Compilation (Linux → Windows)

### What is Cross-Compilation?

Cross-compilation allows you to build Windows executables on Linux without needing a Windows machine. The resulting `.exe` files run natively on Windows.

### Prerequisites

Install MinGW-w64 toolchain:

| Distribution | Command |
|--------------|---------|
| **Ubuntu/Debian** | `sudo apt update && sudo apt install mingw-w64` |
| **Fedora** | `sudo dnf install mingw64-gcc mingw64-gcc-c++` |
| **Arch** | `sudo pacman -S mingw-w64-gcc` |
| **openSUSE** | `sudo zypper install mingw64-cross-gcc mingw64-cross-gcc-c++` |

**Optional:** Install Wine to test Windows executables on Linux:
```bash
# Ubuntu/Debian
sudo apt install wine64

# Fedora
sudo dnf install wine
```

### Build Script (Recommended)

The automated build script handles all configuration:

```bash
# Basic build
./build_windows_mingw.sh

# Clean build
./build_windows_mingw.sh --clean

# Build with verbose output
./build_windows_mingw.sh --verbose

# Build and run tests with Wine
./build_windows_mingw.sh --run-tests

# Show all options
./build_windows_mingw.sh --help
```

**Script features:**
- Auto-detects MinGW-w64 toolchain
- Auto-detects Wine for testing
- Parallel compilation (`-j` flag)
- Clean build option
- Test suite execution

### Manual Build with Makefile

If you prefer manual control:

```bash
# Standard build
make CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++

# Parallel build (faster)
make -j$(nproc) CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++

# Clean build artifacts
make clean

# Legacy build (requires MinGW OpenSSL/GMP libraries)
make legacy CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++
```

### Output

Successful build produces:
- `keyhunt.exe` - Main executable (Windows 64-bit PE32+)
- Size: ~2-4 MB (depending on optimization level)
- Dependencies: None (statically linked)

### Testing on Linux with Wine

```bash
# Show help
wine64 keyhunt.exe --help

# Run basic test
wine64 keyhunt.exe -m address -f tests/1to32.txt -r 1:FFFFFFFF

# Run BSGS test
wine64 keyhunt.exe -m bsgs -f tests/125.txt -b 125 -q -s 10 -R

# Benchmark
wine64 keyhunt.exe --benchmark
```

**Note:** Wine emulates Windows API calls, so performance will be lower than native Windows execution.

### Troubleshooting Cross-Compilation

#### Error: `mingw32-gcc: command not found`

**Solution:** Install MinGW-w64 toolchain (see Prerequisites section).

#### Error: Undefined reference to `pthread_create`

**Solution:** The Makefile automatically links `-lws2_32 -lbcrypt` for Windows. Ensure you're using the cross-compilation flags:
```bash
make CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++
```

#### Error: Wine cannot execute binary

**Solution:** Install 64-bit Wine (`wine64`), not 32-bit Wine:
```bash
sudo apt install wine64  # Ubuntu/Debian
```

#### Error: File format not recognized

**Cause:** Accidentally built Linux binary instead of Windows binary.

**Solution:** Clean and rebuild with correct compiler:
```bash
make clean
make CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++
```

Verify output is Windows executable:
```bash
file keyhunt.exe
# Should output: PE32+ executable (console) x86-64, for MS Windows
```

## Method 2: Native MSVC Build (Windows)

### What is MSVC?

MSVC (Microsoft Visual C++) is Microsoft's official C/C++ compiler for Windows. It provides the best integration with Windows APIs and debugging tools.

### Prerequisites

#### Visual Studio Installation

Download and install **Visual Studio 2019** or **Visual Studio 2022** (Community Edition is free):
- Download: https://visualstudio.microsoft.com/downloads/

**Required Workload:**
- ✅ Desktop development with C++

**Required Components:**
- ✅ MSVC v142 or v143 - VS 2019/2022 C++ x64/x86 build tools (latest)
- ✅ Windows 10 SDK or Windows 11 SDK
- ✅ C++ CMake tools for Windows (optional, but recommended)

**Installation size:** ~5-10 GB (depending on components)

### Build Script (Recommended)

Open **Command Prompt** or **PowerShell** in the project directory:

```cmd
REM Basic build
build_windows.bat

REM Clean build
build_windows.bat --clean

REM Debug build
build_windows.bat --debug

REM Verbose output
build_windows.bat --verbose

REM Show help
build_windows.bat --help
```

**Script features:**
- Auto-detects Visual Studio installation (2019 or 2022)
- Configures MSVC environment automatically
- Compiles with AVX2 optimizations
- Links Windows libraries (ws2_32.lib, bcrypt.lib)

### Manual Build with MSVC

If you prefer manual control, open **Developer Command Prompt for VS 2022** (or 2019):

```cmd
REM Set up environment (if not using Developer Command Prompt)
"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64

REM Create output directories
mkdir obj obj\base58 obj\rmd160 obj\xxhash obj\core obj\platform obj\gpu
mkdir obj\bloom obj\hash obj\sha3 obj\bsgs obj\secp256k1 obj\search
mkdir obj\hybrid obj\util obj\distributed obj\wizard obj\sort obj\crypto obj\io

REM Compile C files
cl.exe /nologo /O2 /W3 /Isrc /D_CRT_SECURE_NO_WARNINGS /c src\base58\base58.c /Foobj\base58\base58.obj
cl.exe /nologo /O2 /W3 /Isrc /D_CRT_SECURE_NO_WARNINGS /c src\rmd160\rmd160.c /Foobj\rmd160\rmd160.obj
REM ... (compile all source files)

REM Compile C++ files with AVX2 optimizations
cl.exe /nologo /O2 /std:c++17 /W3 /Isrc /D_CRT_SECURE_NO_WARNINGS /arch:AVX2 /c src\hash\ripemd160_avx2.cpp /Foobj\hash\ripemd160_avx2.obj
REM ... (compile all C++ files)

REM Link executable
link.exe /NOLOGO /MACHINE:X64 /OUT:keyhunt.exe obj\*.obj obj\**\*.obj ws2_32.lib bcrypt.lib
```

**For complete compilation steps, see `build_windows.bat` script.**

### Build Output

Successful build produces:
- `keyhunt.exe` - Main executable
- Size: ~3-5 MB (Release build)
- Dependencies: Windows runtime libraries (included in Windows 10/11)

### Verification

Test the build:
```cmd
REM Show version and help
keyhunt.exe --help

REM Run quick functionality test
keyhunt.exe -m address -f tests\1to32.txt -t 4 -r 1:FFFFFFFF

REM Run BSGS test
keyhunt.exe -m bsgs -f tests\125.txt -b 125 -q -s 10 -R

REM Run benchmark
keyhunt.exe --benchmark
```

### Troubleshooting MSVC Build

#### Error: `'cl.exe' is not recognized as an internal or external command`

**Cause:** MSVC environment not set up or Visual Studio not installed.

**Solution:**
1. Verify Visual Studio installation:
   - Open "Visual Studio Installer"
   - Ensure "Desktop development with C++" is installed
2. Use **Developer Command Prompt for VS 2022** (search in Start Menu)
3. Or run `vcvarsall.bat` to set up environment:
   ```cmd
   "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
   ```

#### Error: `Visual Studio not found`

**Cause:** Visual Studio not installed at standard location.

**Solution:**
1. Install Visual Studio 2019 or 2022 Community Edition
2. Or edit `build_windows.bat` to specify custom installation path:
   ```cmd
   set "VSINSTALL=C:\path\to\Visual Studio"
   ```

#### Error: `LNK2001: unresolved external symbol`

**Cause:** Missing library or object file.

**Solution:**
1. Ensure all source files are compiled (check `obj\` directory)
2. Verify Windows libraries are linked: `ws2_32.lib`, `bcrypt.lib`
3. Rebuild from clean state:
   ```cmd
   build_windows.bat --clean
   ```

#### Error: `fatal error C1083: Cannot open include file`

**Cause:** Missing Windows SDK or incorrect include paths.

**Solution:**
1. Install Windows 10 SDK or Windows 11 SDK via Visual Studio Installer
2. Verify SDK installation:
   ```cmd
   dir "C:\Program Files (x86)\Windows Kits\10\Include"
   ```

#### Warning: `C4819: The file contains a character that cannot be represented`

**Cause:** Source file encoding issue (rare on Windows).

**Solution:** Can be safely ignored, or compile with `/utf-8` flag:
```cmd
set CXXFLAGS=%CXXFLAGS% /utf-8
```

## Method 3: CUDA GPU Support (Windows)

### Prerequisites

#### Hardware Requirements

- **GPU:** NVIDIA GPU with compute capability 5.0 or higher
  - Maxwell (GTX 900 series) or newer
  - Recommended: Turing (RTX 2000), Ampere (RTX 3000), or Ada (RTX 4000)
- **VRAM:** 4 GB minimum, 8 GB+ recommended

#### Software Requirements

1. **Visual Studio 2019 or 2022** with C++ Desktop Development (see Method 2)
2. **NVIDIA CUDA Toolkit 11.0 or later**
   - Download: https://developer.nvidia.com/cuda-downloads
   - Recommended: CUDA 12.4 or 12.6
   - Installation size: ~3-4 GB

3. **NVIDIA GPU Driver**
   - Download: https://www.nvidia.com/Download/index.aspx
   - Version: 450.00 or later (for CUDA 11.0+)

### GPU Architecture Reference

| GPU Series | Architecture | Compute Capability | Flag |
|------------|--------------|-------------------|------|
| GTX 900 | Maxwell | 5.0 | `sm_50` |
| GTX 1000 | Pascal | 6.1 | `sm_61` |
| RTX 2000, GTX 1660 | Turing | 7.5 | `sm_75` |
| RTX 3000 | Ampere | 8.6 | `sm_86` |
| RTX 4000 | Ada | 8.9 | `sm_89` |
| H100 | Hopper | 9.0 | `sm_90` |

### CUDA Build Script (Recommended)

The automated script detects CUDA, Visual Studio, and GPU architecture automatically:

```cmd
REM Auto-detect everything
build_windows_cuda.bat

REM Specify GPU architecture manually
build_windows_cuda.bat --arch sm_86

REM Specify CUDA installation path
build_windows_cuda.bat --cuda-home "C:\cuda-12.6"

REM Clean build with verbose output
build_windows_cuda.bat --clean --verbose

REM Show GPU architecture reference
build_windows_cuda.bat --list-arch

REM Show help
build_windows_cuda.bat --help
```

**Script features:**
- Auto-detects CUDA Toolkit (searches common paths)
- Auto-detects GPU architecture via `nvidia-smi`
- Auto-detects Visual Studio 2019/2022
- Compiles CUDA kernels with `nvcc`
- Links with CUDA runtime (`cudart.lib`)

### Manual CUDA Build

For advanced users or custom configurations:

```cmd
REM Set up Visual Studio environment
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64

REM Set CUDA paths
set "CUDA_HOME=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.6"
set "PATH=%CUDA_HOME%\bin;%PATH%"
set "INCLUDE=%CUDA_HOME%\include;%INCLUDE%"
set "LIB=%CUDA_HOME%\lib\x64;%LIB%"

REM Compile CUDA kernel
nvcc.exe -O3 -std=c++17 -arch=sm_75 --compiler-options /EHsc,/W3,/nologo ^
    -DHAVE_CUDA_BACKEND=1 -Isrc -c src\gpu\gpu_backend_cuda.cu ^
    -o obj\gpu\gpu_backend_cuda.obj

REM Compile CPU code with CUDA flag
cl.exe /O2 /std:c++17 /W3 /Isrc /D_CRT_SECURE_NO_WARNINGS /DHAVE_CUDA_BACKEND=1 ^
    /c src\keyhunt.cpp /Foobj\keyhunt.obj

REM Link with CUDA runtime
link.exe /NOLOGO /MACHINE:X64 /OUT:keyhunt.exe obj\*.obj obj\**\*.obj ^
    ws2_32.lib bcrypt.lib cudart.lib /LIBPATH:"%CUDA_HOME%\lib\x64"
```

### Using GPU Acceleration

After building with CUDA support, use the `-G` flag:

```cmd
REM Disable GPU (CPU only)
keyhunt.exe -G off -m address -f targets.txt -t 8

REM Auto-detect best mode (default)
keyhunt.exe -G auto -m address -f targets.txt

REM Hash-only GPU mode (~50-100 Mkeys/s)
keyhunt.exe -G hash -m address -f targets.txt -l compress

REM Full GPU mode (~320-330 Mkeys/s on RTX 2080)
keyhunt.exe -G full -m address -f targets.txt -l compress

REM Hybrid mode (GPU + CPU, ~400+ Mkeys/s)
keyhunt.exe -G hybrid -m address -f targets.txt -l compress -t 4
```

### GPU Performance Benchmarks

| Mode | RTX 2080 SUPER | RTX 3070 | RTX 4070 |
|------|----------------|----------|----------|
| CPU only (-G off) | 5-10 Mkeys/s | 5-10 Mkeys/s | 5-10 Mkeys/s |
| GPU hash (-G hash) | 50-100 Mkeys/s | 80-150 Mkeys/s | 100-200 Mkeys/s |
| GPU full (-G full) | 320-330 Mkeys/s | 450-500 Mkeys/s | 600-700 Mkeys/s |
| Hybrid (-G hybrid) | 400+ Mkeys/s | 550+ Mkeys/s | 750+ Mkeys/s |

**Note:** Performance varies based on search mode, target count, and system configuration.

### Troubleshooting CUDA Build

#### Error: `CUDA Toolkit not found`

**Cause:** CUDA not installed or not in standard location.

**Solution:**
1. Install CUDA Toolkit: https://developer.nvidia.com/cuda-downloads
2. Or specify custom path:
   ```cmd
   build_windows_cuda.bat --cuda-home "C:\path\to\cuda"
   ```
3. Verify installation:
   ```cmd
   "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.6\bin\nvcc.exe" --version
   ```

#### Error: `nvidia-smi: command not found`

**Cause:** NVIDIA GPU driver not installed or outdated.

**Solution:**
1. Install latest NVIDIA driver: https://www.nvidia.com/Download/index.aspx
2. Verify installation:
   ```cmd
   nvidia-smi
   ```
   Should show GPU name and CUDA version.

#### Error: `nvcc fatal: Unsupported gpu architecture 'compute_XX'`

**Cause:** Specified architecture not supported by installed CUDA version.

**Solution:**
1. Check GPU compute capability: `nvidia-smi --query-gpu=compute_cap --format=csv`
2. Specify compatible architecture:
   ```cmd
   build_windows_cuda.bat --arch sm_75
   ```

#### Error: `cudaGetDeviceCount returned 0`

**Cause:** No NVIDIA GPU detected or driver issue.

**Solution:**
1. Verify GPU is installed and enabled in Device Manager
2. Update NVIDIA driver
3. Reboot system
4. Check CUDA installation:
   ```cmd
   "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.6\extras\demo_suite\deviceQuery.exe"
   ```

#### Runtime Error: `cudart64_12X.dll not found`

**Cause:** CUDA runtime DLL not found in PATH.

**Solution:**
1. Add CUDA bin directory to PATH:
   ```cmd
   set "PATH=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.6\bin;%PATH%"
   ```
2. Or copy DLL to executable directory:
   ```cmd
   copy "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.6\bin\cudart64_126.dll" .
   ```

## Installation and Deployment

### Running on Windows

After building or cross-compiling, copy `keyhunt.exe` to your Windows machine.

**No installation required** - keyhunt is a standalone executable.

#### Quick Test

```cmd
REM Download or copy executable
REM No installation needed!

REM Show help
keyhunt.exe --help

REM Run quick test
keyhunt.exe -m address -f tests\1to32.txt -t 4

REM Run benchmark
keyhunt.exe --benchmark
```

### System Requirements

| Component | Minimum | Recommended |
|-----------|---------|-------------|
| **OS** | Windows 10 (64-bit) | Windows 11 (64-bit) |
| **CPU** | x86-64 with SSE2 | x86-64 with AVX2 or AVX-512 |
| **RAM** | 4 GB | 16 GB+ (32 GB+ for BSGS mode) |
| **GPU** | None (CPU-only works) | NVIDIA RTX 2000+ with 8 GB VRAM |
| **Storage** | 100 MB | 1 GB+ (for BSGS cache files) |

### Performance Tuning

#### CPU Optimization

```cmd
REM Auto-tune thread count (recommended)
keyhunt.exe -m address -f targets.txt

REM Manual thread count
keyhunt.exe -m address -f targets.txt -t 8

REM Force AVX2 (if supported)
set KEYHUNT_FORCE_AVX2=1
keyhunt.exe -m address -f targets.txt
```

#### Memory Optimization (BSGS Mode)

```cmd
REM Auto-tune N and K parameters
keyhunt.exe -m bsgs -f targets.txt -b 125 -R

REM Manual N (sqrt of range)
keyhunt.exe -m bsgs -f targets.txt -b 125 -n 36893488147419103232 -k 1024

REM Save bloom filters to disk (reuse across runs)
keyhunt.exe -m bsgs -f targets.txt -b 125 -S -R
```

For detailed performance tuning, see [PARAMETER_VALIDATION.md](PARAMETER_VALIDATION.md).

## Common Use Cases

### Address Mode (Unknown Public Key)

```cmd
REM Search for Bitcoin addresses (compressed)
keyhunt.exe -m address -f addresses.txt -l compress -R -q -t 8

REM Search with GPU acceleration
keyhunt.exe -m address -f addresses.txt -l compress -G full

REM Search specific bit range
keyhunt.exe -m address -f addresses.txt -b 66 -R -t 8
```

### BSGS Mode (Known Public Key)

```cmd
REM Auto-tune BSGS parameters
keyhunt.exe -m bsgs -f pubkeys.txt -b 125 -R -q -s 10

REM Manual optimization
keyhunt.exe -m bsgs -f pubkeys.txt -b 125 -n 36893488147419103232 -k 1024 -R

REM Save bloom filters for reuse
keyhunt.exe -m bsgs -f pubkeys.txt -b 125 -S -R -q
```

### Distributed Mode (Multi-Machine)

```cmd
REM Server (coordinator + worker)
keyhunt.exe --wizard
REM Select: "Server mode"

REM Client (worker only, on other machines)
keyhunt.exe --wizard
REM Select: "Client mode"
REM Enter server IP and port
```

For detailed distributed setup, see [docs/wiki/distributed/overview.md](docs/wiki/distributed/overview.md).

## Testing

### Basic Functionality Tests

```cmd
REM Test 1: Address mode (quick)
keyhunt.exe -m address -f tests\1to32.txt -r 1:FFFFFFFF
REM Expected: Find keys in 1-32 bit range

REM Test 2: BSGS mode
keyhunt.exe -m bsgs -f tests\125.txt -b 125 -q -s 10 -R
REM Expected: Search range and complete successfully

REM Test 3: RMD160 mode
keyhunt.exe -m rmd160 -f tests\66.rmd -b 66 -l compress -R -q
REM Expected: Find key matching RIPEMD160 hash

REM Test 4: XPoint mode (fastest for known pubkeys)
keyhunt.exe -m xpoint -f tests\120.txt -t 4 -b 125 -R -q
REM Expected: Find key from X-coordinate
```

### GPU Tests (CUDA Build Only)

```cmd
REM Test GPU detection
keyhunt.exe --benchmark

REM Test GPU modes
keyhunt.exe -G hash -m address -f tests\1to32.txt -r 1:FFFFFFFF
keyhunt.exe -G full -m address -f tests\1to32.txt -r 1:FFFFFFFF
keyhunt.exe -G hybrid -m address -f tests\1to32.txt -r 1:FFFFFFFF -t 4
```

### Performance Benchmark

```cmd
REM Run comprehensive benchmark
keyhunt.exe --benchmark

REM Expected output:
REM   - CPU mode: 5-10 Mkeys/s per thread
REM   - GPU hash mode: 50-100 Mkeys/s (if CUDA)
REM   - GPU full mode: 300-700 Mkeys/s (if CUDA)
REM   - Hybrid mode: GPU + CPU combined
```

## Advanced Topics

### Building Legacy Version

The legacy version uses OpenSSL and GMP libraries for cryptographic operations.

**Not recommended** - the main version is faster and has no external dependencies.

For MinGW cross-compilation (requires MinGW OpenSSL/GMP):
```bash
make legacy CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++
```

For MSVC (requires vcpkg or manual OpenSSL/GMP installation):
```cmd
REM Install dependencies with vcpkg
vcpkg install openssl:x64-windows gmp:x64-windows

REM Build
build_windows.bat --legacy
```

### Custom Compiler Flags

For advanced users who want to customize optimization flags:

**MinGW:**
```bash
make CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++ \
     CXXFLAGS="-O3 -march=haswell -mavx2 -mfma"
```

**MSVC:**
Edit `build_windows.bat` and modify:
```cmd
set CXXFLAGS=/O2 /GL /arch:AVX2 /fp:fast
```

### Debug Build

For debugging issues or development:

**MSVC:**
```cmd
build_windows.bat --debug
```

**MinGW:**
```bash
make CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++ \
     CXXFLAGS="-g -O0" LDFLAGS="-g"
```

## Troubleshooting General Issues

### Application Won't Start

**Symptoms:** Double-clicking `keyhunt.exe` shows nothing or error message.

**Solutions:**
1. Open Command Prompt and run `keyhunt.exe --help` to see error messages
2. Check Windows Event Viewer (Application log) for crash details
3. Ensure Windows 10/11 64-bit (not 32-bit)
4. Run Windows Update to get latest runtime libraries
5. Try compatibility mode (right-click → Properties → Compatibility)

### Slow Performance

**Symptoms:** Lower keys/sec than expected.

**Solutions:**
1. Run benchmark: `keyhunt.exe --benchmark`
2. Enable AVX2 if supported: `set KEYHUNT_FORCE_AVX2=1`
3. Increase thread count: `keyhunt.exe -t 16` (match CPU cores)
4. Use GPU if available: `keyhunt.exe -G full`
5. Close background applications
6. Disable Windows Defender real-time scanning temporarily

### Out of Memory (BSGS Mode)

**Symptoms:** Crash or error "Cannot allocate memory".

**Solutions:**
1. Use auto-tuning: `keyhunt.exe -m bsgs -f targets.txt -b 125 -R`
2. Reduce N parameter: `keyhunt.exe -m bsgs -f targets.txt -n 1000000000 -k 256`
3. Close other applications to free RAM
4. Use disk caching: `keyhunt.exe -m bsgs -f targets.txt -S -R`

### Antivirus False Positive

**Symptoms:** Antivirus quarantines or blocks `keyhunt.exe`.

**Cause:** Some antivirus software flags keyhunt as potentially unwanted due to its nature (cryptographic key search).

**Solutions:**
1. Add exclusion in Windows Defender:
   - Settings → Update & Security → Windows Security → Virus & threat protection
   - Manage settings → Exclusions → Add exclusion → Folder → Select keyhunt directory
2. Verify executable integrity (check SHA256 hash)
3. Build from source to ensure clean binary
4. Temporarily disable antivirus during testing

## See Also

- **[README.md](../README.md)** - User documentation and usage examples
- **[PLATFORM_ABSTRACTION.md](PLATFORM_ABSTRACTION.md)** - Cross-platform architecture details
- **[GPU_BACKEND.md](GPU_BACKEND.md)** - GPU acceleration documentation
- **[PARAMETER_VALIDATION.md](PARAMETER_VALIDATION.md)** - Auto-tuning and parameter optimization
- **[docs/wiki/getting-started/installation.md](docs/wiki/getting-started/installation.md)** - General installation guide
- **[docs/wiki/optimization/cpu-tuning.md](docs/wiki/optimization/cpu-tuning.md)** - CPU performance optimization
- **[docs/wiki/optimization/gpu-setup.md](docs/wiki/optimization/gpu-setup.md)** - GPU setup and tuning

## References

### Windows Build Tools

- [Visual Studio Downloads](https://visualstudio.microsoft.com/downloads/) - MSVC compiler
- [Windows SDK](https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/) - Windows API headers
- [MinGW-w64](https://www.mingw-w64.org/) - GCC for Windows

### CUDA Resources

- [CUDA Toolkit Download](https://developer.nvidia.com/cuda-downloads) - NVIDIA CUDA
- [CUDA Programming Guide](https://docs.nvidia.com/cuda/cuda-c-programming-guide/) - CUDA documentation
- [NVIDIA Driver Download](https://www.nvidia.com/Download/index.aspx) - GPU drivers

### Community

- [GitHub Repository](https://github.com/albertobsd/keyhunt) - Source code and issues
- [BitcoinTalk Thread](https://bitcointalk.org/index.php?topic=5322040.0) - Discussion forum

---

**Last Updated:** 2026-02-27
**Build System Version:** 2.0 (with platform abstraction layer)
