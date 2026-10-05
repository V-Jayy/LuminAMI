[CmdletBinding()]
param(
    [string]$Driver,
    [string]$Directory,
    [switch]$NonInteractive,
    [string]$Executable
)
. (Join-Path $PSScriptRoot 'common.ps1')
$exe = Get-LuminAmiExecutable $Executable
$selected = Get-LuminAmiDriver -Executable $exe -Driver $Driver -Directory $Directory -NonInteractive:$NonInteractive -Choose
Write-Host "Driver ready: $selected"
Write-Host 'Live commands will reuse this choice. No driver was loaded and no BIOS settings were changed.'
Invoke-LuminAmi $exe @('driver-status')
