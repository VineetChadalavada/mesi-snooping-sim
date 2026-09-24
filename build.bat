@echo off
REM Build the Part 1 MESI snooping simulator without needing make.
REM Needs g++ on the PATH (MinGW-w64, e.g. C:\msys64\mingw64\bin).

setlocal

where g++ >nul 2>nul
if errorlevel 1 (
    echo.
    echo   g++ was not found on your PATH.
    echo   If you have MSYS2 installed, try:
    echo       set PATH=C:\msys64\mingw64\bin;%%PATH%%
    echo.
    exit /b 1
)

echo Building mesi_sim.exe ...
g++ -Wall -O1 -std=c++11 -o mesi_sim.exe ^
    main.cpp sim.cpp screens.cpp tests.cpp cache.cpp bus.cpp memory.cpp mesi.cpp ui.cpp

if errorlevel 1 (
    echo.
    echo   BUILD FAILED
    exit /b 1
)

echo.
echo   Built mesi_sim.exe
echo   Resize this window to at least 100x38, then run:  mesi_sim
echo.
endlocal
