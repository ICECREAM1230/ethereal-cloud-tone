@echo off
setlocal

set "PROJECT_ROOT=%~dp0..\.."

cd /d "%PROJECT_ROOT%"

where pythonw.exe >nul 2>&1
if not errorlevel 1 (
    start "" pythonw.exe -B -m host.pcm_measurement_reference.pcm_capture.app
    exit /b 0
)

where python.exe >nul 2>&1
if not errorlevel 1 (
    python.exe -B -m host.pcm_measurement_reference.pcm_capture.app
    exit /b %ERRORLEVEL%
)

echo Could not find pythonw.exe or python.exe on PATH.
echo Activate the Python environment for CH569 PCM capture and try again.
pause
exit /b 1
