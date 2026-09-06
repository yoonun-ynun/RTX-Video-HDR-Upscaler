@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\setup-runtime.ps1" -Destination "%~dp0."
if errorlevel 1 (
  echo Runtime installation failed. See the error above.
  pause
  exit /b 1
)
start "" "%~dp0RTXVideoHDR.exe"
