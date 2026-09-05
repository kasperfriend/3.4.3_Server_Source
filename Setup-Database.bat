@echo off
setlocal EnableDelayedExpansion
title TrinityCore 3.4.3 — Database Setup
color 0A

echo ============================================================
echo   TrinityCore 3.4.3 — Portable Database Setup
echo ============================================================
echo.
echo This script will:
echo   1. Download MariaDB 10.11 LTS (portable, no install needed)
echo   2. Extract it into the "database" folder
echo   3. Initialize and start the database server
echo   4. Create all required databases
echo   5. Import SQL schemas (auth, characters, hotfixes, playerbot)
echo   6. Apply all database updates
echo   7. Configure worldserver.conf and bnetserver.conf
echo.
echo Press any key to continue, or Ctrl+C to cancel...
pause >nul

REM ── Paths ──────────────────────────────────────────────────
set "ROOT=%~dp0"
set "DB_DIR=%ROOT%database"
set "DB_DATA=%DB_DIR%\data"
set "DB_BIN=%DB_DIR%\bin"
set "MYSQL=%DB_BIN%\mysql.exe"
set "MYSQLD=%DB_BIN%\mysqld.exe"
set "MY_INI=%DB_DIR%\my.ini"
set "SQL_DIR=%ROOT%sql"
set "ETC_DIR=%ROOT%etc"
set "PORT=3306"
set "DB_USER=trinity"
set "DB_PASS=trinity"
set "DB_ROOT_PASS=rootpassword"

REM ── Check if already set up ────────────────────────────────
if exist "%MYSQLD%" (
    echo.
    echo [INFO] MariaDB already exists at: %DB_DIR%
    echo.
    set /p REINSTALL="Reinstall? This will DELETE the existing database folder. (y/N): "
    if /i "!REINSTALL!"=="y" (
        echo Stopping any running MariaDB instance...
        taskkill /f /im mysqld.exe >nul 2>&1
        timeout /t 2 /nobreak >nul
        echo Removing old database folder...
        rmdir /s /q "%DB_DIR%" 2>nul
    ) else (
        echo.
        echo Skipping download. Checking if server is running...
        goto :CHECK_RUNNING
    )
)

REM ── Step 1: Download MariaDB ───────────────────────────────
echo.
echo [1/7] Downloading MariaDB 10.11.10 portable...
echo.

set "MARIADB_URL=https://archive.mariadb.org/mariadb-10.11.10/winx64-packages/mariadb-10.11.10-winx64.zip"
set "MARIADB_ZIP=%ROOT%mariadb-download.zip"

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "Write-Host 'Downloading from archive.mariadb.org ...';" ^
    "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12;" ^
    "$ProgressPreference = 'SilentlyContinue';" ^
    "try {" ^
    "  Invoke-WebRequest -Uri '%MARIADB_URL%' -OutFile '%MARIADB_ZIP%' -UseBasicParsing;" ^
    "  Write-Host 'Download complete.';" ^
    "} catch {" ^
    "  Write-Host \"ERROR: Download failed: $_\";" ^
    "  exit 1;" ^
    "}"

if not exist "%MARIADB_ZIP%" (
    echo [ERROR] Download failed. Check your internet connection.
    echo         URL: %MARIADB_URL%
    goto :FAIL
)

REM ── Step 2: Extract ────────────────────────────────────────
echo.
echo [2/7] Extracting MariaDB to database\ ...
echo.

mkdir "%DB_DIR%" 2>nul

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "Expand-Archive -Path '%MARIADB_ZIP%' -DestinationPath '%ROOT%db-extract-tmp' -Force;" ^
    "$inner = Get-ChildItem '%ROOT%db-extract-tmp' -Directory | Select-Object -First 1;" ^
    "if ($inner) { Copy-Item -Recurse -Force \"$($inner.FullName)\*\" '%DB_DIR%\' };" ^
    "Remove-Item -Recurse -Force '%ROOT%db-extract-tmp' -ErrorAction SilentlyContinue;" ^
    "Remove-Item -Force '%MARIADB_ZIP%' -ErrorAction SilentlyContinue;" ^
    "Write-Host 'Extraction complete.'"

if not exist "%MYSQLD%" (
    echo [ERROR] mysqld.exe not found after extraction.
    echo         Expected at: %MYSQLD%
    goto :FAIL
)

REM ── Step 3: Create my.ini and initialize ────────────────────
echo.
echo [3/7] Initializing database...
echo.

REM Write a minimal my.ini
(
echo [mysqld]
echo basedir="%DB_DIR:\=/%"
echo datadir="%DB_DATA:\=/%"
echo port=%PORT%
echo bind-address=127.0.0.1
echo default-storage-engine=innodb
echo character-set-server=utf8mb4
echo collation-server=utf8mb4_general_ci
echo max_connections=200
echo innodb_buffer_pool_size=256M
echo innodb_log_file_size=48M
echo skip-name-resolve
echo.
echo [client]
echo port=%PORT%
echo default-character-set=utf8mb4
) > "%MY_INI%"

REM Initialize MariaDB data directory (creates system tables, root with no password)
"%MYSQLD%" --defaults-file="%MY_INI%" --initialize-insecure --console 2>&1
if errorlevel 1 (
    echo [ERROR] Database initialization failed.
    goto :FAIL
)

echo [OK] Database initialized.

REM ── Step 4: Start MariaDB ───────────────────────────────────
echo.
echo [4/7] Starting MariaDB server...
echo.

start "" /B "%MYSQLD%" --defaults-file="%MY_INI%" --console 2>"%DB_DIR%\mysqld-error.log"

REM Wait for the server to be ready
set /a WAIT_COUNT=0
:WAIT_LOOP
timeout /t 1 /nobreak >nul
set /a WAIT_COUNT+=1
"%MYSQL%" -u root --skip-password -e "SELECT 1" >nul 2>&1
if errorlevel 1 (
    if %WAIT_COUNT% LSS 30 goto :WAIT_LOOP
    echo [ERROR] MariaDB did not start within 30 seconds.
    echo         Check %DB_DIR%\mysqld-error.log for details.
    goto :FAIL
)

echo [OK] MariaDB is running on port %PORT%.

REM ── Step 5: Create databases ────────────────────────────────
echo.
echo [5/7] Creating databases and user...
echo.

REM Set root password
"%MYSQL%" -u root --skip-password -e "ALTER USER 'root'@'localhost' IDENTIFIED BY '%DB_ROOT_PASS%';" 2>nul

REM Create databases using the project's create script
"%MYSQL%" -u root -p%DB_ROOT_PASS% < "%SQL_DIR%\create\create_mysql.sql"
if errorlevel 1 (
    echo [WARN] create_mysql.sql had errors (may be OK if databases already exist).
)

REM Create the trinity user with full access
"%MYSQL%" -u root -p%DB_ROOT_PASS% -e "CREATE USER IF NOT EXISTS '%DB_USER%'@'localhost' IDENTIFIED BY '%DB_PASS%';"
"%MYSQL%" -u root -p%DB_ROOT_PASS% -e "CREATE USER IF NOT EXISTS '%DB_USER%'@'127.0.0.1' IDENTIFIED BY '%DB_PASS%';"
"%MYSQL%" -u root -p%DB_ROOT_PASS% -e "GRANT ALL PRIVILEGES ON `auth`.* TO '%DB_USER%'@'localhost', '%DB_USER%'@'127.0.0.1';"
"%MYSQL%" -u root -p%DB_ROOT_PASS% -e "GRANT ALL PRIVILEGES ON `characters`.* TO '%DB_USER%'@'localhost', '%DB_USER%'@'127.0.0.1';"
"%MYSQL%" -u root -p%DB_ROOT_PASS% -e "GRANT ALL PRIVILEGES ON `world`.* TO '%DB_USER%'@'localhost', '%DB_USER%'@'127.0.0.1';"
"%MYSQL%" -u root -p%DB_ROOT_PASS% -e "GRANT ALL PRIVILEGES ON `hotfixes`.* TO '%DB_USER%'@'localhost', '%DB_USER%'@'127.0.0.1';"
"%MYSQL%" -u root -p%DB_ROOT_PASS% -e "FLUSH PRIVILEGES;"

echo [OK] Databases created: auth, characters, world, hotfixes
echo [OK] User '%DB_USER%' created with password '%DB_PASS%'

REM ── Step 6: Import SQL schemas ──────────────────────────────
echo.
echo [6/7] Importing SQL schemas...
echo.

REM Base schemas
echo   Importing auth database...
"%MYSQL%" -u %DB_USER% -p%DB_PASS% auth < "%SQL_DIR%\base\auth_database.sql" 2>nul
if errorlevel 1 echo   [WARN] auth_database.sql had errors.

echo   Importing characters database...
"%MYSQL%" -u %DB_USER% -p%DB_PASS% characters < "%SQL_DIR%\base\characters_database.sql" 2>nul
if errorlevel 1 echo   [WARN] characters_database.sql had errors.

if exist "%SQL_DIR%\base\hotfixes_database.sql" (
    echo   Importing hotfixes database...
    "%MYSQL%" -u %DB_USER% -p%DB_PASS% hotfixes < "%SQL_DIR%\base\hotfixes_database.sql" 2>nul
    if errorlevel 1 echo   [WARN] hotfixes_database.sql had errors.
)

REM Playerbot custom tables
if exist "%SQL_DIR%\custom\playerbot\characters_playerbot.sql" (
    echo   Importing playerbot tables...
    "%MYSQL%" -u %DB_USER% -p%DB_PASS% characters < "%SQL_DIR%\custom\playerbot\characters_playerbot.sql" 2>nul
    if errorlevel 1 echo   [WARN] characters_playerbot.sql had errors.
)

REM Apply updates (sorted by filename for correct ordering)
echo   Applying database updates...
for /f "delims=" %%f in ('dir /b /s /a-d "%SQL_DIR%\updates\auth\*.sql" 2^>nul ^| sort') do (
    "%MYSQL%" -u %DB_USER% -p%DB_PASS% auth < "%%f" 2>nul
)
for /f "delims=" %%f in ('dir /b /s /a-d "%SQL_DIR%\updates\characters\*.sql" 2^>nul ^| sort') do (
    "%MYSQL%" -u %DB_USER% -p%DB_PASS% characters < "%%f" 2>nul
)
for /f "delims=" %%f in ('dir /b /s /a-d "%SQL_DIR%\updates\world\*.sql" 2^>nul ^| sort') do (
    "%MYSQL%" -u %DB_USER% -p%DB_PASS% world < "%%f" 2>nul
)
for /f "delims=" %%f in ('dir /b /s /a-d "%SQL_DIR%\updates\hotfixes\*.sql" 2^>nul ^| sort') do (
    "%MYSQL%" -u %DB_USER% -p%DB_PASS% hotfixes < "%%f" 2>nul
)

echo [OK] SQL import complete.

REM ── Step 7: Configure server ────────────────────────────────
echo.
echo [7/7] Configuring server files...
echo.

REM Copy .dist files to actual config files if they don't exist
if not exist "%ETC_DIR%\worldserver.conf" (
    if exist "%ETC_DIR%\worldserver.conf.dist" (
        copy "%ETC_DIR%\worldserver.conf.dist" "%ETC_DIR%\worldserver.conf" >nul
        echo   Created etc\worldserver.conf from .dist template
    )
)
if not exist "%ETC_DIR%\bnetserver.conf" (
    if exist "%ETC_DIR%\bnetserver.conf.dist" (
        copy "%ETC_DIR%\bnetserver.conf.dist" "%ETC_DIR%\bnetserver.conf" >nul
        echo   Created etc\bnetserver.conf from .dist template
    )
)

REM Update worldserver.conf with correct database credentials
if exist "%ETC_DIR%\worldserver.conf" (
    powershell -NoProfile -ExecutionPolicy Bypass -Command ^
        "$f = '%ETC_DIR%\worldserver.conf';" ^
        "$c = [IO.File]::ReadAllText($f);" ^
        "$c = $c -replace 'LoginDatabaseInfo\s*=\s*\"[^\"]*\"', 'LoginDatabaseInfo     = \"127.0.0.1;%PORT%;%DB_USER%;%DB_PASS%;auth\"';" ^
        "$c = $c -replace 'WorldDatabaseInfo\s*=\s*\"[^\"]*\"', 'WorldDatabaseInfo     = \"127.0.0.1;%PORT%;%DB_USER%;%DB_PASS%;world\"';" ^
        "$c = $c -replace 'CharacterDatabaseInfo\s*=\s*\"[^\"]*\"', 'CharacterDatabaseInfo = \"127.0.0.1;%PORT%;%DB_USER%;%DB_PASS%;characters\"';" ^
        "[IO.File]::WriteAllText($f, $c);" ^
        "Write-Host '  Updated etc\worldserver.conf'"
)

REM Update bnetserver.conf with correct database credentials
if exist "%ETC_DIR%\bnetserver.conf" (
    powershell -NoProfile -ExecutionPolicy Bypass -Command ^
        "$f = '%ETC_DIR%\bnetserver.conf';" ^
        "$c = [IO.File]::ReadAllText($f);" ^
        "$c = $c -replace 'LoginDatabaseInfo\s*=\s*\"[^\"]*\"', 'LoginDatabaseInfo = \"127.0.0.1;%PORT%;%DB_USER%;%DB_PASS%;auth\"';" ^
        "[IO.File]::WriteAllText($f, $c);" ^
        "Write-Host '  Updated etc\bnetserver.conf'"
)

echo [OK] Configuration files updated.

REM ── Done ────────────────────────────────────────────────────
echo.
echo ============================================================
echo   Setup complete!
echo ============================================================
echo.
echo   Database:    MariaDB 10.11 running on 127.0.0.1:%PORT%
echo   User:        %DB_USER%
echo   Password:    %DB_PASS%
echo   Databases:   auth, characters, world, hotfixes
echo   Data dir:    %DB_DIR%\data
echo.
echo   Server config files have been updated:
echo     etc\worldserver.conf
echo     etc\bnetserver.conf
echo.
echo   IMPORTANT: The "world" database is empty. You need to import
echo   a TDB (Trinity Database) world dump for your server to work.
echo   Get it from your TrinityCore provider or community.
echo.
echo   To stop the database:  taskkill /f /im mysqld.exe
echo   To start it again:     Run Start-Database.bat
echo.
echo Press any key to exit...
pause >nul
goto :EOF

:CHECK_RUNNING
REM Check if MariaDB is already running
"%MYSQL%" -u %DB_USER% -p%DB_PASS% -e "SELECT 1" >nul 2>&1
if errorlevel 1 (
    echo MariaDB is not running. Starting it...
    if exist "%MYSQLD%" (
        start "" /B "%MYSQLD%" --defaults-file="%MY_INI%" --console 2>"%DB_DIR%\mysqld-error.log"
        set /a WAIT_COUNT=0
        :WAIT_LOOP2
        timeout /t 1 /nobreak >nul
        set /a WAIT_COUNT+=1
        "%MYSQL%" -u %DB_USER% -p%DB_PASS% -e "SELECT 1" >nul 2>&1
        if errorlevel 1 (
            if !WAIT_COUNT! LSS 30 goto :WAIT_LOOP2
            echo [ERROR] MariaDB did not start. Check %DB_DIR%\mysqld-error.log
            goto :FAIL
        )
        echo [OK] MariaDB is running.
    ) else (
        echo [ERROR] mysqld.exe not found. Run this script from scratch.
        goto :FAIL
    )
) else (
    echo [OK] MariaDB is already running.
)

REM Still update config files
goto :STEP7_ONLY

:STEP7_ONLY
echo.
echo Updating configuration files...
goto :STEP7_CONFIG

:STEP7_CONFIG
if exist "%ETC_DIR%\worldserver.conf" (
    powershell -NoProfile -ExecutionPolicy Bypass -Command ^
        "$f = '%ETC_DIR%\worldserver.conf';" ^
        "$c = [IO.File]::ReadAllText($f);" ^
        "$c = $c -replace 'LoginDatabaseInfo\s*=\s*\"[^\"]*\"', 'LoginDatabaseInfo     = \"127.0.0.1;%PORT%;%DB_USER%;%DB_PASS%;auth\"';" ^
        "$c = $c -replace 'WorldDatabaseInfo\s*=\s*\"[^\"]*\"', 'WorldDatabaseInfo     = \"127.0.0.1;%PORT%;%DB_USER%;%DB_PASS%;world\"';" ^
        "$c = $c -replace 'CharacterDatabaseInfo\s*=\s*\"[^\"]*\"', 'CharacterDatabaseInfo = \"127.0.0.1;%PORT%;%DB_USER%;%DB_PASS%;characters\"';" ^
        "[IO.File]::WriteAllText($f, $c);" ^
        "Write-Host '  Updated etc\worldserver.conf'"
)
if exist "%ETC_DIR%\bnetserver.conf" (
    powershell -NoProfile -ExecutionPolicy Bypass -Command ^
        "$f = '%ETC_DIR%\bnetserver.conf';" ^
        "$c = [IO.File]::ReadAllText($f);" ^
        "$c = $c -replace 'LoginDatabaseInfo\s*=\s*\"[^\"]*\"', 'LoginDatabaseInfo = \"127.0.0.1;%PORT%;%DB_USER%;%DB_PASS%;auth\"';" ^
        "[IO.File]::WriteAllText($f, $c);" ^
        "Write-Host '  Updated etc\bnetserver.conf'"
)
echo [OK] Done.
echo.
echo Press any key to exit...
pause >nul
goto :EOF

:FAIL
echo.
echo ============================================================
echo   Setup FAILED — see error messages above.
echo ============================================================
echo.
echo Common fixes:
echo   - Check your internet connection (download may have failed)
echo   - Make sure port %PORT% is not in use by another MySQL instance
echo   - Run this script as Administrator if you get permission errors
echo   - Delete the "database" folder and try again
echo.
echo Press any key to exit...
pause >nul
