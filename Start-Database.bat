@echo off
setlocal DisableDelayedExpansion
title TrinityCore 3.4.3 - Start Database
color 0A
cd /d "%~dp0"

REM Logic lives in Start-Database.ps1. This file is a launcher only so that
REM cmd.exe never parses parenthesized IF/FOR blocks.

where powershell.exe >nul 2>&1
if errorlevel 1 goto :NOPS

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-Database.ps1" %*
exit /b %ERRORLEVEL%

:NOPS
echo [ERROR] PowerShell was not found. It is required to start the database.
pause
exit /b 1
