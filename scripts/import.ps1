[CmdletBinding()]
param(
    [string]$Workspace = (Join-Path (Get-Location) 'LuminAMI-backup'),
    [string]$Script,
    [string]$Driver,
    [switch]$InstallDrivers,
    [switch]$NonInteractive,
    # Only for a board that asks for the BIOS administrator password you set in BIOS setup. LuminAMI itself shows a hidden
    # prompt (nothing is echoed or passed through this script); see docs/PASSWORD.md.
    [switch]$AskPassword,
    [switch]$Apply,
    [string]$Executable
)
. (Join-Path $PSScriptRoot 'common.ps1')
$exe = Get-LuminAmiExecutable $Executable
if (-not $Script) { $Script = Join-Path $Workspace 'BIOSSettings.txt' }
$capture = Join-Path $Workspace 'capture'
$original = Join-Path $Workspace 'OriginalSettings.txt'
foreach ($required in @($Script, $original, (Join-Path $capture 'capture.json'), (Join-Path $capture 'hii.bin'))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Backup or settings file is missing: $required" }
}
if ($AskPassword -and -not $Apply) { throw '-AskPassword applies to -Apply. Offline planning reads no firmware.' }
if ($InstallDrivers -and -not $Apply) { throw '-InstallDrivers requires -Apply on import. Offline planning needs no driver.' }
Invoke-LuminAmi $exe @('diff', '--before', $original, '--after', $Script)
$runId = Get-LuminAmiRunId
$plan = Join-Path $Workspace "plan-$runId.json"
Invoke-LuminAmi $exe @('import', '--capture', $capture, '--script', $Script, '--output', $plan)
if (-not $Apply) {
    Write-Host "Plan saved to $plan. No BIOS settings were written."
    Write-Host 'To apply it, rerun Import.cmd -Apply.'
    return
}
$journal = Join-Path $Workspace "import-$runId.json"
$selectedDriver = Get-LuminAmiDriver -Executable $exe -Driver $Driver -InstallDrivers:$InstallDrivers -NonInteractive:$NonInteractive
$arguments = @(
    'import', '--driver', $selectedDriver, '--non-interactive',
    '--capture', $capture, '--script', $Script, '--journal', $journal,
    '--report', (Join-Path $Workspace "import-$runId-result.json")
)
if ($AskPassword) { $arguments += '--ask-password' }
Invoke-LuminAmi $exe $arguments
Write-Host "Import finished. Journal: $journal"
