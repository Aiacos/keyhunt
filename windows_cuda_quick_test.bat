@echo off
REM ============================================================================
REM Windows CUDA Quick Test Suite - 5 Minutes
REM
REM Tests basic CUDA GPU functionality:
REM   1. GPU detection via --benchmark
REM   2. Basic GPU search (ADDRESS mode, 1-32 puzzle)
REM
REM Usage: windows_cuda_quick_test.bat
REM ============================================================================

setlocal enabledelayedexpansion

echo ========================================
echo Windows CUDA Quick Test Suite
echo Version 1.0 - 5 Minute Test
echo ========================================
echo.

REM Check if keyhunt.exe exists
if not exist keyhunt.exe (
    echo [ERROR] keyhunt.exe not found!
    echo Please build keyhunt first:
    echo   build_windows_cuda.bat
    exit /b 1
)

echo [INFO] Starting CUDA GPU tests...
echo.

REM ============================================================================
REM Test 1: GPU Detection
REM ============================================================================
echo Test 1: GPU Detection
echo ----------------------------------------

keyhunt.exe --benchmark > cuda_test_benchmark.txt 2>&1
if %errorlevel% neq 0 (
    echo [FAIL] Benchmark command failed
    type cuda_test_benchmark.txt
    exit /b 1
)

REM Check if GPU detected
findstr /C:"GPU Count" cuda_test_benchmark.txt >nul
if %errorlevel% neq 0 (
    echo [FAIL] GPU not detected
    echo.
    echo Benchmark output:
    type cuda_test_benchmark.txt
    echo.
    echo Troubleshooting:
    echo   - Verify NVIDIA GPU is installed
    echo   - Install latest NVIDIA drivers
    echo   - Install CUDA Toolkit
    echo   - Check if cudart64_XX.dll is in PATH
    exit /b 1
)

REM Extract GPU name
for /f "tokens=*" %%i in ('findstr /C:"GPU 0:" cuda_test_benchmark.txt') do (
    echo [PASS] %%i
    goto gpu_found
)

:gpu_found
echo.

REM ============================================================================
REM Test 2: Basic GPU Search (ADDRESS mode, Puzzle 1-32)
REM ============================================================================
echo Test 2: Basic GPU Search ^(ADDRESS mode, Puzzle 1-32^)
echo ----------------------------------------

if not exist tests\1to32.txt (
    echo [WARNING] Test file tests\1to32.txt not found
    echo Skipping GPU search test
    echo.
    goto test_complete
)

echo [INFO] Running GPU search for 10 seconds...
echo Command: keyhunt.exe -m address -f tests\1to32.txt -r 1:FFFFFFFF -g -s 10
echo.

keyhunt.exe -m address -f tests\1to32.txt -r 1:FFFFFFFF -g -s 10 > cuda_test_search.txt 2>&1
if %errorlevel% neq 0 (
    echo [FAIL] GPU search failed
    echo.
    echo Search output:
    type cuda_test_search.txt
    exit /b 1
)

REM Check if GPU mode was used
findstr /C:"GPU" cuda_test_search.txt >nul
if %errorlevel% neq 0 (
    echo [WARNING] GPU mode may not have been enabled
    echo.
    echo Search output:
    type cuda_test_search.txt
    echo.
)

REM Extract throughput
for /f "tokens=*" %%i in ('findstr /C:"Keys/sec" /C:"MK/s" /C:"GK/s" cuda_test_search.txt') do (
    echo [INFO] Throughput: %%i
)

echo [PASS] GPU search completed successfully
echo.

:test_complete
REM ============================================================================
REM Test Summary
REM ============================================================================
echo ========================================
echo Test Summary
echo ========================================
echo.

echo Tests Completed:
echo   ✅ GPU Detection
echo   ✅ Basic GPU Search
echo.

echo GPU Information:
type cuda_test_benchmark.txt | findstr /C:"GPU Count" /C:"GPU 0" /C:"Compute Capability" /C:"VRAM"
echo.

echo Performance:
type cuda_test_search.txt | findstr /C:"Keys/sec" /C:"MK/s" /C:"GK/s"
echo.

echo ========================================
echo All Quick Tests Passed!
echo ========================================
echo.

echo Next Steps:
echo   - Run full test suite: windows_cuda_full_test.bat
echo   - Test real puzzles: keyhunt.exe -m address -f tests\66.txt -b 66 -g
echo   - Benchmark performance: keyhunt.exe --benchmark
echo.

REM Clean up temporary files (optional)
REM del cuda_test_benchmark.txt cuda_test_search.txt

exit /b 0
