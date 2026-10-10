@echo off
echo Building Core Monitor...
cd /d "%~dp0"

windres -i res\resource.rc -o res\resource.o -I res
if %ERRORLEVEL% NEQ 0 (
    echo Error compiling resources!
    pause
    exit /b %ERRORLEVEL%
)

gcc -Os -s -Wall -fno-asynchronous-unwind-tables -fno-ident -mwindows -static-libgcc -o CoreMonitor.exe src\main.c src\battery_monitor.c src\icon_generator.c src\settings.c src\battery_stats.c res\resource.o -lgdi32 -luser32 -lshell32 -lsetupapi -lm
if %ERRORLEVEL% NEQ 0 (
    echo Compilation failed!
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo Build Successful!
echo Output: CoreMonitor.exe (Size: ~47 KB)
echo.
pause
