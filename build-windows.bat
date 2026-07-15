@echo off
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul || (
  echo ERROR: CMake was not found in PATH.
  echo Install Visual Studio 2022 with Desktop development with C++ and CMake tools.
  exit /b 1
)

cmake -S . -B build -A x64 -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b %errorlevel%
cmake --build build --config Release
if errorlevel 1 exit /b %errorlevel%

echo.
echo Build complete:
echo   build\Release\AKISoundStudio.exe
endlocal
