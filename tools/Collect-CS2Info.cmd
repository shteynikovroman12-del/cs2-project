@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Collect-CS2Info.ps1" -OutputPath "%~dp0CS2-client-info.json"
if errorlevel 1 echo Report creation failed. See the message above.
pause
