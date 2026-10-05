[CmdletBinding()]
param(
    [string]$Driver,
    [switch]$InstallDrivers,
    [switch]$NonInteractive,
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
Invoke-LuminAmi $exe @(
    'export', '--driver', $selectedDriver, '--non-interactive',
    '--capture', (Join-Path $workspacePath 'capture'), '--script', $settings,
    '--dupes', (Join-Path $workspacePath 'Dupes.txt'),
    '--report', (Join-Path $workspacePath 'export.json')
)
Copy-Item -LiteralPath $settings -Destination (Join-Path $workspacePath 'OriginalSettings.txt')
Write-Host "Export saved to $workspacePath"
Write-Host 'Edit BIOSSettings.txt, then run Import.cmd to review your changes.'
