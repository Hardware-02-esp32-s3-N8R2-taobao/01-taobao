@echo off
for /f "tokens=2 delims=:" %%a in ('ipconfig ^| findstr /R /C:"IPv4.*:"') do (
  echo %%a
  goto :eof
)
