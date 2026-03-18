@echo off
set NODE_PATH=C:\Program Files\nodejs\node.exe

if not exist "%NODE_PATH%" (
  echo Node.js not found at %NODE_PATH%
  exit /b 1
)

cd /d "%~dp0"
"%NODE_PATH%" server.js
