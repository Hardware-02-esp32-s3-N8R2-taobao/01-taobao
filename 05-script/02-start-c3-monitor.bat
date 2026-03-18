@echo off
setlocal

set "ROOT=D:\56-esp32\01-taobao"
set "APP_DIR=%ROOT%\06-app\01-c3-monitor"
set "VENV_DIR=%APP_DIR%\.venv"
set "PYTHON=py -3"

if not exist "%APP_DIR%" (
  echo App directory not found: %APP_DIR%
  pause
  exit /b 1
)

pushd "%APP_DIR%"

if not exist "%VENV_DIR%\Scripts\python.exe" (
  echo Creating virtual environment...
  %PYTHON% -m venv "%VENV_DIR%"
  if errorlevel 1 goto :fail
)

echo Installing dependencies...
"%VENV_DIR%\Scripts\python.exe" -m pip install --upgrade pip
if errorlevel 1 goto :fail
"%VENV_DIR%\Scripts\python.exe" -m pip install -r requirements.txt
if errorlevel 1 goto :fail

echo Starting ESP32-C3 monitor...
"%VENV_DIR%\Scripts\python.exe" main.py
set "EXIT_CODE=%ERRORLEVEL%"
popd
exit /b %EXIT_CODE%

:fail
echo.
echo Failed to prepare or start the monitor.
pause
popd
exit /b 1
