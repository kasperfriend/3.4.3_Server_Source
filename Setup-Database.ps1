#Requires -Version 5.1
<#
.SYNOPSIS
    TrinityCore 3.4.3 portable MariaDB download, init, import and config.

.DESCRIPTION
    This is the real setup implementation. Setup-Database.bat is only a
    double-clickable launcher.

    Logic lives in PowerShell on purpose. The previous .bat kept dying with
    "- was unexpected at this time" because cmd.exe parses an entire
    parenthesized IF/FOR block before running it, treats unescaped "(" as a
    nested block, then treats the following "-" as a command. Echo lines such
    as "already present (N rows) - skipping ..." triggered that, and every
    "fix" that stayed in .bat reintroduced a variant of the same parser bug
    (quoted programs inside for /f, odd quote counts on caret-continued
    powershell -Command lines, parentheses in prompts, etc.).

    PowerShell does not have that parser. Do not move this logic back into
    a .bat file.

.PARAMETER NonInteractive
    Skip keypress prompts. Use with -Reinstall to wipe an existing database
    folder without asking.

.PARAMETER Reinstall
    Delete the existing portable database folder and set it up from scratch.
#>
[CmdletBinding()]
param(
    [switch]$NonInteractive,
    [switch]$Reinstall
)

$ErrorActionPreference = 'Continue'
Set-StrictMode -Off

$Root        = $PSScriptRoot
$DbDir       = Join-Path $Root 'database'
$DbData      = Join-Path $DbDir 'data'
$DbBin       = Join-Path $DbDir 'bin'
$MysqlExe    = Join-Path $DbBin 'mysql.exe'
$MysqldExe   = Join-Path $DbBin 'mysqld.exe'
$MyIni       = Join-Path $DbDir 'my.ini'
$SqlDir      = Join-Path $Root 'sql'
$EtcDir      = Join-Path $Root 'etc'
$DlDir       = Join-Path $Root 'db-downloads'
$Port        = 3306
$DbUser      = 'trinity'
$DbPass      = 'trinity'
$DbRootPass  = 'rootpassword'

$DbDataRelease      = 'https://github.com/xHashii/WyrmrestCore/releases/download/DB.2608'
$WorldDump          = 'world_full_2026_08_10.sql'
$HotfixesDump       = 'hotfixes_full_2026_08_10.sql'
$WorldDumpSha256    = '91401028dce1dc302e12709a268e4d46cb74f527f95396e2dea1ea5da17081de'
$HotfixesDumpSha256 = '20170a1a52a93af556f4a875885cf2e462777f3a5a3b58e8b128bc171ea866dc'
$MariaDbUrl         = 'https://archive.mariadb.org/mariadb-10.11.10/winx64-packages/mariadb-10.11.10-winx64.zip'

try {
    $Host.UI.RawUI.WindowTitle = 'TrinityCore 3.4.3 - Database Setup'
} catch { }

# ---------------------------------------------------------------------------
# UI helpers
# ---------------------------------------------------------------------------
function Wait-Key {
    param([string]$Message = 'Press any key to continue...')
    if ($NonInteractive) { return }
    Write-Host $Message
    try {
        if ($Host.Name -eq 'ConsoleHost' -and $Host.UI.RawUI) {
            [void]$Host.UI.RawUI.ReadKey('NoEcho,IncludeKeyDown')
            return
        }
    } catch { }
    [void](Read-Host 'Press Enter to continue')
}

function Write-Banner {
    Write-Host '============================================================'
    Write-Host '  TrinityCore 3.4.3 - Portable Database Setup'
    Write-Host '============================================================'
    Write-Host ''
    Write-Host 'This script will:'
    Write-Host '  1. Download MariaDB 10.11 LTS (portable, no install needed)'
    Write-Host '  2. Extract it into the "database" folder'
    Write-Host '  3. Initialize and start the database server'
    Write-Host '  4. Create all required databases'
    Write-Host '  5. Import SQL schemas (auth, characters, playerbot)'
    Write-Host '  6. Download + verify + import the world and hotfixes game data'
    Write-Host '     (WyrmrestCore DB release), then apply database updates'
    Write-Host '  7. Configure worldserver.conf and bnetserver.conf'
    Write-Host ''
}

function Fail-Setup {
    param([string]$Message)
    if ($Message) { Write-Host "[ERROR] $Message" }
    Write-Host ''
    Write-Host '============================================================'
    Write-Host '  Setup FAILED - see error messages above.'
    Write-Host '============================================================'
    Write-Host ''
    Write-Host 'Common fixes:'
    Write-Host '  - Check your internet connection (download may have failed)'
    Write-Host "  - Make sure port $Port is not in use by another MySQL instance"
    Write-Host '  - Run this script as Administrator if you get permission errors'
    Write-Host '  - Delete the "database" folder and try again'
    Write-Host '  - If a game-data download failed, delete the "db-downloads" folder and re-run'
    Write-Host ''
    Wait-Key 'Press any key to exit...'
    exit 1
}

# ---------------------------------------------------------------------------
# Downloads / files
# ---------------------------------------------------------------------------
function Get-FileFromUrl {
    param([string]$Url, [string]$Destination)
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $partial = "$Destination.partial"
    if (Test-Path -LiteralPath $partial) {
        Remove-Item -LiteralPath $partial -Force -ErrorAction SilentlyContinue
    }
    $wc = $null
    try {
        $wc = New-Object System.Net.WebClient
        $wc.DownloadFile($Url, $partial)
        if (Test-Path -LiteralPath $Destination) {
            Remove-Item -LiteralPath $Destination -Force
        }
        Move-Item -LiteralPath $partial -Destination $Destination -Force
    } catch {
        Remove-Item -LiteralPath $partial -Force -ErrorAction SilentlyContinue
        throw
    } finally {
        if ($wc) { $wc.Dispose() }
    }
}

function Test-Sha256 {
    param([string]$Path, [string]$Expected)
    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
    $want = $Expected.ToLowerInvariant()
    if ($actual -ne $want) {
        Remove-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
        throw "SHA-256 mismatch.`n  Expected: $want`n  Actual:   $actual"
    }
}

# ---------------------------------------------------------------------------
# mysql client (never goes through cmd.exe)
# ---------------------------------------------------------------------------
function Invoke-Mysql {
    param(
        [string]$User,
        [string]$Password,
        [string]$Sql,
        [string]$Database = '',
        [switch]$ShowOutput
    )
    if (-not (Test-Path -LiteralPath $script:MysqlExe)) {
        return @{ ExitCode = 1; Output = '' }
    }
    $argList = @(
        "--user=$User",
        "--password=$Password",
        '--batch',
        '--skip-column-names',
        '--default-character-set=utf8mb4'
    )
    if ($Database) { $argList += $Database }
    $argList += @('-e', $Sql)

    $output = & $script:MysqlExe @argList 2>$null
    $code = $LASTEXITCODE
    if ($null -eq $code) { $code = 0 }
    if ($ShowOutput -and $output) { Write-Host ([string]$output) }
    return @{ ExitCode = $code; Output = [string]$output }
}

function Test-MysqlReady {
    param([string]$User, [string]$Password)
    $r = Invoke-Mysql -User $User -Password $Password -Sql 'SELECT 1'
    return ($r.ExitCode -eq 0)
}

function Get-QueryCount {
    param([string]$Sql)
    $r = Invoke-Mysql -User $script:DbUser -Password $script:DbPass -Sql $Sql
    if ($r.ExitCode -ne 0) { return 0 }
    $n = 0
    $text = (($r.Output | Out-String).Trim())
    if ([int]::TryParse($text, [ref]$n)) { return $n }
    return 0
}

function Invoke-MysqlImport {
    param(
        [string]$User,
        [string]$Password,
        [string]$Database,
        [string]$SqlFile
    )
    if (-not (Test-Path -LiteralPath $SqlFile)) {
        return @{ ExitCode = 1; StdErr = "SQL file not found: $SqlFile" }
    }

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $script:MysqlExe
    $arg = "--user=$User --password=$Password --default-character-set=utf8mb4"
    if ($Database) { $arg = "$arg $Database" }
    $psi.Arguments = $arg
    $psi.UseShellExecute = $false
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true
    $psi.WorkingDirectory = $script:Root

    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi
    [void]$proc.Start()

    # Read stdout/stderr asynchronously so a chatty dump cannot deadlock the stdin copy.
    $outTask = $proc.StandardOutput.ReadToEndAsync()
    $errTask = $proc.StandardError.ReadToEndAsync()

    $fs = [System.IO.File]::OpenRead($SqlFile)
    try {
        $fs.CopyTo($proc.StandardInput.BaseStream)
        $proc.StandardInput.BaseStream.Flush()
    } finally {
        $fs.Dispose()
        try { $proc.StandardInput.Close() } catch { }
    }

    $proc.WaitForExit()
    $stdout = ''
    $stderr = ''
    try { $stdout = $outTask.Result } catch { }
    try { $stderr = $errTask.Result } catch { }
    return @{ ExitCode = $proc.ExitCode; StdOut = $stdout; StdErr = $stderr }
}

# ---------------------------------------------------------------------------
# MariaDB process
# ---------------------------------------------------------------------------
function Stop-Mysqld {
    Get-Process -Name 'mysqld' -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2
}

function Start-MysqldProcess {
    if (-not (Test-Path -LiteralPath $script:MysqldExe)) {
        Fail-Setup "mysqld.exe not found. Expected at: $script:MysqldExe"
    }
    if (-not (Test-Path -LiteralPath $script:MyIni)) {
        Fail-Setup "my.ini not found. Expected at: $script:MyIni"
    }

    # Detach from this console so the portable server keeps running after
    # the setup window closes. Stop it with Stop-Database.bat.
    # Errors go to my.ini log-error; do not redirect stdio here or the log
    # file can stay locked if a previous mysqld is still shutting down.
    $argList = @("--defaults-file=$script:MyIni", '--console')
    try {
        Start-Process -FilePath $script:MysqldExe -ArgumentList $argList -WindowStyle Hidden | Out-Null
    } catch {
        Fail-Setup "Could not start mysqld.exe: $($_.Exception.Message)"
    }
}

function Wait-MysqlReady {
    param(
        [string]$User,
        [string]$Password,
        [int]$Seconds = 30
    )
    for ($i = 1; $i -le $Seconds; $i++) {
        Start-Sleep -Seconds 1
        if (Test-MysqlReady -User $User -Password $Password) { return $true }
    }
    return $false
}

function Ensure-MysqldRunning {
    param([switch]$PreferRoot)

    if ($PreferRoot) {
        if (Test-MysqlReady -User 'root' -Password $script:DbRootPass) { return }
        if (Test-MysqlReady -User $script:DbUser -Password $script:DbPass) { return }
    } else {
        if (Test-MysqlReady -User $script:DbUser -Password $script:DbPass) { return }
        if (Test-MysqlReady -User 'root' -Password $script:DbRootPass) { return }
    }

    Write-Host 'MariaDB is not running. Starting it...'
    Start-MysqldProcess

    $user = $script:DbUser
    $pass = $script:DbPass
    if ($PreferRoot) {
        $user = 'root'
        $pass = $script:DbRootPass
    }
    if (-not (Wait-MysqlReady -User $user -Password $pass -Seconds 30)) {
        # One more try with the other account before giving up.
        $altUser = 'root'
        $altPass = $script:DbRootPass
        if ($PreferRoot) {
            $altUser = $script:DbUser
            $altPass = $script:DbPass
        }
        if (-not (Wait-MysqlReady -User $altUser -Password $altPass -Seconds 5)) {
            $log = Join-Path $script:DbDir 'mysqld-error.log'
            Fail-Setup "MariaDB did not start within 30 seconds. Check $log for details."
        }
    }
}

# ---------------------------------------------------------------------------
# Install / init
# ---------------------------------------------------------------------------
function Write-MyIni {
    $basedir = ($script:DbDir -replace '\\', '/')
    $datadir = ($script:DbData -replace '\\', '/')
    $content = @"
[mysqld]
basedir="$basedir"
datadir="$datadir"
port=$($script:Port)
bind-address=127.0.0.1
default-storage-engine=innodb
character-set-server=utf8mb4
collation-server=utf8mb4_general_ci
max_connections=200
innodb_buffer_pool_size=256M
innodb_log_file_size=48M
skip-name-resolve
log-error="$basedir/mysqld-error.log"

[client]
port=$($script:Port)
default-character-set=utf8mb4
"@
    $dir = Split-Path -Parent $script:MyIni
    if (-not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
    [System.IO.File]::WriteAllText($script:MyIni, $content)
}

function Install-MariaDb {
    Write-Host ''
    Write-Host '[1/7] Downloading MariaDB 10.11.10 portable...'
    Write-Host ''

    $zip = Join-Path $script:Root 'mariadb-download.zip'
    Write-Host 'Downloading from archive.mariadb.org ...'
    try {
        Get-FileFromUrl -Url $script:MariaDbUrl -Destination $zip
        Write-Host 'Download complete.'
    } catch {
        Fail-Setup "Download failed. Check your internet connection.`n         URL: $($script:MariaDbUrl)`n         $($_.Exception.Message)"
    }
    if (-not (Test-Path -LiteralPath $zip)) {
        Fail-Setup "Download failed. Check your internet connection.`n         URL: $($script:MariaDbUrl)"
    }

    Write-Host ''
    Write-Host '[2/7] Extracting MariaDB to database\ ...'
    Write-Host ''

    $extractTmp = Join-Path $script:Root 'db-extract-tmp'
    if (Test-Path -LiteralPath $extractTmp) {
        Remove-Item -LiteralPath $extractTmp -Recurse -Force -ErrorAction SilentlyContinue
    }
    if (-not (Test-Path -LiteralPath $script:DbDir)) {
        New-Item -ItemType Directory -Path $script:DbDir -Force | Out-Null
    }

    try {
        Expand-Archive -LiteralPath $zip -DestinationPath $extractTmp -Force
        $inner = Get-ChildItem -LiteralPath $extractTmp -Directory | Select-Object -First 1
        if ($inner) {
            Copy-Item -Path (Join-Path $inner.FullName '*') -Destination $script:DbDir -Recurse -Force
        }
        Write-Host 'Extraction complete.'
    } catch {
        Fail-Setup "Extraction failed: $($_.Exception.Message)"
    } finally {
        Remove-Item -LiteralPath $extractTmp -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $zip -Force -ErrorAction SilentlyContinue
    }

    if (-not (Test-Path -LiteralPath $script:MysqldExe)) {
        Fail-Setup "mysqld.exe not found after extraction.`n         Expected at: $($script:MysqldExe)"
    }
}

function Initialize-MariaDb {
    Write-Host ''
    Write-Host '[3/7] Initializing database...'
    Write-Host ''

    Write-MyIni

    if (Test-Path -LiteralPath $script:DbData) {
        Remove-Item -LiteralPath $script:DbData -Recurse -Force -ErrorAction SilentlyContinue
    }
    New-Item -ItemType Directory -Path $script:DbData -Force | Out-Null

    $installDb = Join-Path $script:DbBin 'mariadb-install-db.exe'
    if (-not (Test-Path -LiteralPath $installDb)) {
        $installDb = Join-Path $script:DbBin 'mysql_install_db.exe'
    }
    if (-not (Test-Path -LiteralPath $installDb)) {
        Fail-Setup @"
Database installer not found. Expected one of:
         $($script:DbBin)\mariadb-install-db.exe
         $($script:DbBin)\mysql_install_db.exe
         The MariaDB download may be incomplete. Delete the "database"
         folder and run this script again.
"@
    }

    Write-Host '  Running mariadb-install-db.exe (this can take a minute)...'
    & $installDb --datadir=$script:DbData --password=$script:DbRootPass --port=$script:Port
    if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        Fail-Setup 'Database initialization failed.'
    }
    $mysqlSys = Join-Path $script:DbData 'mysql'
    if (-not (Test-Path -LiteralPath $mysqlSys)) {
        Fail-Setup @"
Database initialization failed - system tables were not created.
         Check the messages above. You may need to delete the "database"
         folder and run this script again.
"@
    }

    Write-Host '[OK] Database initialized (root password set).'
}

# ---------------------------------------------------------------------------
# Schema / data
# ---------------------------------------------------------------------------
function Ensure-Databases {
    $createSql = Join-Path $script:SqlDir 'create\create_mysql.sql'
    if (Test-Path -LiteralPath $createSql) {
        $imp = Invoke-MysqlImport -User 'root' -Password $script:DbRootPass -Database 'mysql' -SqlFile $createSql
        if ($imp.ExitCode -ne 0) {
            Write-Host '  [WARN] create_mysql.sql had errors (may be OK if databases already exist).'
        }
    } else {
        $sql = @(
            'CREATE DATABASE IF NOT EXISTS `auth` DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci;',
            'CREATE DATABASE IF NOT EXISTS `characters` DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci;',
            'CREATE DATABASE IF NOT EXISTS `world` DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci;',
            'CREATE DATABASE IF NOT EXISTS `hotfixes` DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci;'
        ) -join ' '
        [void](Invoke-Mysql -User 'root' -Password $script:DbRootPass -Sql $sql)
    }

    $stmts = @(
        "CREATE USER IF NOT EXISTS '$script:DbUser'@'localhost' IDENTIFIED BY '$script:DbPass';",
        "CREATE USER IF NOT EXISTS '$script:DbUser'@'127.0.0.1' IDENTIFIED BY '$script:DbPass';",
        "GRANT ALL PRIVILEGES ON ``auth``.* TO '$script:DbUser'@'localhost', '$script:DbUser'@'127.0.0.1';",
        "GRANT ALL PRIVILEGES ON ``characters``.* TO '$script:DbUser'@'localhost', '$script:DbUser'@'127.0.0.1';",
        "GRANT ALL PRIVILEGES ON ``world``.* TO '$script:DbUser'@'localhost', '$script:DbUser'@'127.0.0.1';",
        "GRANT ALL PRIVILEGES ON ``hotfixes``.* TO '$script:DbUser'@'localhost', '$script:DbUser'@'127.0.0.1';",
        'FLUSH PRIVILEGES;'
    )
    foreach ($s in $stmts) {
        [void](Invoke-Mysql -User 'root' -Password $script:DbRootPass -Sql $s)
    }
}

function Import-SqlIfPresent {
    param([string]$Database, [string]$SqlFile, [string]$Label)
    if (-not (Test-Path -LiteralPath $SqlFile)) { return }
    Write-Host "  Importing $Label..."
    $imp = Invoke-MysqlImport -User $script:DbUser -Password $script:DbPass -Database $Database -SqlFile $SqlFile
    if ($imp.ExitCode -ne 0) {
        Write-Host "  [WARN] $(Split-Path -Leaf $SqlFile) had errors."
    }
}

function Get-OrDownloadDump {
    param([string]$FileName, [string]$Sha256, [string]$Kind)
    if (-not (Test-Path -LiteralPath $script:DlDir)) {
        New-Item -ItemType Directory -Path $script:DlDir -Force | Out-Null
    }
    $dest = Join-Path $script:DlDir $FileName
    $url = "$script:DbDataRelease/$FileName"

    if (Test-Path -LiteralPath $dest) {
        Write-Host "  $FileName already present, skipping download."
    } else {
        $hint = if ($Kind -eq 'world') { '~200 MB, may take several minutes' } else { '~100 MB, may take a few minutes' }
        Write-Host "  Downloading $FileName ($hint)..."
        try {
            Get-FileFromUrl -Url $url -Destination $dest
            Write-Host '  Download complete.'
        } catch {
            Fail-Setup "Failed to download $FileName.`n         URL: $url`n         $($_.Exception.Message)"
        }
    }

    Write-Host "  Verifying $FileName checksum (SHA-256)..."
    try {
        Test-Sha256 -Path $dest -Expected $Sha256
        Write-Host '  Checksum OK.'
    } catch {
        Fail-Setup "Checksum verification failed for $FileName.`n         The corrupted file was deleted - re-run this script to re-download it.`n         $($_.Exception.Message)"
    }
    return $dest
}

function Import-GameData {
    if (-not (Test-Path -LiteralPath $script:DlDir)) {
        New-Item -ItemType Directory -Path $script:DlDir -Force | Out-Null
    }

    $worldRows = Get-QueryCount -Sql 'SELECT COUNT(*) FROM world.version'
    if ($worldRows -ne 0) {
        Write-Host "  World data already present ($worldRows rows in world.version) - skipping world import."
    } else {
        $dump = Get-OrDownloadDump -FileName $script:WorldDump -Sha256 $script:WorldDumpSha256 -Kind 'world'

        Write-Host "  Preparing a clean 'world' database..."
        $r = Invoke-Mysql -User 'root' -Password $script:DbRootPass -Sql "DROP DATABASE IF EXISTS world; CREATE DATABASE world DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci;"
        if ($r.ExitCode -ne 0) {
            Fail-Setup "Could not recreate the 'world' database."
        }
        Ensure-Databases

        Write-Host "  Importing $($script:WorldDump) into the 'world' database (this can take several minutes)..."
        $imp = Invoke-MysqlImport -User $script:DbUser -Password $script:DbPass -Database 'world' -SqlFile $dump
        if ($imp.ExitCode -ne 0) {
            Fail-Setup "Failed to import $($script:WorldDump) into the 'world' database."
        }

        Write-Host '  Verifying world database contents...'
        $check = Invoke-Mysql -User $script:DbUser -Password $script:DbPass -Sql 'SELECT COUNT(*) FROM world.version' -ShowOutput
        if ($check.ExitCode -ne 0) {
            Fail-Setup "The 'world' database is missing its 'version' table after import.`n         The downloaded world dump may be incompatible with this server build."
        }
        Write-Host '  [OK] world data imported and verified.'
    }

    $hotfixRows = Get-QueryCount -Sql 'SELECT COUNT(*) FROM hotfixes.achievement'
    if ($hotfixRows -ne 0) {
        Write-Host "  Hotfixes data already present ($hotfixRows rows in achievement) - skipping hotfixes import."
        return
    }

    $dump = Get-OrDownloadDump -FileName $script:HotfixesDump -Sha256 $script:HotfixesDumpSha256 -Kind 'hotfixes'

    Write-Host "  Preparing a clean 'hotfixes' database..."
    $r = Invoke-Mysql -User 'root' -Password $script:DbRootPass -Sql "DROP DATABASE IF EXISTS hotfixes; CREATE DATABASE hotfixes DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci;"
    if ($r.ExitCode -ne 0) {
        Fail-Setup "Could not recreate the 'hotfixes' database."
    }
    Ensure-Databases

    Write-Host "  Importing $($script:HotfixesDump) into the 'hotfixes' database (this can take a few minutes)..."
    $imp = Invoke-MysqlImport -User $script:DbUser -Password $script:DbPass -Database 'hotfixes' -SqlFile $dump
    if ($imp.ExitCode -ne 0) {
        Fail-Setup "Failed to import $($script:HotfixesDump) into the 'hotfixes' database."
    }

    Write-Host '  Verifying hotfixes database contents...'
    $check = Invoke-Mysql -User $script:DbUser -Password $script:DbPass -Sql 'SELECT COUNT(*) FROM hotfixes.achievement' -ShowOutput
    if ($check.ExitCode -ne 0) {
        Fail-Setup "The 'hotfixes' database is missing its 'achievement' table after import.`n         The downloaded hotfixes dump may be incompatible with this server build."
    }
    Write-Host '  [OK] hotfixes data imported and verified.'
}

function Apply-Updates {
    Write-Host '  Applying database updates...'
    $map = @{
        auth        = (Join-Path $script:SqlDir 'updates\auth')
        characters  = (Join-Path $script:SqlDir 'updates\characters')
        world       = (Join-Path $script:SqlDir 'updates\world')
        hotfixes    = (Join-Path $script:SqlDir 'updates\hotfixes')
    }
    foreach ($db in @('auth', 'characters', 'world', 'hotfixes')) {
        $dir = $map[$db]
        if (-not (Test-Path -LiteralPath $dir)) { continue }
        $files = @(Get-ChildItem -LiteralPath $dir -Filter '*.sql' -Recurse -File -ErrorAction SilentlyContinue |
            Sort-Object -Property FullName)
        foreach ($f in $files) {
            $imp = Invoke-MysqlImport -User $script:DbUser -Password $script:DbPass -Database $db -SqlFile $f.FullName
            if ($imp.ExitCode -ne 0) {
                Write-Host "  [WARN] update $($f.Name) on $db had errors."
            }
        }
    }
}

function Import-AllSql {
    Import-SqlIfPresent -Database 'auth' -SqlFile (Join-Path $script:SqlDir 'base\auth_database.sql') -Label 'auth database'
    Import-SqlIfPresent -Database 'characters' -SqlFile (Join-Path $script:SqlDir 'base\characters_database.sql') -Label 'characters database'

    $playerbot = Join-Path $script:SqlDir 'custom\playerbot\characters_playerbot.sql'
    Import-SqlIfPresent -Database 'characters' -SqlFile $playerbot -Label 'playerbot tables'

    Import-GameData
    Apply-Updates
}

# ---------------------------------------------------------------------------
# Server config
# ---------------------------------------------------------------------------
function Set-ConfDatabaseLines {
    param([string]$Path, [string]$Kind)
    if (-not (Test-Path -LiteralPath $Path)) { return }

    $c = [System.IO.File]::ReadAllText($Path)
    $info = "127.0.0.1;$script:Port;$script:DbUser;$script:DbPass"
    if ($Kind -eq 'world') {
        $c = [regex]::Replace($c, 'LoginDatabaseInfo\s*=.*',     "LoginDatabaseInfo     = `"$info;auth`"")
        $c = [regex]::Replace($c, 'WorldDatabaseInfo\s*=.*',     "WorldDatabaseInfo     = `"$info;world`"")
        $c = [regex]::Replace($c, 'CharacterDatabaseInfo\s*=.*', "CharacterDatabaseInfo = `"$info;characters`"")
        $c = [regex]::Replace($c, 'HotfixDatabaseInfo\s*=.*',    "HotfixDatabaseInfo    = `"$info;hotfixes`"")
    } else {
        $c = [regex]::Replace($c, 'LoginDatabaseInfo\s*=.*', "LoginDatabaseInfo = `"$info;auth`"")
    }
    $c = [regex]::Replace($c, 'Updates\.EnableDatabases\s*=\s*[0-9]+', 'Updates.EnableDatabases = 0')
    $c = [regex]::Replace($c, 'Updates\.AutoSetup\s*=\s*[0-9]+', 'Updates.AutoSetup = 0')
    [System.IO.File]::WriteAllText($Path, $c)
}

function Update-ServerConfig {
    if (-not (Test-Path -LiteralPath $script:EtcDir)) {
        New-Item -ItemType Directory -Path $script:EtcDir -Force | Out-Null
    }

    $ws = Join-Path $script:EtcDir 'worldserver.conf'
    $bs = Join-Path $script:EtcDir 'bnetserver.conf'
    $wsDist = Join-Path $script:EtcDir 'worldserver.conf.dist'
    $bsDist = Join-Path $script:EtcDir 'bnetserver.conf.dist'

    if (-not (Test-Path -LiteralPath $ws) -and (Test-Path -LiteralPath $wsDist)) {
        Copy-Item -LiteralPath $wsDist -Destination $ws -Force
        Write-Host '  Created etc\worldserver.conf from .dist template'
    }
    if (-not (Test-Path -LiteralPath $bs) -and (Test-Path -LiteralPath $bsDist)) {
        Copy-Item -LiteralPath $bsDist -Destination $bs -Force
        Write-Host '  Created etc\bnetserver.conf from .dist template'
    }

    if (Test-Path -LiteralPath $ws) {
        Set-ConfDatabaseLines -Path $ws -Kind 'world'
        Write-Host '  Updated etc\worldserver.conf'
    }
    if (Test-Path -LiteralPath $bs) {
        Set-ConfDatabaseLines -Path $bs -Kind 'bnet'
        Write-Host '  Updated etc\bnetserver.conf'
    }

    $binDir = Join-Path $script:Root 'bin'
    if (Test-Path -LiteralPath $binDir) {
        if (Test-Path -LiteralPath $ws) {
            Copy-Item -LiteralPath $ws -Destination (Join-Path $binDir 'worldserver.conf') -Force
            Write-Host '  Copied worldserver.conf to bin\'
        }
        if (Test-Path -LiteralPath $bs) {
            Copy-Item -LiteralPath $bs -Destination (Join-Path $binDir 'bnetserver.conf') -Force
            Write-Host '  Copied bnetserver.conf to bin\'
        }
    }
}

function Invoke-ContentSetup {
    Write-Host ''
    Write-Host '[5/7] Creating databases and user...'
    Write-Host ''
    Ensure-Databases
    Write-Host '[OK] Databases created: auth, characters, world, hotfixes'
    Write-Host "[OK] User '$script:DbUser' created with password '$script:DbPass'"

    Write-Host ''
    Write-Host '[6/7] Importing SQL schemas and game data...'
    Write-Host ''
    Import-AllSql
    Write-Host '[OK] SQL import complete.'

    Write-Host ''
    Write-Host '[7/7] Configuring server files...'
    Write-Host ''
    Update-ServerConfig
    Write-Host '[OK] Configuration files updated.'
}

function Write-Success {
    Write-Host ''
    Write-Host '============================================================'
    Write-Host '  Setup complete!'
    Write-Host '============================================================'
    Write-Host ''
    Write-Host "  Database:    MariaDB 10.11 running on 127.0.0.1:$script:Port"
    Write-Host "  User:        $script:DbUser"
    Write-Host "  Password:    $script:DbPass"
    Write-Host '  Databases:   auth, characters, world, hotfixes'
    Write-Host "  Data dir:    $script:DbDir\data"
    Write-Host ''
    Write-Host '  Server config files have been updated:'
    Write-Host '    etc\worldserver.conf'
    Write-Host '    etc\bnetserver.conf'
    Write-Host '    bin\worldserver.conf'
    Write-Host '    bin\bnetserver.conf'
    Write-Host ''
    Write-Host '  Game data downloaded, checksum-verified and imported:'
    Write-Host "    world     <- $script:WorldDump"
    Write-Host "    hotfixes  <- $script:HotfixesDump"
    Write-Host ''
    Write-Host '  To stop the database:  run Stop-Database.bat'
    Write-Host '  To start it again:     run Start-Database.bat'
    Write-Host ''
    Wait-Key 'Press any key to exit...'
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
Write-Banner
Wait-Key 'Press any key to continue, or Ctrl+C to cancel...'

$freshInstall = -not (Test-Path -LiteralPath $script:MysqldExe)

if (-not $freshInstall) {
    Write-Host ''
    Write-Host "[INFO] MariaDB already exists at: $script:DbDir"
    Write-Host ''

    $doReinstall = $false
    if ($Reinstall) {
        $doReinstall = $true
    } elseif (-not $NonInteractive) {
        $answer = Read-Host 'Reinstall? This will DELETE the existing database folder. (y/N)'
        if ($answer -eq 'y' -or $answer -eq 'Y') { $doReinstall = $true }
    }

    if ($doReinstall) {
        Write-Host 'Stopping any running MariaDB instance...'
        Stop-Mysqld
        Write-Host 'Removing old database folder...'
        Remove-Item -LiteralPath $script:DbDir -Recurse -Force -ErrorAction SilentlyContinue
        $freshInstall = $true
    } else {
        Write-Host ''
        Write-Host 'Skipping download. Checking if server is running...'
        if (Test-MysqlReady -User $script:DbUser -Password $script:DbPass) {
            Write-Host '[OK] MariaDB is already running.'
        } else {
            Ensure-MysqldRunning
            Write-Host "[OK] MariaDB is running on port $script:Port."
        }
        Write-Host 'Ensuring databases/user, importing any missing game data and applying updates...'
        Invoke-ContentSetup
        Write-Success
        exit 0
    }
}

Install-MariaDb
Initialize-MariaDb

Write-Host ''
Write-Host '[4/7] Starting MariaDB server...'
Write-Host ''
Start-MysqldProcess
if (-not (Wait-MysqlReady -User 'root' -Password $script:DbRootPass -Seconds 30)) {
    $log = Join-Path $script:DbDir 'mysqld-error.log'
    Fail-Setup "MariaDB did not start within 30 seconds. Check $log for details."
}
Write-Host "[OK] MariaDB is running on port $script:Port."

Invoke-ContentSetup
Write-Success
exit 0
