@echo off
setlocal

set "ROOT=%~dp0"
set "PYTHON="

where py >nul 2>&1 && set "PYTHON=py -3"
if not defined PYTHON where python >nul 2>&1 && set "PYTHON=python"
if not defined PYTHON where python3 >nul 2>&1 && set "PYTHON=python3"
if not defined PYTHON (
  echo 未找到 Python，请先安装 Python:
  echo https://www.python.org/downloads/
  pause
  exit /b 1
)

pushd "%ROOT%"
%PYTHON% "打开网页.py"
set "EXIT_CODE=%ERRORLEVEL%"
popd
exit /b %EXIT_CODE%
