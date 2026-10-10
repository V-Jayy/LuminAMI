[CmdletBinding()]
param(
    [string]$Driver,
    [switch]$InstallDrivers,
    [switch]$NonInteractive,
    # Only for a board that asks for the BIOS administrator password you set in BIOS setup. LuminAMI itself shows a hidden
    # prompt (nothing is echoed or passed through this script); see docs/PASSWORD.md.
    [switch]$AskPassword,
    [string]$Workspace = (Join-Path (Get-Location) 'LuminAMI-backup'),
    [string]$Executable
)
. (Join-Path $PSScriptRoot 'common.ps1')
$exe = Get-LuminAmiExecutable $Executable
if (Test-Path -LiteralPath $Workspace) { throw "Backup folder already exists. Choose a new -Workspace: $Workspace" }
$selectedDriver = Get-LuminAmiDriver -Executable $exe -Driver $Driver -InstallDrivers:$InstallDrivers -NonInteractive:$NonInteractive
New-Item -ItemType Directory -Path $Workspace | Out-Null
$workspacePath = (Resolve-Path -LiteralPath $Workspace).Path
$settings = Join-Path $workspacePath 'BIOSSettings.txt'
$arguments = @(
    'export', '--driver', $selectedDriver, '--non-interactive',
    '--capture', (Join-Path $workspacePath 'capture'), '--script', $settings,
    '--dupes', (Join-Path $workspacePath 'Dupes.txt'),
    '--report', (Join-Path $workspacePath 'export.json')
)
if ($AskPassword) { $arguments += '--ask-password' }
Invoke-LuminAmi $exe $arguments
Copy-Item -LiteralPath $settings -Destination (Join-Path $workspacePath 'OriginalSettings.txt')
Write-Host "Export saved to $workspacePath"
Write-Host 'Edit BIOSSettings.txt, then run Import.cmd to review your changes.'
