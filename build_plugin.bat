@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "build_plugin.ps1" %*
if %ERRORLEVEL% neq 0 (
    echo [ERROR] IMGUIExtension build failed with exit code %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)
endlocal
