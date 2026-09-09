#Requires -Version 5.1
<#
.SYNOPSIS
    Stop the portable MariaDB instance started by Start-Database / Setup-Database.

.DESCRIPTION
    Lives in PowerShell so cmd.exe cannot misparse parentheses or dashes.
    Stop-Database.bat is only a double-clickable launcher.
#>
[CmdletBinding()]
param(
    [switch]$NonInteractive
)

$ErrorActionPreference = 'Continue'
try { $Host.UI.RawUI.WindowTitle = 'TrinityCore 3.4.3 - Stop Database' } catch { }

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

Write-Host 'Stopping MariaDB...'
$procs = @(Get-Process -Name 'mysqld' -ErrorAction SilentlyContinue)
if ($procs.Count -eq 0) {
    Write-Host '[OK] MariaDB is not running.'
} else {
    $procs | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2
    $left = @(Get-Process -Name 'mysqld' -ErrorAction SilentlyContinue)
    if ($left.Count -eq 0) {
        Write-Host '[OK] MariaDB stopped.'
    } else {
        Write-Host '[ERROR] mysqld.exe is still running. Try Task Manager or run as Administrator.'
        Wait-Key
        exit 1
    }
}
Write-Host ''
Wait-Key
exit 0
