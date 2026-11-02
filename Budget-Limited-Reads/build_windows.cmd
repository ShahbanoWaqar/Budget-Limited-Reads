@echo off
setlocal
where cmake >nul 2>nul
if errorlevel 1 (
  echo CMake was not found. Install Visual Studio Build Tools with Desktop development with C++ and CMake tools.
  exit /b 1
)
cmake -S "%~dp0." -B "%~dp0build-windows" -DBLR_SANITIZERS=OFF
if errorlevel 1 exit /b %errorlevel%
cmake --build "%~dp0build-windows" --config Release --parallel
if errorlevel 1 exit /b %errorlevel%
ctest --test-dir "%~dp0build-windows" -C Release --output-on-failure
if errorlevel 1 exit /b %errorlevel%
echo Windows build and tests completed. Visual Studio executables are in build-windows\Release.
