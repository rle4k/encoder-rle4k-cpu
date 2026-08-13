@echo off
REM Thin wrapper for publish-deps.ps1
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0publish-deps.ps1" %*
exit /b %ERRORLEVEL%
