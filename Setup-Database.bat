@echo off
setlocal DisableDelayedExpansion
title TrinityCore 3.4.3 - Database Setup
color 0A
cd /d "%~dp0"

REM All setup logic lives in Setup-Database.ps1. Do not put IF ( ) blocks,
REM FOR /F loops, or parenthesized echo text in this file: cmd.exe parses
REM those blocks before running them and dies with
REM "- was unexpected at this time" on dashes after unescaped parentheses.

where powershell.exe >nul 2>&1
if errorlevel 1 goto :NOPS

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Setup-Database.ps1" %*
exit /b %ERRORLEVEL%

:NOPS
echo [ERROR] PowerShell was not found. It is required for database setup.
echo         Install Windows PowerShell 5.1 or PowerShell 7 and re-run.
pause
exit /b 1
