@echo off
setlocal
set "BLR_BIN=%~dp0bin"
if not exist "%BLR_BIN%\blr.exe" set "BLR_BIN=%~dp0build-windows\Release"
if not exist "%BLR_BIN%\blr.exe" set "BLR_BIN=%~dp0build-windows"
if not exist "%BLR_BIN%\blr.exe" (
  echo No Windows executables found. Run build_windows.cmd or use the Windows download package.
  exit /b 1
)
set "BLR_DEMO_OUTPUT=%TEMP%\blr-demo-%RANDOM%-%RANDOM%"
mkdir "%BLR_DEMO_OUTPUT%"
if errorlevel 1 exit /b 1
"%BLR_BIN%\gen_trace.exe" 1048576 100 1 42 "%BLR_DEMO_OUTPUT%\trace.csv"
if errorlevel 1 exit /b 1
for %%M in (rr lessbusy fixed budget) do (
  echo Running %%M
  "%BLR_BIN%\blr.exe" --fake --mode %%M --trace "%BLR_DEMO_OUTPUT%\trace.csv" --delay-us 10 --permits 1 --threshold 8 --max-outstanding 16 --out-prefix "%BLR_DEMO_OUTPUT%\%%M"
  if errorlevel 1 exit /b 1
)
echo All four simulator modes completed. Logs: %BLR_DEMO_OUTPUT%
