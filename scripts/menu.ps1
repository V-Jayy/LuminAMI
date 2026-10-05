[CmdletBinding()]
param(
    [string]$Workspace = (Join-Path (Get-Location) 'LuminAMI-backup'),
    [string]$Executable
)
. (Join-Path $PSScriptRoot 'common.ps1')
$exe = Get-LuminAmiExecutable $Executable
while ($true) {
    Write-Host "`nLuminAMI | discord.gg/lumin"
    Write-Host "Backup folder: $Workspace"
    Write-Host '1  Export BIOS settings'
    Write-Host '2  Inspect settings'
    Write-Host '3  Edit a setting by token'
    Write-Host '4  Show changes'
    Write-Host '5  Plan import (no writes)'
    Write-Host '6  Apply import'
    Write-Host '7  Restore backup'
    Write-Host '8  Command help'
    Write-Host '9  Install provided drivers / choose custom driver'
    Write-Host '0  Exit'
    $choice = Read-Host 'Command'
    if ($choice -eq '0') { break }
    try {
        $settings = Join-Path $Workspace 'BIOSSettings.txt'
        $original = Join-Path $Workspace 'OriginalSettings.txt'
        switch ($choice) {
            '1' {
                & (Join-Path $PSScriptRoot 'export.ps1') -Workspace $Workspace -Executable $exe
            }
            '2' { Invoke-LuminAmi $exe @('inspect', '--script', $settings) }
            '3' {
                $token = Read-Host 'Token from your export (e.g. 0x2B)'
                $value = Read-Host 'New value'
                $edited = Join-Path $Workspace ('edited-' + (Get-LuminAmiRunId) + '.txt')
                Invoke-LuminAmi $exe @('edit', '--script', $settings, '--output', $edited, '--token', $token, '--value', $value)
                Write-Host "Saved $edited. Inspect it, then copy it over BIOSSettings.txt if you want to import it."
            }
            '4' { Invoke-LuminAmi $exe @('diff', '--before', $original, '--after', $settings) }
            '5' { & (Join-Path $PSScriptRoot 'import.ps1') -Workspace $Workspace -Executable $exe }
            '6' {
                & (Join-Path $PSScriptRoot 'import.ps1') -Workspace $Workspace -Executable $exe
                if ((Read-Host 'This writes BIOS settings. Type APPLY to continue') -eq 'APPLY') {
                    & (Join-Path $PSScriptRoot 'import.ps1') -Workspace $Workspace -Apply -Executable $exe
                }
            }
            '7' {
                if ((Read-Host 'This restores BIOS settings from the backup. Type RESTORE to continue') -eq 'RESTORE') {
                    & (Join-Path $PSScriptRoot 'restore.ps1') -Workspace $Workspace -Executable $exe
                }
            }
            '8' { Invoke-LuminAmi $exe @('--help') }
            '9' { & (Join-Path $PSScriptRoot 'install-drivers.ps1') -Executable $exe }
            default { Write-Host 'Choose a number from the list.' }
        }
    } catch { Write-Host $_.Exception.Message -ForegroundColor Red }
}
