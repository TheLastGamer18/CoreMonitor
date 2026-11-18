@echo off
setlocal EnableDelayedExpansion
echo ============================================================
echo Building Core Monitor Suite (Core75, CoreM1, and Combined)
echo ============================================================
cd /d "%~dp0"

:: Close running instances if any, to allow overwriting executables
taskkill /IM CoreMonitor.exe /F 2>nul
taskkill /IM CoreMonitor-Core75.exe /F 2>nul
taskkill /IM CoreMonitor-CoreM1.exe /F 2>nul

set "COMMON_CFLAGS=-Os -s -Wall -flto -fno-math-errno -fno-trapping-math -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-ident -fomit-frame-pointer -falign-functions=1 -falign-jumps=1 -falign-loops=1 -mwindows -static-libgcc -Wl,--gc-sections -Wl,--file-alignment,512 -Wl,--nxcompat -Wl,--dynamicbase -Wl,--no-seh"
set "COMMON_LIBS=-lgdi32 -luser32 -lshell32 -lsetupapi"
set "SHARED_SRCS=src\hid_common.c src\icon_generator.c src\settings.c src\battery_stats.c"

:: 1. Compile Resource Objects
echo [1/6] Compiling resources for Core75...
windres -DBUILD_TARGET_CORE75 -i res\resource.rc -o res\resource_core75.o -I res
if %ERRORLEVEL% NEQ 0 goto :error

echo [2/6] Compiling resources for CoreM1...
windres -DBUILD_TARGET_COREM1 -i res\resource.rc -o res\resource_corem1.o -I res
if %ERRORLEVEL% NEQ 0 goto :error

echo [3/6] Compiling resources for Combined...
windres -DBUILD_TARGET_CORE75 -DBUILD_TARGET_COREM1 -i res\resource.rc -o res\resource_combined.o -I res
if %ERRORLEVEL% NEQ 0 goto :error

:: 2. Compile CoreMonitor-Core75.exe
echo [4/6] Compiling CoreMonitor-Core75.exe...
gcc %COMMON_CFLAGS% -DBUILD_TARGET_CORE75 -o CoreMonitor-Core75.exe src\main.c src\device_core75.c %SHARED_SRCS% res\resource_core75.o %COMMON_LIBS%
if %ERRORLEVEL% NEQ 0 goto :error

:: 3. Compile CoreMonitor-CoreM1.exe
echo [5/6] Compiling CoreMonitor-CoreM1.exe...
gcc %COMMON_CFLAGS% -DBUILD_TARGET_COREM1 -o CoreMonitor-CoreM1.exe src\main.c src\device_corem1.c %SHARED_SRCS% res\resource_corem1.o %COMMON_LIBS%
if %ERRORLEVEL% NEQ 0 goto :error

:: 4. Compile Combined CoreMonitor.exe
echo [6/6] Compiling CoreMonitor.exe (Combined Core75 + CoreM1)...
gcc %COMMON_CFLAGS% -DBUILD_TARGET_CORE75 -DBUILD_TARGET_COREM1 -o CoreMonitor.exe src\main.c src\device_core75.c src\device_corem1.c %SHARED_SRCS% res\resource_combined.o %COMMON_LIBS%
if %ERRORLEVEL% NEQ 0 goto :error

echo.
echo ============================================================
echo Build Successful!
echo   Output 1: CoreMonitor-Core75.exe (Keyboard Only)
echo   Output 2: CoreMonitor-CoreM1.exe (Mouse Only)
echo   Output 3: CoreMonitor.exe        (Combined Dual Monitor)
echo ============================================================
echo.
if /i not "%1"=="nopause" pause
exit /b 0

:error
echo.
echo ============================================================
echo Compilation FAILED with error code %ERRORLEVEL%!
echo ============================================================
echo.
if /i not "%1"=="nopause" pause
exit /b %ERRORLEVEL%
