@echo off
setlocal EnableDelayedExpansion
title TrinityCore 3.4.3 — Start Database
color 0A

echo ============================================================
echo   TrinityCore 3.4.3 — Start Database
echo ============================================================
echo.

set "ROOT=%~dp0"
set "DB_DIR=%ROOT%database"
set "MYSQLD=%DB_DIR%\bin\mysqld.exe"
set "MYSQL=%DB_DIR%\bin\mysql.exe"
set "MY_INI=%DB_DIR%\my.ini"
set "PORT=3306"

if not exist "%MYSQLD%" (
    echo [ERROR] MariaDB not found. Run Setup-Database.bat first.
    echo.
    pause
    goto :EOF
)

REM Check if already running
"%MYSQL%" -u trinity -ptrinity -e "SELECT 1" >nul 2>&1
if not errorlevel 1 (
    echo [OK] MariaDB is already running on port %PORT%.
    echo.
    pause
    goto :EOF
)

echo Starting MariaDB...
start "MariaDB" /B "%MYSQLD%" --defaults-file="%MY_INI%" --console 2>"%DB_DIR%\mysqld-error.log"

set /a WAIT=0
:WAIT_LOOP
timeout /t 1 /nobreak >nul
set /a WAIT+=1
"%MYSQL%" -u trinity -ptrinity -e "SELECT 1" >nul 2>&1
if errorlevel 1 (
    if !WAIT! LSS 30 goto :WAIT_LOOP
    echo [ERROR] MariaDB did not start within 30 seconds.
    echo         Check %DB_DIR%\mysqld-error.log
    pause
    goto :EOF
)

echo [OK] MariaDB is running on port %PORT%.
echo.
echo To stop it: run Stop-Database.bat or close this window.
echo.
pause
