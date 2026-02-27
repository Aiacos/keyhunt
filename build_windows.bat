@echo off
REM ============================================================================
REM Build script for keyhunt with native MSVC compiler (Windows 64-bit)
REM Automatically detects Visual Studio installation and sets up environment
REM
REM Requirements: Visual Studio 2019 or 2022 with C++ Desktop Development
REM Supported: Windows 10/11 (64-bit)
REM ============================================================================

setlocal enabledelayedexpansion

echo ========================================
echo Keyhunt Windows Build Script (MSVC)
echo Version 1.0 - Native Windows Build
echo ========================================
echo.

REM Default values
set BUILD_TYPE=Release
set CLEAN_BUILD=0
set BUILD_LEGACY=0
set BUILD_TESTS=0
set VERBOSE=0
set FOUND_VSINSTALL=

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
if /i "%1"=="-l" (
    set BUILD_LEGACY=1
    shift
    goto parse_args
)
if /i "%1"=="--legacy" (
    set BUILD_LEGACY=1
    shift
    goto parse_args
)
if /i "%1"=="-t" (
    set BUILD_TESTS=1
    shift
    goto parse_args
)
if /i "%1"=="--test" (
    set BUILD_TESTS=1
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
echo Unknown option: %1
echo Use --help for usage information
exit /b 1

:end_parse_args

REM Detect Visual Studio installation
echo Detecting Visual Studio installation...
echo.

REM Try Visual Studio 2022 first
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VSINSTALL=C:\Program Files\Microsoft Visual Studio\2022\Community"
    set FOUND_VSINSTALL=1
    echo [OK] Found Visual Studio 2022 Community
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VSINSTALL=C:\Program Files\Microsoft Visual Studio\2022\Professional"
    set FOUND_VSINSTALL=1
    echo [OK] Found Visual Studio 2022 Professional
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VSINSTALL=C:\Program Files\Microsoft Visual Studio\2022\Enterprise"
    set FOUND_VSINSTALL=1
    echo [OK] Found Visual Studio 2022 Enterprise
)

REM Try Visual Studio 2019 if 2022 not found
if not defined FOUND_VSINSTALL (
    if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" (
        set "VSINSTALL=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community"
        set FOUND_VSINSTALL=1
        echo [OK] Found Visual Studio 2019 Community
    )
)
if not defined FOUND_VSINSTALL (
    if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
        set "VSINSTALL=C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional"
        set FOUND_VSINSTALL=1
        echo [OK] Found Visual Studio 2019 Professional
    )
)
if not defined FOUND_VSINSTALL (
    if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" (
        set "VSINSTALL=C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise"
        set FOUND_VSINSTALL=1
        echo [OK] Found Visual Studio 2019 Enterprise
    )
)

REM Check if Visual Studio was found
if not defined FOUND_VSINSTALL (
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
)

REM Set up Visual Studio environment
echo.
echo Setting up MSVC environment...
call "%VSINSTALL%\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 (
    echo [ERROR] Failed to set up MSVC environment
    exit /b 1
)
echo [OK] MSVC environment configured
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

REM Clean build artifacts if requested
if %CLEAN_BUILD%==1 (
    echo Cleaning build artifacts...
    if exist obj rmdir /s /q obj
    if exist keyhunt.exe del /q keyhunt.exe
    if exist keyhunt_legacy.exe del /q keyhunt_legacy.exe
    if exist run_tests.exe del /q run_tests.exe
    echo [OK] Clean complete
    echo.
)

REM Create object directory structure
echo Creating output directories...
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
if not exist obj\gmp256k1 mkdir obj\gmp256k1
if not exist obj\search mkdir obj\search
if not exist obj\sort mkdir obj\sort
if not exist obj\crypto mkdir obj\crypto
if not exist obj\io mkdir obj\io
if not exist obj\tests mkdir obj\tests
echo [OK] Output directories created
echo.

REM Set compiler flags
set INCLUDES=/Isrc
set DEFINES=/D_CRT_SECURE_NO_WARNINGS /DPLATFORM_WINDOWS=1

REM Optimization flags based on build type
if "%BUILD_TYPE%"=="Debug" (
    set OPT_FLAGS=/Od /Zi /MDd
    echo Build type: Debug
) else (
    set OPT_FLAGS=/O2 /Oi /GL /MD
    echo Build type: Release
)

REM Common C++ flags
set CXXFLAGS=/nologo /W3 /EHsc /std:c++17 %OPT_FLAGS% %INCLUDES% %DEFINES%

REM Common C flags
set CFLAGS=/nologo /W3 %OPT_FLAGS% %INCLUDES% %DEFINES%

REM AVX2 flags for optimized hash functions
set AVX2_FLAGS=/arch:AVX2

REM Linker flags
set LDFLAGS=/nologo /MACHINE:X64
if "%BUILD_TYPE%"=="Release" (
    set LDFLAGS=%LDFLAGS% /LTCG
)

REM Windows libraries
set LIBS=ws2_32.lib bcrypt.lib

echo ========================================
echo Building keyhunt.exe
echo ========================================
echo.

REM Compile base58
echo Compiling base58...
cl.exe %CFLAGS% /c src\base58\base58.c /Foobj\base58\base58.obj
if errorlevel 1 exit /b 1

REM Compile rmd160
echo Compiling rmd160...
cl.exe %CFLAGS% /c src\rmd160\rmd160.c /Foobj\rmd160\rmd160.obj
if errorlevel 1 exit /b 1

REM Compile xxhash
echo Compiling xxhash...
cl.exe %CFLAGS% /c src\xxhash\xxhash.c /Foobj\xxhash\xxhash.obj
if errorlevel 1 exit /b 1

REM Compile core modules
echo Compiling core modules...
cl.exe %CFLAGS% /c src\core\util.c /Foobj\core\util.obj
if errorlevel 1 exit /b 1
cl.exe %CFLAGS% /c src\core\sysinfo.c /Foobj\core\sysinfo.obj
if errorlevel 1 exit /b 1
cl.exe %CFLAGS% /c src\core\parameter_validator.c /Foobj\core\parameter_validator.obj
if errorlevel 1 exit /b 1
cl.exe %CFLAGS% /c src\core\config.c /Foobj\core\config.obj
if errorlevel 1 exit /b 1

REM Compile config module
echo Compiling config...
cl.exe %CXXFLAGS% /c src\config\config.cpp /Foobj\config\config.obj
if errorlevel 1 exit /b 1

REM Compile platform abstraction layer
echo Compiling platform layer...
cl.exe %CFLAGS% /c src\platform\platform_thread.c /Foobj\platform\platform_thread.obj
if errorlevel 1 exit /b 1
cl.exe %CFLAGS% /c src\platform\platform_mutex.c /Foobj\platform\platform_mutex.obj
if errorlevel 1 exit /b 1
cl.exe %CFLAGS% /c src\platform\platform_time.c /Foobj\platform\platform_time.obj
if errorlevel 1 exit /b 1
cl.exe %CFLAGS% /c src\platform\platform_compat.c /Foobj\platform\platform_compat.obj
if errorlevel 1 exit /b 1
cl.exe %CFLAGS% /c src\platform\platform_dir.c /Foobj\platform\platform_dir.obj
if errorlevel 1 exit /b 1

REM Compile GPU backend (no CUDA for now)
echo Compiling GPU backend...
cl.exe %CXXFLAGS% /c src\gpu\gpu_backend_none.cpp /Foobj\gpu\gpu_backend_none.obj
if errorlevel 1 exit /b 1
cl.exe %CFLAGS% /c src\gpu\gpu_autotune.c /Foobj\gpu\gpu_autotune.obj
if errorlevel 1 exit /b 1
cl.exe %CFLAGS% /c src\gpu\multi_gpu_scheduler.c /Foobj\gpu\multi_gpu_scheduler.obj
if errorlevel 1 exit /b 1
cl.exe %CFLAGS% /c src\gpu\async_pipeline.c /Foobj\gpu\async_pipeline.obj
if errorlevel 1 exit /b 1

REM Compile bloom filters
echo Compiling bloom filters...
cl.exe %CXXFLAGS% /c src\bloom\bloom.cpp /Foobj\bloom\bloom.obj
if errorlevel 1 exit /b 1
cl.exe %CXXFLAGS% /c src\bloom\bloom_simd.cpp /Foobj\bloom\bloom_simd.obj
if errorlevel 1 exit /b 1

REM Compile hash functions (with AVX2 optimizations)
echo Compiling hash functions...
cl.exe %CXXFLAGS% /c src\hash\ripemd160.cpp /Foobj\hash\ripemd160.obj
if errorlevel 1 exit /b 1
cl.exe %CXXFLAGS% /c src\hash\ripemd160_sse.cpp /Foobj\hash\ripemd160_sse.obj
if errorlevel 1 exit /b 1
cl.exe %CXXFLAGS% %AVX2_FLAGS% /c src\hash\ripemd160_avx2.cpp /Foobj\hash\ripemd160_avx2.obj
if errorlevel 1 exit /b 1
cl.exe %CXXFLAGS% /c src\hash\ripemd160_avx512.cpp /Foobj\hash\ripemd160_avx512.obj
if errorlevel 1 exit /b 1
cl.exe %CXXFLAGS% /c src\hash\sha256.cpp /Foobj\hash\sha256.obj
if errorlevel 1 exit /b 1
cl.exe %CXXFLAGS% /c src\hash\sha256_sse.cpp /Foobj\hash\sha256_sse.obj
if errorlevel 1 exit /b 1
cl.exe %CXXFLAGS% %AVX2_FLAGS% /c src\hash\sha256_avx2.cpp /Foobj\hash\sha256_avx2.obj
if errorlevel 1 exit /b 1
cl.exe %CXXFLAGS% /c src\hash\sha256_shani.cpp /Foobj\hash\sha256_shani.obj
if errorlevel 1 exit /b 1
cl.exe %CXXFLAGS% /c src\hash\sha512.cpp /Foobj\hash\sha512.obj
if errorlevel 1 exit /b 1
cl.exe %CXXFLAGS% %AVX2_FLAGS% /c src\hash\sha512_avx2.cpp /Foobj\hash\sha512_avx2.obj
if errorlevel 1 exit /b 1
cl.exe %CXXFLAGS% /c src\hash\sha512_avx512.cpp /Foobj\hash\sha512_avx512.obj
if errorlevel 1 exit /b 1

REM Compile SHA3/Keccak
echo Compiling SHA3...
cl.exe %CFLAGS% /c src\sha3\sha3.c /Foobj\sha3\sha3.obj
if errorlevel 1 exit /b 1
cl.exe %CFLAGS% /c src\sha3\keccak.c /Foobj\sha3\keccak.obj
if errorlevel 1 exit /b 1

REM Compile BSGS
echo Compiling BSGS...
cl.exe %CXXFLAGS% /c src\bsgs\bsgs_ops.cpp /Foobj\bsgs\bsgs_ops.obj
if errorlevel 1 exit /b 1
cl.exe %CXXFLAGS% /c src\bsgs\bsgs_fast.cpp /Foobj\bsgs\bsgs_fast.obj
if errorlevel 1 exit /b 1
cl.exe %CXXFLAGS% /c src\bsgs\bsgs_sort.cpp /Foobj\bsgs\bsgs_sort.obj
if errorlevel 1 exit /b 1

REM Compile hybrid and util
echo Compiling hybrid and utility modules...
cl.exe %CXXFLAGS% /c src\hybrid\adaptive_scheduler.cpp /Foobj\hybrid\adaptive_scheduler.obj
if errorlevel 1 exit /b 1
cl.exe %CFLAGS% /c src\util\mempool.c /Foobj\util\mempool.obj
if errorlevel 1 exit /b 1

REM Compile distributed
echo Compiling distributed...
cl.exe %CFLAGS% /c src\distributed\distributed.c /Foobj\distributed\distributed.obj
if errorlevel 1 exit /b 1

REM Compile output and progress
echo Compiling output and progress...
cl.exe %CXXFLAGS% /c src\output.cpp /Foobj\output.obj
if errorlevel 1 exit /b 1
cl.exe %CXXFLAGS% /c src\progress.cpp /Foobj\progress.obj
if errorlevel 1 exit /b 1

REM Compile benchmark and CLI
echo Compiling benchmark and CLI...
cl.exe %CXXFLAGS% /c src\benchmark.cpp /Foobj\benchmark.obj
if errorlevel 1 exit /b 1
cl.exe %CXXFLAGS% /c src\cli.cpp /Foobj\cli.obj
if errorlevel 1 exit /b 1

REM Compile wizard
echo Compiling wizard...
cl.exe %CFLAGS% /c src\wizard\wizard.c /Foobj\wizard\wizard.obj
if errorlevel 1 exit /b 1
cl.exe %CFLAGS% /c src\wizard\wizard_config.c /Foobj\wizard\wizard_config.obj
if errorlevel 1 exit /b 1
cl.exe %CFLAGS% /c src\wizard\wizard_ui.c /Foobj\wizard\wizard_ui.obj
if errorlevel 1 exit /b 1
cl.exe %CFLAGS% /c src\wizard\wizard_community.c /Foobj\wizard\wizard_community.obj
if errorlevel 1 exit /b 1
cl.exe %CFLAGS% /c src\wizard\wizard_server.c /Foobj\wizard\wizard_server.obj
if errorlevel 1 exit /b 1
cl.exe %CFLAGS% /c src\wizard\wizard_client.c /Foobj\wizard\wizard_client.obj
if errorlevel 1 exit /b 1

REM Compile secp256k1
echo Compiling secp256k1...
cl.exe %CXXFLAGS% /c src\secp256k1\Int.cpp /Foobj\secp256k1\Int.obj
if errorlevel 1 exit /b 1
cl.exe %CXXFLAGS% /c src\secp256k1\Point.cpp /Foobj\secp256k1\Point.obj
if errorlevel 1 exit /b 1
cl.exe %CXXFLAGS% /c src\secp256k1\SECP256K1.cpp /Foobj\secp256k1\SECP256K1.obj
if errorlevel 1 exit /b 1
cl.exe %CXXFLAGS% /c src\secp256k1\IntMod.cpp /Foobj\secp256k1\IntMod.obj
if errorlevel 1 exit /b 1
cl.exe %CXXFLAGS% /c src\secp256k1\Random.cpp /Foobj\secp256k1\Random.obj
if errorlevel 1 exit /b 1
cl.exe %CXXFLAGS% /c src\secp256k1\IntGroup.cpp /Foobj\secp256k1\IntGroup.obj
if errorlevel 1 exit /b 1

REM Compile search modules
echo Compiling search modules...
cl.exe %CXXFLAGS% /c src\search\search_xpoint.cpp /Foobj\search\search_xpoint.obj
if errorlevel 1 exit /b 1
cl.exe %CXXFLAGS% /c src\search\search_rmd160.cpp /Foobj\search\search_rmd160.obj
if errorlevel 1 exit /b 1
cl.exe %CXXFLAGS% /c src\search\search_bsgs.cpp /Foobj\search\search_bsgs.obj
if errorlevel 1 exit /b 1
cl.exe %CXXFLAGS% /c src\search\search_bsgs_threads.cpp /Foobj\search\search_bsgs_threads.obj
if errorlevel 1 exit /b 1
cl.exe %CXXFLAGS% /c src\search\search_minikeys.cpp /Foobj\search\search_minikeys.obj
if errorlevel 1 exit /b 1
cl.exe %CXXFLAGS% /c src\search\search_address.cpp /Foobj\search\search_address.obj
if errorlevel 1 exit /b 1
cl.exe %CXXFLAGS% /c src\search\search_vanity.cpp /Foobj\search\search_vanity.obj
if errorlevel 1 exit /b 1

REM Compile sort
echo Compiling sort...
cl.exe %CXXFLAGS% /c src\sort\sort.cpp /Foobj\sort\sort.obj
if errorlevel 1 exit /b 1

REM Compile crypto
echo Compiling crypto...
cl.exe %CXXFLAGS% /c src\crypto\address_util.cpp /Foobj\crypto\address_util.obj
if errorlevel 1 exit /b 1
cl.exe %CXXFLAGS% /c src\crypto\bloom_init.cpp /Foobj\crypto\bloom_init.obj
if errorlevel 1 exit /b 1

REM Compile I/O
echo Compiling I/O...
cl.exe %CXXFLAGS% /c src\io\io.cpp /Foobj\io\io.obj
if errorlevel 1 exit /b 1

REM Compile main keyhunt
echo Compiling main keyhunt...
cl.exe %CXXFLAGS% /c src\keyhunt.cpp /Foobj\keyhunt.obj
if errorlevel 1 exit /b 1

REM Link final executable
echo.
echo Linking keyhunt.exe...
link.exe %LDFLAGS% /OUT:keyhunt.exe ^
    obj\keyhunt.obj ^
    obj\base58\base58.obj ^
    obj\rmd160\rmd160.obj ^
    obj\xxhash\xxhash.obj ^
    obj\core\util.obj ^
    obj\core\sysinfo.obj ^
    obj\core\parameter_validator.obj ^
    obj\core\config.obj ^
    obj\config\config.obj ^
    obj\gpu\gpu_backend_none.obj ^
    obj\gpu\gpu_autotune.obj ^
    obj\gpu\multi_gpu_scheduler.obj ^
    obj\gpu\async_pipeline.obj ^
    obj\bloom\bloom.obj ^
    obj\bloom\bloom_simd.obj ^
    obj\hash\ripemd160.obj ^
    obj\hash\ripemd160_sse.obj ^
    obj\hash\ripemd160_avx2.obj ^
    obj\hash\ripemd160_avx512.obj ^
    obj\hash\sha256.obj ^
    obj\hash\sha256_sse.obj ^
    obj\hash\sha256_avx2.obj ^
    obj\hash\sha256_shani.obj ^
    obj\hash\sha512.obj ^
    obj\hash\sha512_avx2.obj ^
    obj\hash\sha512_avx512.obj ^
    obj\sha3\sha3.obj ^
    obj\sha3\keccak.obj ^
    obj\platform\platform_thread.obj ^
    obj\platform\platform_mutex.obj ^
    obj\platform\platform_time.obj ^
    obj\platform\platform_compat.obj ^
    obj\platform\platform_dir.obj ^
    obj\bsgs\bsgs_ops.obj ^
    obj\bsgs\bsgs_fast.obj ^
    obj\bsgs\bsgs_sort.obj ^
    obj\hybrid\adaptive_scheduler.obj ^
    obj\util\mempool.obj ^
    obj\distributed\distributed.obj ^
    obj\output.obj ^
    obj\progress.obj ^
    obj\benchmark.obj ^
    obj\cli.obj ^
    obj\wizard\wizard.obj ^
    obj\wizard\wizard_config.obj ^
    obj\wizard\wizard_ui.obj ^
    obj\wizard\wizard_community.obj ^
    obj\wizard\wizard_server.obj ^
    obj\wizard\wizard_client.obj ^
    obj\secp256k1\Int.obj ^
    obj\secp256k1\Point.obj ^
    obj\secp256k1\SECP256K1.obj ^
    obj\secp256k1\IntMod.obj ^
    obj\secp256k1\Random.obj ^
    obj\secp256k1\IntGroup.obj ^
    obj\search\search_xpoint.obj ^
    obj\search\search_rmd160.obj ^
    obj\search\search_bsgs.obj ^
    obj\search\search_bsgs_threads.obj ^
    obj\search\search_minikeys.obj ^
    obj\search\search_address.obj ^
    obj\search\search_vanity.obj ^
    obj\sort\sort.obj ^
    obj\crypto\address_util.obj ^
    obj\crypto\bloom_init.obj ^
    obj\io\io.obj ^
    %LIBS%

if errorlevel 1 (
    echo.
    echo [ERROR] Linking failed
    exit /b 1
)

echo.
echo ========================================
echo Build Completed Successfully!
echo ========================================
echo.
echo Built executable: keyhunt.exe
if exist keyhunt.exe (
    for %%A in (keyhunt.exe) do echo   Size: %%~zA bytes
)
echo.
echo Next steps:
echo   1. Run: keyhunt.exe --help
echo   2. Example: keyhunt.exe -m address -f tests\1to32.txt -t 4
echo   3. Benchmark: keyhunt.exe --benchmark
echo.
goto end

:show_help
echo.
echo Keyhunt Windows Build Script (MSVC)
echo.
echo Usage:
echo   build_windows.bat [options]
echo.
echo Options:
echo   -h, --help        Show this help message
echo   -v, --verbose     Enable verbose output
echo   -c, --clean       Clean build artifacts before building
echo   -d, --debug       Build debug version (default: Release)
echo   -l, --legacy      Also build legacy version (requires OpenSSL, GMP)
echo   -t, --test        Build test suite
echo.
echo Requirements:
echo   - Visual Studio 2019 or 2022 with C++ Desktop Development
echo   - Windows 10 SDK or Windows 11 SDK
echo.
echo Output:
echo   keyhunt.exe       Main executable (Windows 64-bit)
echo.
echo Examples:
echo   build_windows.bat              # Basic release build
echo   build_windows.bat --clean      # Clean build
echo   build_windows.bat --debug      # Debug build
echo   build_windows.bat --verbose    # Verbose output
echo.
goto end

:end
endlocal
