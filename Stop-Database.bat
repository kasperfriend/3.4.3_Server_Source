@echo off
title TrinityCore 3.4.3 — Stop Database
color 0C

echo Stopping MariaDB...
tasklist | findstr /i "mysqld.exe" >nul 2>&1
if errorlevel 1 (
    echo [OK] MariaDB is not running.
) else (
    taskkill /f /im mysqld.exe >nul 2>&1
    timeout /t 2 /nobreak >nul
    echo [OK] MariaDB stopped.
)
echo.
pause
