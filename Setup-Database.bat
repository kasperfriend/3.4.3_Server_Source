@echo off
setlocal EnableDelayedExpansion
title TrinityCore 3.4.3 - Database Setup
color 0A

echo ============================================================
echo   TrinityCore 3.4.3 - Portable Database Setup
echo ============================================================
echo.
echo This script will:
echo   1. Download MariaDB 10.11 LTS (portable, no install needed)
echo   2. Extract it into the "database" folder
echo   3. Initialize and start the database server
echo   4. Create all required databases
echo   5. Import SQL schemas (auth, characters, playerbot)
echo   6. Download + verify + import the world and hotfixes game data
echo      (WyrmrestCore DB release), then apply database updates
echo   7. Configure worldserver.conf and bnetserver.conf
echo.
echo Press any key to continue, or Ctrl+C to cancel...
pause >nul

REM Paths
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

REM World / hotfixes game data - downloaded from the WyrmrestCore release and
REM verified against their published SHA-256 checksums before import.
set "DB_DATA_RELEASE=https://github.com/xHashii/WyrmrestCore/releases/download/DB.2608"
set "WORLD_DUMP=world_full_2026_08_10.sql"
set "HOTFIXES_DUMP=hotfixes_full_2026_08_10.sql"
set "WORLD_DUMP_SHA256=91401028dce1dc302e12709a268e4d46cb74f527f95396e2dea1ea5da17081de"
set "HOTFIXES_DUMP_SHA256=20170a1a52a93af556f4a875885cf2e462777f3a5a3b58e8b128bc171ea866dc"
set "DL_DIR=%ROOT%db-downloads"

REM Check if already set up
if exist "%MYSQLD%" (
    echo.
    echo [INFO] MariaDB already exists at: !DB_DIR!
    echo.
    set /p REINSTALL="Reinstall? This will DELETE the existing database folder. (y/N): "
    if /i "!REINSTALL!"=="y" (
        echo Stopping any running MariaDB instance...
        taskkill /f /im mysqld.exe >nul 2>&1
        timeout /t 2 /nobreak >nul
        echo Removing old database folder...
        rmdir /s /q "!DB_DIR!" 2>nul
    ) else (
        echo.
        echo Skipping download. Checking if server is running...
        goto :CHECK_RUNNING
    )
)

REM Step 1: Download MariaDB
echo.
echo [1/7] Downloading MariaDB 10.11.10 portable...
echo.

set "MARIADB_URL=https://archive.mariadb.org/mariadb-10.11.10/winx64-packages/mariadb-10.11.10-winx64.zip"
set "MARIADB_ZIP=%ROOT%mariadb-download.zip"

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "Write-Host 'Downloading from archive.mariadb.org ...';"^
    "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12;"^
    "$ProgressPreference = 'SilentlyContinue';"^
    "try {"^
    "  Invoke-WebRequest -Uri '%MARIADB_URL%' -OutFile '%MARIADB_ZIP%' -UseBasicParsing;"^
    "  Write-Host 'Download complete.';"^
    "} catch {"^
    "  Write-Host \"ERROR: Download failed: $_\";"^
    "  exit 1;"^
    "}"

if not exist "%MARIADB_ZIP%" (
    echo [ERROR] Download failed. Check your internet connection.
    echo         URL: %MARIADB_URL%
    goto :FAIL
)

REM Step 2: Extract
echo.
echo [2/7] Extracting MariaDB to database\ ...
echo.

mkdir "!DB_DIR!" 2>nul

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "Expand-Archive -Path '%MARIADB_ZIP%' -DestinationPath '%ROOT%db-extract-tmp' -Force;"^
    "$inner = Get-ChildItem '%ROOT%db-extract-tmp' -Directory | Select-Object -First 1;"^
    "if ($inner) { Copy-Item -Recurse -Force \"$($inner.FullName)\*\" '!DB_DIR!\' };"^
    "Remove-Item -Recurse -Force '%ROOT%db-extract-tmp' -ErrorAction SilentlyContinue;"^
    "Remove-Item -Force '%MARIADB_ZIP%' -ErrorAction SilentlyContinue;"^
    "Write-Host 'Extraction complete.'"

if not exist "!MYSQLD!" (
    echo [ERROR] mysqld.exe not found after extraction.
    echo         Expected at: !MYSQLD!
    goto :FAIL
)

REM Step 3: Create my.ini and initialize
echo.
echo [3/7] Initializing database...
echo.

REM Write a minimal my.ini (do not use a parenthesized block to avoid parse-time expansion issues)
> "!MY_INI!" echo [mysqld]
>> "!MY_INI!" echo basedir="!DB_DIR:\=/%!"
>> "!MY_INI!" echo datadir="!DB_DATA:\=/%!"
>> "!MY_INI!" echo port=!PORT!
>> "!MY_INI!" echo bind-address=127.0.0.1
>> "!MY_INI!" echo default-storage-engine=innodb
>> "!MY_INI!" echo character-set-server=utf8mb4
>> "!MY_INI!" echo collation-server=utf8mb4_general_ci
>> "!MY_INI!" echo max_connections=200
>> "!MY_INI!" echo innodb_buffer_pool_size=256M
>> "!MY_INI!" echo innodb_log_file_size=48M
>> "!MY_INI!" echo skip-name-resolve
>> "!MY_INI!" echo.
>> "!MY_INI!" echo [client]
>> "!MY_INI!" echo port=!PORT!
>> "!MY_INI!" echo default-character-set=utf8mb4

if exist "!DB_DATA!" rmdir /s /q "!DB_DATA!" 2>nul
mkdir "!DB_DATA!" 2>nul

set "INSTALL_DB=!DB_BIN!\mariadb-install-db.exe"
if not exist "!INSTALL_DB!" set "INSTALL_DB=!DB_BIN!\mysql_install_db.exe"
if not exist "!INSTALL_DB!" (
    echo [ERROR] Database installer not found. Expected one of:
    echo         !DB_BIN!\mariadb-install-db.exe
    echo         !DB_BIN!\mysql_install_db.exe
    echo         The MariaDB download may be incomplete. Delete the "database"
    echo         folder and run this script again.
    goto :FAIL
)

echo   Running mariadb-install-db.exe (this can take a minute)...
"!INSTALL_DB!" --datadir="!DB_DATA!" --password="!DB_ROOT_PASS!" --port=!PORT!
if errorlevel 1 (
    echo [ERROR] Database initialization failed.
    goto :FAIL
)
if not exist "!DB_DATA!\mysql" (
    echo [ERROR] Database initialization failed - system tables were not created.
    echo         Check the messages above. You may need to delete the "database"
    echo         folder and run this script again.
    goto :FAIL
)

echo [OK] Database initialized (root password set).

REM Step 4: Start MariaDB
echo.
echo [4/7] Starting MariaDB server...
echo.

start "" /B "!MYSQLD!" --defaults-file="!MY_INI!" --console 2>"!DB_DIR!\mysqld-error.log"

REM Wait for the server to be ready
set /a WAIT_COUNT=0
:WAIT_LOOP
timeout /t 1 /nobreak >nul
set /a WAIT_COUNT+=1
"!MYSQL!" -u root -p"!DB_ROOT_PASS!" -e "SELECT 1" >nul 2>&1
if errorlevel 1 (
    if !WAIT_COUNT! LSS 30 goto :WAIT_LOOP
    echo [ERROR] MariaDB did not start within 30 seconds.
    echo         Check !DB_DIR!\mysqld-error.log for details.
    goto :FAIL
)

echo [OK] MariaDB is running on port !PORT!.

REM Step 5: Create databases
echo.
echo [5/7] Creating databases and user...
echo.

call :ENSURE_DATABASES

echo [OK] Databases created: auth, characters, world, hotfixes
echo [OK] User '!DB_USER!' created with password '!DB_PASS!'

REM Step 6: Import SQL schemas
echo.
echo [6/7] Importing SQL schemas and game data...
echo.

call :IMPORT_ALL_SQL
if errorlevel 1 goto :FAIL

echo [OK] SQL import complete.

REM Step 7: Configure server
echo.
echo [7/7] Configuring server files...
echo.

call :STEP7_CONFIG
:STEP7_DONE
echo [OK] Configuration files updated.

REM Done
echo.
echo ============================================================
echo   Setup complete!
echo ============================================================
echo.
echo   Database:    MariaDB 10.11 running on 127.0.0.1:!PORT!
echo   User:        !DB_USER!
echo   Password:    !DB_PASS!
echo   Databases:   auth, characters, world, hotfixes
echo   Data dir:    !DB_DIR!\data
echo.
echo   Server config files have been updated:
echo     etc\worldserver.conf
echo     etc\bnetserver.conf
echo     bin\worldserver.conf
echo     bin\bnetserver.conf
echo.
echo   Game data downloaded, checksum-verified and imported:
echo     world     ^<- %WORLD_DUMP%
echo     hotfixes  ^<- %HOTFIXES_DUMP%
echo.
echo   To stop the database:  taskkill /f /im mysqld.exe
echo   To start it again:     Run Start-Database.bat
echo.
echo Press any key to exit...
pause >nul
goto :EOF

REM ------------------------------------------------------------------
REM Create the four databases and the trinity user (idempotent).
REM ------------------------------------------------------------------
:ENSURE_DATABASES
"!MYSQL!" -u root -p!DB_ROOT_PASS! < "!SQL_DIR!\create\create_mysql.sql"
if errorlevel 1 echo   [WARN] create_mysql.sql had errors (may be OK if databases already exist).

"!MYSQL!" -u root -p!DB_ROOT_PASS! -e "CREATE USER IF NOT EXISTS '!DB_USER!'@'localhost' IDENTIFIED BY '!DB_PASS!';"
"!MYSQL!" -u root -p!DB_ROOT_PASS! -e "CREATE USER IF NOT EXISTS '!DB_USER!'@'127.0.0.1' IDENTIFIED BY '!DB_PASS!';"
"!MYSQL!" -u root -p!DB_ROOT_PASS! -e "GRANT ALL PRIVILEGES ON `auth`.* TO '!DB_USER!'@'localhost', '!DB_USER!'@'127.0.0.1';"
"!MYSQL!" -u root -p!DB_ROOT_PASS! -e "GRANT ALL PRIVILEGES ON `characters`.* TO '!DB_USER!'@'localhost', '!DB_USER!'@'127.0.0.1';"
"!MYSQL!" -u root -p!DB_ROOT_PASS! -e "GRANT ALL PRIVILEGES ON `world`.* TO '!DB_USER!'@'localhost', '!DB_USER!'@'127.0.0.1';"
"!MYSQL!" -u root -p!DB_ROOT_PASS! -e "GRANT ALL PRIVILEGES ON `hotfixes`.* TO '!DB_USER!'@'localhost', '!DB_USER!'@'127.0.0.1';"
"!MYSQL!" -u root -p!DB_ROOT_PASS! -e "FLUSH PRIVILEGES;"
goto :EOF

REM ------------------------------------------------------------------
REM Import the base schemas, the world + hotfixes game data, and the
REM incremental updates. Safe to re-run on an existing install.
REM ------------------------------------------------------------------
:IMPORT_ALL_SQL
echo   Importing auth database...
"!MYSQL!" -u !DB_USER! -p!DB_PASS! auth < "!SQL_DIR!\base\auth_database.sql" 2>nul
if errorlevel 1 echo   [WARN] auth_database.sql had errors.

echo   Importing characters database...
"!MYSQL!" -u !DB_USER! -p!DB_PASS! characters < "!SQL_DIR!\base\characters_database.sql" 2>nul
if errorlevel 1 echo   [WARN] characters_database.sql had errors.

REM The hotfixes database is created and populated by the hotfixes_full_*.sql
REM download (it ships both the schema and the data), so there is no separate
REM hotfixes base import here.

if exist "!SQL_DIR!\custom\playerbot\characters_playerbot.sql" (
    echo   Importing playerbot tables...
    "!MYSQL!" -u !DB_USER! -p!DB_PASS! characters < "!SQL_DIR!\custom\playerbot\characters_playerbot.sql" 2>nul
    if errorlevel 1 echo   [WARN] characters_playerbot.sql had errors.
)

call :IMPORT_GAMEDATA
if errorlevel 1 goto :FAIL
call :APPLY_UPDATES
goto :EOF

REM ------------------------------------------------------------------
REM Download, verify and import the world + hotfixes game data from the
REM WyrmrestCore DB release. Each file is checked against its published
REM SHA-256 checksum before it is imported. A database is only imported
REM when it is still empty, so re-running is safe.
REM ------------------------------------------------------------------
:IMPORT_GAMEDATA
mkdir "!DL_DIR!" 2>nul

REM --- world ---
REM IMPORTANT: never put a quoted program inside for /f ('...') - cmd.exe
REM re-runs that command through a child "cmd /c" process which corrupts the
REM quotes and dies with "... was unexpected at this time.". Instead: run
REM mysql as a plain command, capture its output in a temp file, then read
REM that file back with for /f "usebackq" (file mode - no child cmd.exe, no
REM quote problems, and it works even when the folder path contains spaces).
set "WORLD_ROWS=0"
"!MYSQL!" -u !DB_USER! -p!DB_PASS! --batch --skip-column-names -e "SELECT COUNT(*) FROM world.version" >"!DL_DIR!\rowcount.tmp" 2>nul
for /f "usebackq delims=" %%C in ("!DL_DIR!\rowcount.tmp") do set "WORLD_ROWS=%%C"
del "!DL_DIR!\rowcount.tmp" >nul 2>&1
if not "!WORLD_ROWS!"=="0" (
    echo   World data already present (!WORLD_ROWS! rows in world.version) - skipping world import.
    goto :IMPORT_HOTFIXES
)

echo   Downloading %WORLD_DUMP% (~200 MB, may take several minutes)...
if exist "!DL_DIR!\%WORLD_DUMP%" (
    echo   %WORLD_DUMP% already present, skipping download.
) else (
    powershell -NoProfile -ExecutionPolicy Bypass -Command ^
        "$ProgressPreference = 'SilentlyContinue';" ^
        "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12;" ^
        "try { Invoke-WebRequest -Uri '%DB_DATA_RELEASE%/%WORLD_DUMP%' -OutFile '!DL_DIR!\%WORLD_DUMP%' -UseBasicParsing; Write-Host '  Download complete.' }" ^
        "catch { Write-Host (\"ERROR: world dump download failed: \" + $_.Exception.Message); exit 1 }"
    if errorlevel 1 (
        echo [ERROR] Failed to download %WORLD_DUMP%.
        echo         URL: %DB_DATA_RELEASE%/%WORLD_DUMP%
        goto :FAIL
    )
)

echo   Verifying %WORLD_DUMP% checksum (SHA-256)...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$h = (Get-FileHash -Algorithm SHA256 -Path '!DL_DIR!\%WORLD_DUMP%').Hash.ToLower();" ^
    "if ($h -ne '%WORLD_DUMP_SHA256%') { Write-Host (\"ERROR: SHA-256 mismatch for %WORLD_DUMP%.`n  Expected: %WORLD_DUMP_SHA256%`n  Actual:   \" + $h); Remove-Item -Force '!DL_DIR!\%WORLD_DUMP%' -ErrorAction SilentlyContinue; exit 1 } else { Write-Host '  Checksum OK.' }"
if errorlevel 1 (
    echo [ERROR] Checksum verification failed for %WORLD_DUMP%.
    echo         The corrupted file was deleted - re-run this script to re-download it.
    goto :FAIL
)

echo   Preparing a clean 'world' database...
"!MYSQL!" -u root -p!DB_ROOT_PASS! -e "DROP DATABASE IF EXISTS world; CREATE DATABASE world DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci;"
if errorlevel 1 (
    echo [ERROR] Could not recreate the 'world' database.
    goto :FAIL
)

echo   Importing %WORLD_DUMP% into the 'world' database (this can take several minutes)...
"!MYSQL!" -u !DB_USER! -p!DB_PASS! world < "!DL_DIR!\%WORLD_DUMP%"
if errorlevel 1 (
    echo [ERROR] Failed to import %WORLD_DUMP% into the 'world' database.
    goto :FAIL
)

echo   Verifying world database contents...
"!MYSQL!" -u !DB_USER! -p!DB_PASS! --batch --skip-column-names -e "SELECT COUNT(*) FROM world.version" 2>nul
if errorlevel 1 (
    echo [ERROR] The 'world' database is missing its 'version' table after import.
    echo         The downloaded world dump may be incompatible with this server build.
    goto :FAIL
)
echo   [OK] world data imported and verified.

:IMPORT_HOTFIXES
REM Same pattern as the world check above: capture mysql output in a temp
REM file - never run a quoted program inside for /f ('...').
set "HOTFIX_ROWS=0"
"!MYSQL!" -u !DB_USER! -p!DB_PASS! --batch --skip-column-names -e "SELECT COUNT(*) FROM hotfixes.achievement" >"!DL_DIR!\rowcount.tmp" 2>nul
for /f "usebackq delims=" %%C in ("!DL_DIR!\rowcount.tmp") do set "HOTFIX_ROWS=%%C"
del "!DL_DIR!\rowcount.tmp" >nul 2>&1
if not "!HOTFIX_ROWS!"=="0" (
    echo   Hotfixes data already present (!HOTFIX_ROWS! rows in achievement) - skipping hotfixes import.
    goto :EOF
)

echo   Downloading %HOTFIXES_DUMP% (~100 MB, may take a few minutes)...
if exist "!DL_DIR!\%HOTFIXES_DUMP%" (
    echo   %HOTFIXES_DUMP% already present, skipping download.
) else (
    powershell -NoProfile -ExecutionPolicy Bypass -Command ^
        "$ProgressPreference = 'SilentlyContinue';" ^
        "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12;" ^
        "try { Invoke-WebRequest -Uri '%DB_DATA_RELEASE%/%HOTFIXES_DUMP%' -OutFile '!DL_DIR!\%HOTFIXES_DUMP%' -UseBasicParsing; Write-Host '  Download complete.' }" ^
        "catch { Write-Host (\"ERROR: hotfixes dump download failed: \" + $_.Exception.Message); exit 1 }"
    if errorlevel 1 (
        echo [ERROR] Failed to download %HOTFIXES_DUMP%.
        echo         URL: %DB_DATA_RELEASE%/%HOTFIXES_DUMP%
        goto :FAIL
    )
)

echo   Verifying %HOTFIXES_DUMP% checksum (SHA-256)...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$h = (Get-FileHash -Algorithm SHA256 -Path '!DL_DIR!\%HOTFIXES_DUMP%').Hash.ToLower();" ^
    "if ($h -ne '%HOTFIXES_DUMP_SHA256%') { Write-Host (\"ERROR: SHA-256 mismatch for %HOTFIXES_DUMP%.`n  Expected: %HOTFIXES_DUMP_SHA256%`n  Actual:   \" + $h); Remove-Item -Force '!DL_DIR!\%HOTFIXES_DUMP%' -ErrorAction SilentlyContinue; exit 1 } else { Write-Host '  Checksum OK.' }"
if errorlevel 1 (
    echo [ERROR] Checksum verification failed for %HOTFIXES_DUMP%.
    echo         The corrupted file was deleted - re-run this script to re-download it.
    goto :FAIL
)

echo   Preparing a clean 'hotfixes' database...
"!MYSQL!" -u root -p!DB_ROOT_PASS! -e "DROP DATABASE IF EXISTS hotfixes; CREATE DATABASE hotfixes DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci;"
if errorlevel 1 (
    echo [ERROR] Could not recreate the 'hotfixes' database.
    goto :FAIL
)

echo   Importing %HOTFIXES_DUMP% into the 'hotfixes' database (this can take a few minutes)...
"!MYSQL!" -u !DB_USER! -p!DB_PASS! hotfixes < "!DL_DIR!\%HOTFIXES_DUMP%"
if errorlevel 1 (
    echo [ERROR] Failed to import %HOTFIXES_DUMP% into the 'hotfixes' database.
    goto :FAIL
)

echo   Verifying hotfixes database contents...
"!MYSQL!" -u !DB_USER! -p!DB_PASS! --batch --skip-column-names -e "SELECT COUNT(*) FROM hotfixes.achievement" 2>nul
if errorlevel 1 (
    echo [ERROR] The 'hotfixes' database is missing its 'achievement' table after import.
    echo         The downloaded hotfixes dump may be incompatible with this server build.
    goto :FAIL
)
echo   [OK] hotfixes data imported and verified.
goto :EOF

REM ------------------------------------------------------------------
REM Apply the project's incremental SQL updates (sorted by filename).
REM ------------------------------------------------------------------
:APPLY_UPDATES
echo   Applying database updates...
for /f "delims=" %%f in ('dir /b /s /a-d "!SQL_DIR!\updates\auth\*.sql" 2^>nul ^| sort') do (
    "!MYSQL!" -u !DB_USER! -p!DB_PASS! auth < "%%f" 2>nul
)
for /f "delims=" %%f in ('dir /b /s /a-d "!SQL_DIR!\updates\characters\*.sql" 2^>nul ^| sort') do (
    "!MYSQL!" -u !DB_USER! -p!DB_PASS! characters < "%%f" 2>nul
)
for /f "delims=" %%f in ('dir /b /s /a-d "!SQL_DIR!\updates\world\*.sql" 2^>nul ^| sort') do (
    "!MYSQL!" -u !DB_USER! -p!DB_PASS! world < "%%f" 2>nul
)
for /f "delims=" %%f in ('dir /b /s /a-d "!SQL_DIR!\updates\hotfixes\*.sql" 2^>nul ^| sort') do (
    "!MYSQL!" -u !DB_USER! -p!DB_PASS! hotfixes < "%%f" 2>nul
)
goto :EOF

:CHECK_RUNNING
REM Check if MariaDB is already running
"!MYSQL!" -u !DB_USER! -p!DB_PASS! -e "SELECT 1" >nul 2>&1
if not errorlevel 1 (
    echo [OK] MariaDB is already running.
    echo Ensuring databases/user, importing any missing game data and applying updates...
    call :ENSURE_DATABASES
    call :IMPORT_ALL_SQL
    if errorlevel 1 goto :FAIL
    goto :STEP7_ONLY
)

echo MariaDB is not running. Starting it...
if not exist "!MYSQLD!" (
    echo [ERROR] mysqld.exe not found. Run this script from scratch.
    goto :FAIL
)

start "" /B "!MYSQLD!" --defaults-file="!MY_INI!" --console 2>"!DB_DIR!\mysqld-error.log"
set /a WAIT_COUNT=0
:WAIT_LOOP2
timeout /t 1 /nobreak >nul
set /a WAIT_COUNT+=1
"!MYSQL!" -u !DB_USER! -p!DB_PASS! -e "SELECT 1" >nul 2>&1
if errorlevel 1 (
    if !WAIT_COUNT! LSS 30 goto :WAIT_LOOP2
    echo [ERROR] MariaDB did not start. Check !DB_DIR!\mysqld-error.log
    goto :FAIL
)
echo [OK] MariaDB is running.
echo Ensuring databases/user, importing any missing game data and applying updates...
call :ENSURE_DATABASES
call :IMPORT_ALL_SQL
if errorlevel 1 goto :FAIL
goto :STEP7_ONLY

:STEP7_ONLY
echo.
echo Updating configuration files...
call :STEP7_CONFIG
goto :STEP7_DONE

:STEP7_CONFIG
REM Copy .dist files to actual config files if they don't exist
if not exist "!ETC_DIR!\worldserver.conf" (
    if exist "!ETC_DIR!\worldserver.conf.dist" (
        copy "!ETC_DIR!\worldserver.conf.dist" "!ETC_DIR!\worldserver.conf" >nul
        echo   Created etc\worldserver.conf from .dist template
    )
)
if not exist "!ETC_DIR!\bnetserver.conf" (
    if exist "!ETC_DIR!\bnetserver.conf.dist" (
        copy "!ETC_DIR!\bnetserver.conf.dist" "!ETC_DIR!\bnetserver.conf" >nul
        echo   Created etc\bnetserver.conf from .dist template
    )
)

REM NOTE: each continuation line below must contain an even number of
REM double-quote characters - with an odd count the trailing ^ becomes a
REM literal caret (cmd disables ^ escaping inside quotes) and the multi-line
REM PowerShell command silently breaks in half. That is why the match
REM expressions use the form 'name\s*=.*' instead of embedding escaped
REM quotes in the pattern.
if exist "!ETC_DIR!\worldserver.conf" (
    powershell -NoProfile -ExecutionPolicy Bypass -Command ^
        "$f = '!ETC_DIR!\worldserver.conf';"^
        "$c = [IO.File]::ReadAllText($f);"^
        "$c = $c -replace 'LoginDatabaseInfo\s*=.*', 'LoginDatabaseInfo     = \"127.0.0.1;!PORT!;!DB_USER!;!DB_PASS!;auth\"';"^
        "$c = $c -replace 'WorldDatabaseInfo\s*=.*', 'WorldDatabaseInfo     = \"127.0.0.1;!PORT!;!DB_USER!;!DB_PASS!;world\"';"^
        "$c = $c -replace 'CharacterDatabaseInfo\s*=.*', 'CharacterDatabaseInfo = \"127.0.0.1;!PORT!;!DB_USER!;!DB_PASS!;characters\"';"^
        "$c = $c -replace 'HotfixDatabaseInfo\s*=.*', 'HotfixDatabaseInfo    = \"127.0.0.1;!PORT!;!DB_USER!;!DB_PASS!;hotfixes\"';"^
        "$c = $c -replace 'Updates\.EnableDatabases\s*=\s*[0-9]+', 'Updates.EnableDatabases = 0';"^
        "$c = $c -replace 'Updates\.AutoSetup\s*=\s*[0-9]+', 'Updates.AutoSetup = 0';"^
        "[IO.File]::WriteAllText($f, $c);"^
        "Write-Host '  Updated etc\worldserver.conf'"
)
if exist "!ETC_DIR!\bnetserver.conf" (
    powershell -NoProfile -ExecutionPolicy Bypass -Command ^
        "$f = '!ETC_DIR!\bnetserver.conf';"^
        "$c = [IO.File]::ReadAllText($f);"^
        "$c = $c -replace 'LoginDatabaseInfo\s*=.*', 'LoginDatabaseInfo = \"127.0.0.1;!PORT!;!DB_USER!;!DB_PASS!;auth\"';"^
        "$c = $c -replace 'Updates\.EnableDatabases\s*=\s*[0-9]+', 'Updates.EnableDatabases = 0';"^
        "$c = $c -replace 'Updates\.AutoSetup\s*=\s*[0-9]+', 'Updates.AutoSetup = 0';"^
        "[IO.File]::WriteAllText($f, $c);"^
        "Write-Host '  Updated etc\bnetserver.conf'"
)
if exist "!ETC_DIR!\worldserver.conf" (
    if exist "%ROOT%bin" (
        copy "!ETC_DIR!\worldserver.conf" "%ROOT%bin\worldserver.conf" >nul
        echo   Copied worldserver.conf to bin\
    )
)
if exist "!ETC_DIR!\bnetserver.conf" (
    if exist "%ROOT%bin" (
        copy "!ETC_DIR!\bnetserver.conf" "%ROOT%bin\bnetserver.conf" >nul
        echo   Copied bnetserver.conf to bin\
    )
)
goto :EOF

:FAIL
echo.
echo ============================================================
echo   Setup FAILED - see error messages above.
echo ============================================================
echo.
echo Common fixes:
echo   - Check your internet connection (download may have failed)
echo   - Make sure port !PORT! is not in use by another MySQL instance
echo   - Run this script as Administrator if you get permission errors
echo   - Delete the "database" folder and try again
echo   - If a game-data download failed, delete the "db-downloads" folder and re-run
echo.
echo Press any key to exit...
pause >nul
