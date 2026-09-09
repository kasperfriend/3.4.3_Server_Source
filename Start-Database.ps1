#Requires -Version 5.1
<#
.SYNOPSIS
    Start the portable MariaDB instance created by Setup-Database.ps1.

.DESCRIPTION
    Lives in PowerShell so paths with parentheses, spaces, or dashes cannot
    trip cmd.exe's "X was unexpected at this time" parser. Start-Database.bat
    is only a double-clickable launcher.
#>
[CmdletBinding()]
param(
    [switch]$NonInteractive
)

$ErrorActionPreference = 'Continue'
$Root      = $PSScriptRoot
$DbDir     = Join-Path $Root 'database'
$MysqldExe = Join-Path $DbDir 'bin\mysqld.exe'
$MysqlExe  = Join-Path $DbDir 'bin\mysql.exe'
$MyIni     = Join-Path $DbDir 'my.ini'
$Port      = 3306
$DbUser    = 'trinity'
$DbPass    = 'trinity'
$DbRootPass = 'rootpassword'

try { $Host.UI.RawUI.WindowTitle = 'TrinityCore 3.4.3 - Start Database' } catch { }

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

function Test-Ready {
    param([string]$User, [string]$Password)
    if (-not (Test-Path -LiteralPath $MysqlExe)) { return $false }
    & $MysqlExe --user=$User --password=$Password --batch --skip-column-names -e 'SELECT 1' 2>$null | Out-Null
    return ($LASTEXITCODE -eq 0)
}

Write-Host '============================================================'
Write-Host '  TrinityCore 3.4.3 - Start Database'
Write-Host '============================================================'
Write-Host ''

if (-not (Test-Path -LiteralPath $MysqldExe)) {
    Write-Host '[ERROR] MariaDB not found. Run Setup-Database.bat first.'
    Write-Host ''
    Wait-Key
    exit 1
}

if ((Test-Ready -User $DbUser -Password $DbPass) -or (Test-Ready -User 'root' -Password $DbRootPass)) {
    Write-Host "[OK] MariaDB is already running on port $Port."
    Write-Host ''
    Wait-Key
    exit 0
}

if (-not (Test-Path -LiteralPath $MyIni)) {
    Write-Host "[ERROR] my.ini not found at $MyIni"
    Write-Host '        Run Setup-Database.bat first.'
    Write-Host ''
    Wait-Key
    exit 1
}

Write-Host 'Starting MariaDB...'
$errLog = Join-Path $DbDir 'mysqld-error.log'

try {
    Start-Process -FilePath $MysqldExe -ArgumentList @("--defaults-file=$MyIni", '--console') -WindowStyle Hidden | Out-Null
} catch {
    Write-Host "[ERROR] Could not start mysqld.exe: $($_.Exception.Message)"
    Wait-Key
    exit 1
}

$ready = $false
for ($i = 1; $i -le 30; $i++) {
    Start-Sleep -Seconds 1
    if ((Test-Ready -User $DbUser -Password $DbPass) -or (Test-Ready -User 'root' -Password $DbRootPass)) {
        $ready = $true
        break
    }
}

if (-not $ready) {
    Write-Host '[ERROR] MariaDB did not start within 30 seconds.'
    Write-Host "        Check $errLog"
    Wait-Key
    exit 1
}

Write-Host "[OK] MariaDB is running on port $Port."
Write-Host ''
Write-Host 'To stop it: run Stop-Database.bat'
Write-Host ''
Wait-Key
exit 0
