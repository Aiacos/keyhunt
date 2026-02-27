@echo off
REM ============================================================================
REM Build script for keyhunt with CUDA GPU support on Windows
REM Automatically detects CUDA installation, GPU architecture, and Visual Studio
REM
REM Requirements:
REM   - Visual Studio 2019 or 2022 with C++ Desktop Development
REM   - NVIDIA CUDA Toolkit 11.0 or later
REM   - NVIDIA GPU with compute capability 5.0 or higher
REM
REM Supported CUDA versions: 11.0 - 12.x
REM Supported GPU architectures: sm_50 (Maxwell) through sm_90 (Hopper)
REM ============================================================================

setlocal enabledelayedexpansion

echo ========================================
echo Keyhunt Windows CUDA Build Script
echo Version 1.0 - GPU Acceleration Support
echo ========================================
echo.

REM Default values
set "CUDA_HOME="
set "CUDA_ARCH="
set "BUILD_TYPE=Release"
set "CLEAN_BUILD=0"
set "VERBOSE=0"
set "FOUND_CUDA=0"
set "FOUND_VSINSTALL="
set "DETECTED_GPU_ARCH="
set "JOBS=%NUMBER_OF_PROCESSORS%"

REM Parse command-line arguments
:parse_args
if "%1"=="" goto end_parse_args
if /i "%1"=="-h" goto show_help
if /i "%1"=="--help" goto show_help
if /i "%1"=="-v" (
    set VERBOSE=1
    shift
    goto parse_args
)
if /i "%1"=="--verbose" (
    set VERBOSE=1
    shift
    goto parse_args
)
if /i "%1"=="-c" (
    set CLEAN_BUILD=1
    shift
    goto parse_args
)
if /i "%1"=="--clean" (
    set CLEAN_BUILD=1
    shift
    goto parse_args
)
if /i "%1"=="--cuda-home" (
    set "CUDA_HOME=%~2"
    shift
    shift
    goto parse_args
)
if /i "%1"=="--arch" (
    set "CUDA_ARCH=%~2"
    shift
    shift
    goto parse_args
)
if /i "%1"=="-j" (
    set "JOBS=%~2"
    shift
    shift
    goto parse_args
)
if /i "%1"=="--jobs" (
    set "JOBS=%~2"
    shift
    shift
    goto parse_args
)
if /i "%1"=="-d" (
    set BUILD_TYPE=Debug
    shift
    goto parse_args
)
if /i "%1"=="--debug" (
    set BUILD_TYPE=Debug
    shift
    goto parse_args
)
if /i "%1"=="--list-arch" (
    goto show_arch_help
)
echo Unknown option: %1
echo Use --help for usage information
exit /b 1

:end_parse_args

REM ============================================================================
REM Step 1: Detect CUDA installation
REM ============================================================================
echo Step 1: Detecting CUDA installation...
echo.

REM Check if CUDA_HOME environment variable is set
if defined CUDA_HOME (
    if exist "%CUDA_HOME%\bin\nvcc.exe" (
        set FOUND_CUDA=1
        goto check_cuda_version
    )
)

REM Search common CUDA installation paths
set "CUDA_BASE=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA"
for %%V in (v12.6 v12.4 v12.2 v12.0 v11.8 v11.7 v11.6 v11.5 v11.4 v11.3 v11.2 v11.1 v11.0) do (
    if exist "%CUDA_BASE%\%%V\bin\nvcc.exe" (
        set "CUDA_HOME=%CUDA_BASE%\%%V"
        set FOUND_CUDA=1
        goto check_cuda_version
    )
)

REM Check if nvcc.exe is in PATH
where nvcc.exe >nul 2>&1
if %errorlevel%==0 (
    for /f "tokens=*" %%i in ('where nvcc.exe') do (
        set "NVCC_PATH=%%i"
        goto found_nvcc_in_path
    )
)
goto cuda_not_found

:found_nvcc_in_path
REM Extract CUDA_HOME from nvcc.exe path (remove \bin\nvcc.exe)
for %%i in ("%NVCC_PATH%") do set "CUDA_HOME=%%~dpi"
set "CUDA_HOME=%CUDA_HOME:~0,-5%"
set FOUND_CUDA=1

:check_cuda_version
if %FOUND_CUDA%==1 (
    echo [OK] Found CUDA installation at: %CUDA_HOME%
    REM Get CUDA version
    "%CUDA_HOME%\bin\nvcc.exe" --version | findstr /C:"release" >nul
    if %errorlevel%==0 (
        for /f "tokens=5" %%v in ('"%CUDA_HOME%\bin\nvcc.exe" --version ^| findstr /C:"release"') do (
            echo [OK] CUDA Version: %%v
        )
    )
    echo.
    goto detect_gpu
)

:cuda_not_found
echo [ERROR] CUDA Toolkit not found!
echo.
echo Please install NVIDIA CUDA Toolkit 11.0 or later:
echo   https://developer.nvidia.com/cuda-downloads
echo.
echo Standard installation path:
echo   C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.x
echo.
echo Or specify custom path with --cuda-home:
echo   %0 --cuda-home "C:\path\to\cuda"
echo.
exit /b 1

REM ============================================================================
REM Step 2: Detect GPU architecture
REM ============================================================================
:detect_gpu
echo Step 2: Detecting GPU architecture...
echo.

REM If user specified architecture, use it
if defined CUDA_ARCH (
    echo [OK] Using specified architecture: %CUDA_ARCH%
    echo.
    goto detect_vs
)

REM Try to detect GPU using nvidia-smi
where nvidia-smi.exe >nul 2>&1
if %errorlevel% neq 0 (
    echo [WARNING] nvidia-smi.exe not found, using default architecture sm_75
    set "CUDA_ARCH=sm_75"
    echo.
    goto detect_vs
)

REM Get compute capability from nvidia-smi
for /f "tokens=*" %%i in ('nvidia-smi --query-gpu^=compute_cap --format^=csv^,noheader 2^>nul') do (
    set "COMPUTE_CAP=%%i"
    goto found_compute_cap
)

REM If detection failed, use default
echo [WARNING] Could not detect GPU compute capability, using default sm_75
set "CUDA_ARCH=sm_75"
echo.
goto detect_vs

:found_compute_cap
REM Convert compute capability (e.g., 7.5) to sm_xx format
set "COMPUTE_CAP=%COMPUTE_CAP: =%"
for /f "tokens=1,2 delims=." %%a in ("%COMPUTE_CAP%") do (
    set "DETECTED_GPU_ARCH=sm_%%a%%b"
)

REM Get GPU name
for /f "tokens=*" %%i in ('nvidia-smi --query-gpu^=name --format^=csv^,noheader 2^>nul') do (
    echo [OK] Detected GPU: %%i
)
echo [OK] Compute Capability: %COMPUTE_CAP% ^(Architecture: %DETECTED_GPU_ARCH%^)
set "CUDA_ARCH=%DETECTED_GPU_ARCH%"
echo.

REM ============================================================================
REM Step 3: Detect Visual Studio installation
REM ============================================================================
:detect_vs
echo Step 3: Detecting Visual Studio installation...
echo.

REM Try Visual Studio 2022 first
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VSINSTALL=C:\Program Files\Microsoft Visual Studio\2022\Community"
    set FOUND_VSINSTALL=1
    echo [OK] Found Visual Studio 2022 Community
    goto setup_vs
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VSINSTALL=C:\Program Files\Microsoft Visual Studio\2022\Professional"
    set FOUND_VSINSTALL=1
    echo [OK] Found Visual Studio 2022 Professional
    goto setup_vs
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VSINSTALL=C:\Program Files\Microsoft Visual Studio\2022\Enterprise"
    set FOUND_VSINSTALL=1
    echo [OK] Found Visual Studio 2022 Enterprise
    goto setup_vs
)

REM Try Visual Studio 2019
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VSINSTALL=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community"
    set FOUND_VSINSTALL=1
    echo [OK] Found Visual Studio 2019 Community
    goto setup_vs
)
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VSINSTALL=C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional"
    set FOUND_VSINSTALL=1
    echo [OK] Found Visual Studio 2019 Professional
    goto setup_vs
)
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VSINSTALL=C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise"
    set FOUND_VSINSTALL=1
    echo [OK] Found Visual Studio 2019 Enterprise
    goto setup_vs
)

REM Visual Studio not found
echo [ERROR] Visual Studio not found!
echo.
echo Please install Visual Studio 2019 or 2022 with C++ Desktop Development workload:
echo   https://visualstudio.microsoft.com/downloads/
echo.
echo Required components:
echo   - MSVC v142 or v143 - VS 2019/2022 C++ x64/x86 build tools
echo   - Windows 10 SDK or Windows 11 SDK
echo.
exit /b 1

:setup_vs
echo.
echo Setting up Visual Studio environment...
call "%VSINSTALL%\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 (
    echo [ERROR] Failed to set up Visual Studio environment
    exit /b 1
)
echo [OK] Visual Studio environment configured
echo.

REM Verify compiler is available
where cl.exe >nul 2>&1
if errorlevel 1 (
    echo [ERROR] cl.exe not found in PATH
    echo Make sure Visual Studio is properly installed
    exit /b 1
)

REM Display compiler version
echo Compiler information:
cl.exe 2>&1 | findstr /C:"Microsoft" /C:"Version"
echo.

REM ============================================================================
REM Step 4: Clean build artifacts if requested
REM ============================================================================
if %CLEAN_BUILD%==1 (
    echo Step 4: Cleaning build artifacts...
    if exist obj rmdir /s /q obj
    if exist keyhunt.exe del /q keyhunt.exe
    if exist keyhunt_cuda.exe del /q keyhunt_cuda.exe
    echo [OK] Clean complete
    echo.
)

REM ============================================================================
REM Step 5: Create output directories
REM ============================================================================
echo Step 5: Creating output directories...
if not exist obj mkdir obj
if not exist obj\base58 mkdir obj\base58
if not exist obj\rmd160 mkdir obj\rmd160
if not exist obj\xxhash mkdir obj\xxhash
if not exist obj\core mkdir obj\core
if not exist obj\config mkdir obj\config
if not exist obj\gpu mkdir obj\gpu
if not exist obj\bloom mkdir obj\bloom
if not exist obj\hash mkdir obj\hash
if not exist obj\sha3 mkdir obj\sha3
if not exist obj\platform mkdir obj\platform
if not exist obj\bsgs mkdir obj\bsgs
if not exist obj\hybrid mkdir obj\hybrid
if not exist obj\util mkdir obj\util
if not exist obj\distributed mkdir obj\distributed
if not exist obj\wizard mkdir obj\wizard
if not exist obj\secp256k1 mkdir obj\secp256k1
if not exist obj\search mkdir obj\search
if not exist obj\sort mkdir obj\sort
if not exist obj\crypto mkdir obj\crypto
if not exist obj\io mkdir obj\io
echo [OK] Output directories created
echo.

REM ============================================================================
REM Step 6: Build with CUDA support
REM ============================================================================
echo Step 6: Building keyhunt with CUDA support...
echo.
echo Build configuration:
echo   CUDA Home:      %CUDA_HOME%
echo   GPU Arch:       %CUDA_ARCH%
echo   Build Type:     %BUILD_TYPE%
echo   Parallel Jobs:  %JOBS%
echo.

REM Set compiler flags
set "CXX_FLAGS=/O2 /std:c++17 /EHsc /nologo /W3 /D_CRT_SECURE_NO_WARNINGS /DHAVE_CUDA_BACKEND=1 /Isrc"
set "NVCC_FLAGS=-O3 -std=c++17 -arch=%CUDA_ARCH% --compiler-options /EHsc,/W3,/nologo -DHAVE_CUDA_BACKEND=1 -Isrc"
set "LD_FLAGS=/NOLOGO"
set "LIBS=ws2_32.lib bcrypt.lib cudart.lib"

if /i "%BUILD_TYPE%"=="Debug" (
    set "CXX_FLAGS=/Od /Zi /DEBUG /std:c++17 /EHsc /nologo /W3 /D_CRT_SECURE_NO_WARNINGS /DHAVE_CUDA_BACKEND=1 /Isrc"
    set "NVCC_FLAGS=-g -G -std=c++17 -arch=%CUDA_ARCH% --compiler-options /EHsc,/W3,/nologo -DHAVE_CUDA_BACKEND=1 -Isrc"
    set "LD_FLAGS=/NOLOGO /DEBUG"
)

if %VERBOSE%==1 (
    echo.
    echo Verbose build information:
    echo   CXX_FLAGS:  %CXX_FLAGS%
    echo   NVCC_FLAGS: %NVCC_FLAGS%
    echo   LD_FLAGS:   %LD_FLAGS%
    echo   LIBS:       %LIBS%
    echo.
)

REM Add CUDA paths
set "PATH=%CUDA_HOME%\bin;%PATH%"
set "INCLUDE=%CUDA_HOME%\include;%INCLUDE%"
set "LIB=%CUDA_HOME%\lib\x64;%LIB%"

REM Compile CUDA source file (gpu_backend_cuda.cu)
echo Compiling CUDA GPU backend...
"%CUDA_HOME%\bin\nvcc.exe" %NVCC_FLAGS% -c src\gpu\gpu_backend_cuda.cu -o obj\gpu\gpu_backend_cuda.obj
if errorlevel 1 (
    echo [ERROR] CUDA compilation failed!
    exit /b 1
)
echo [OK] CUDA GPU backend compiled
echo.

REM Note: For a complete build, we would compile all other C++ files with cl.exe
REM and link everything together. Since the Makefile handles CPU compilation,
REM we'll provide instructions for manual integration.

echo ========================================
echo CUDA Build Preparation Complete!
echo ========================================
echo.
echo The CUDA GPU backend has been compiled successfully.
echo.
echo To complete the build:
echo   1. Compile the remaining C++ files using the standard Windows build script
echo   2. Link with the CUDA runtime library (cudart.lib)
echo.
echo Or use the Makefile with MinGW-w64 and CUDA support:
echo   make NVCC="%CUDA_HOME%\bin\nvcc.exe" NVCCFLAGS="%NVCC_FLAGS%" CUDA_HOME="%CUDA_HOME%"
echo.
echo GPU Backend Object: obj\gpu\gpu_backend_cuda.obj
echo Architecture:       %CUDA_ARCH%
echo CUDA Version:       %CUDA_HOME%
echo.
goto end

:show_help
echo Usage: %0 [OPTIONS]
echo.
echo Build keyhunt with CUDA GPU support on Windows. Automatically detects
echo CUDA installation, compatible Visual Studio version, and GPU architecture.
echo.
echo Options:
echo   --cuda-home PATH   Path to CUDA toolkit (default: auto-detect)
echo   --arch ARCH        CUDA architecture (default: auto-detect from GPU)
echo   --jobs N, -j N     Parallel jobs (default: number of processors)
echo   --verbose, -v      Show detailed build information
echo   --clean, -c        Clean build artifacts before building
echo   --debug, -d        Build debug version
echo   --list-arch        Show GPU architecture reference
echo   --help, -h         Show this help
echo.
echo Environment variables:
echo   CUDA_HOME          Alternative to --cuda-home
echo   CUDA_ARCH          Alternative to --arch
echo.
echo Examples:
echo   %0                                              # Auto-detect everything
echo   %0 --cuda-home "C:\cuda-12.6"                   # Specify CUDA path
echo   %0 --arch sm_86                                 # For RTX 30xx GPUs
echo   %0 --arch sm_89                                 # For RTX 40xx GPUs
echo   set CUDA_ARCH=sm_75 ^& %0                       # Environment variable
echo.
goto show_arch_help_inline

:show_arch_help
echo.
:show_arch_help_inline
echo GPU Architecture Reference:
echo   sm_50  - Maxwell    (GTX 900 series)
echo   sm_60  - Pascal     (GTX 1000 series, P100)
echo   sm_61  - Pascal     (GTX 1050/1060/1070/1080)
echo   sm_70  - Volta      (V100, Titan V)
echo   sm_75  - Turing     (RTX 2000 series, GTX 1660)
echo   sm_80  - Ampere     (A100)
echo   sm_86  - Ampere     (RTX 3000 series)
echo   sm_89  - Ada        (RTX 4000 series)
echo   sm_90  - Hopper     (H100)
echo.
if "%1"=="--list-arch" exit /b 0
if "%1"=="--help" exit /b 0
if "%1"=="-h" exit /b 0
goto end

:end
endlocal
exit /b 0
