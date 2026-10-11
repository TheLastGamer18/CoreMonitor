@echo off
echo Building Core Monitor...
cd /d "%~dp0"

windres -i res\resource.rc -o res\resource.o -I res
if %ERRORLEVEL% NEQ 0 (
    echo Error compiling resources!
    pause
    exit /b %ERRORLEVEL%
)

gcc -Os -s -Wall -flto -fmerge-all-constants -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-ident -fomit-frame-pointer -falign-functions=1 -falign-jumps=1 -falign-loops=1 -mwindows -static-libgcc -Wl,--gc-sections -Wl,--file-alignment,512 -o CoreMonitor.exe src\main.c src\battery_monitor.c src\icon_generator.c src\settings.c src\battery_stats.c res\resource.o -lgdi32 -luser32 -lshell32 -lsetupapi
if %ERRORLEVEL% NEQ 0 (
    echo Compilation failed!
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo Build Successful!
echo Output: CoreMonitor.exe
echo.
pause
