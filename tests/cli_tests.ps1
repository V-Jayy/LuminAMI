param(
    [Parameter(Mandatory = $true)][string]$Executable,
    [Parameter(Mandatory = $true)][string]$Fixtures
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$work = Join-Path $Fixtures 'cli scripts with spaces'
New-Item -ItemType Directory -Path $work | Out-Null
$checks = 0

function Check {
    param([bool]$Condition, [string]$Message)
    $script:checks++
    if (-not $Condition) { throw "CLI test failed: $Message" }
}

function Invoke-Json {
    param([string[]]$Arguments)
    $output = & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "CLI command failed: $($Arguments[0])" }
    return ($output -join "`n" | ConvertFrom-Json)
}

Check ((& $Executable --version) -eq 'LuminAMI 0.1.0') 'Version is exposed'
Check (((& $Executable --help) -join "`n") -match 'discord.gg/lumin') 'Help credits Lumin'
$capture = Join-Path $Fixtures 'cli-capture'
$original = Join-Path $work 'OriginalSettings.txt'
$settings = Join-Path $work 'BIOSSettings.txt'
$export = Invoke-Json @('/o', '/s', $original, '/sd', (Join-Path $work 'Dupes.txt'), '--capture', $capture)
Check ($export.ok) 'Lowercase export aliases work with spaced paths'
$edit = Invoke-Json @('edit', '--script', $original, '--output', $settings, '--token', '1', '--value', '0')
Check ($edit.ok -and -not $edit.writes_firmware) 'Edit creates an offline settings file'
$diff = Invoke-Json @('diff', '--before', $original, '--after', $settings)
Check ($diff.changes.Count -eq 1) 'Diff finds the edited option'
$inspect = Invoke-Json @('inspect', '--script', $settings)
Check ($inspect.summary.issues -eq 0) 'Edited script parses cleanly'
$plan = Invoke-Json @('/i', '/s', $settings, '--capture', $capture)
Check ($plan.patches.Count -eq 1) 'Import aliases produce the expected offline plan'
Copy-Item -LiteralPath $capture -Destination (Join-Path $work 'capture') -Recurse
$originalHash = (Get-FileHash -LiteralPath $original).Hash
& (Join-Path $root 'scripts\import.ps1') -Workspace $work -Executable $Executable | Out-Null
$plans = @(Get-ChildItem -LiteralPath $work -Filter 'plan-*.json')
Check ($plans.Count -eq 1) 'Import shortcut saves one plan'
$savedPlan = Get-Content -LiteralPath $plans[0].FullName -Raw | ConvertFrom-Json
Check ($savedPlan.patches.Count -eq 1) 'Import shortcut validates the intended patch'
Check ((Get-FileHash -LiteralPath $original).Hash -eq $originalHash) 'Original export is preserved'
Check (@(Get-ChildItem -LiteralPath $work -Filter 'import-*.json').Count -eq 0) 'Plan shortcut creates no live journal'
$rejected = $false
try { & (Join-Path $root 'scripts\import.ps1') -Workspace $work -Apply -Executable $Executable | Out-Null }
catch { $rejected = $_.Exception.Message -match 'Pass -Driver' }
Check $rejected 'Applying without a driver fails before writing'
$rejected = $false
try { & (Join-Path $root 'scripts\export.ps1') -Workspace $work -Driver $Executable -Executable $Executable | Out-Null }
catch { $rejected = $_.Exception.Message -match 'already exists' }
Check $rejected 'Export refuses an existing workspace before driver use'
$errorsFile = Join-Path $work 'invalid-options.json'
& $Executable import --capture $capture --script $settings --typo value 2> $errorsFile | Out-Null
Check ($LASTEXITCODE -eq 1) 'Unknown options fail'
$errorReport = Get-Content -LiteralPath $errorsFile -Raw | ConvertFrom-Json
Check (-not $errorReport.writes_firmware) 'Invalid command reports no writes'
# The expected rejection above must not become the build script's final exit status.
$global:LASTEXITCODE = 0
Write-Output "PASS: $checks CLI/script checks; no driver was loaded."
