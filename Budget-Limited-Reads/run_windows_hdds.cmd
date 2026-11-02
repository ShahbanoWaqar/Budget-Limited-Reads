@echo off
setlocal
set "BLR_BIN=%~dp0bin"
if not exist "%BLR_BIN%\blr_hdd.exe" set "BLR_BIN=%~dp0build-windows\Release"
if not exist "%BLR_BIN%\blr_hdd.exe" set "BLR_BIN=%~dp0build-windows"
if not exist "%BLR_BIN%\blr_hdd.exe" (
  echo HDD runner not found. Use the D-E Windows package or run build_windows.cmd.
  pause
  exit /b 1
)
"%BLR_BIN%\blr_hdd.exe" %*
exit /b %errorlevel%
