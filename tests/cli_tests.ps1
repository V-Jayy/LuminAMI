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

$previousConfig = $env:LUMINAMI_CONFIG_DIR
$env:LUMINAMI_CONFIG_DIR = Join-Path $work 'driver-state'
try {
    Check ((& $Executable --version) -eq 'LuminAMI 0.2.1') 'Version is exposed'
    Check (((& $Executable --help) -join "`n") -match 'discord.gg/lumin') 'Help credits Lumin'
    $capture = Join-Path $Fixtures 'cli-capture'
    Check (((& $Executable -h) -join "`n") -match 'Jayy and Billz') 'Short help credits both creators'
    Check (((& $Executable help) -join "`n") -match 'LuminAMI -e') 'Help exposes short commands'
    Check (((& $Executable ami help) -join "`n") -match 'capture-ami') 'AMI namespace exposes advanced help'
    $shortFile = Join-Path $work 'short settings.txt'
    $shortExport = Invoke-Json @('-e', $shortFile, '--capture', $capture, '--non-interactive')
    Check ($shortExport.ok -and -not $shortExport.writes_firmware) 'Short export uses an explicit offline capture with spaced paths'
    Check (Test-Path -LiteralPath ($shortFile + '.dupes.txt')) 'Short export publishes the duplicate file'
    Copy-Item -LiteralPath $capture -Destination ($shortFile + '.capture') -Recurse
    $shortPlan = Invoke-Json @('-i', $shortFile, '--plan')
    Check ($shortPlan.patches.Count -eq 0) 'Short plan resolves the matching sidecar capture without writing'
    $secret = 'test-password-that-must-not-be-logged'
    $passwordError = Join-Path $work 'password-error.json'
    & $Executable -e (Join-Path $work 'protected.txt') -p $secret 2> $passwordError | Out-Null
    Check ($LASTEXITCODE -eq 1) 'Unsupported password authentication fails'
    $passwordText = Get-Content -LiteralPath $passwordError -Raw
    Check ($passwordText -notmatch [regex]::Escape($secret)) 'Password is never included in the error'
    Check (-not (Test-Path -LiteralPath $env:LUMINAMI_CONFIG_DIR)) 'Password rejection has no setup side effects'
    $invalidShort = Join-Path $work 'invalid-short.json'
    & $Executable -i (Join-Path $work 'missing.txt') --non-interactive --install-drivers 2> $invalidShort | Out-Null
    Check ($LASTEXITCODE -eq 1 -and -not (Test-Path -LiteralPath $env:LUMINAMI_CONFIG_DIR)) 'Invalid short import fails before driver installation'
    & $Executable -i $shortFile --non-interactive 2> $invalidShort | Out-Null
    Check ($LASTEXITCODE -eq 1) 'Short live import requires a configured driver'
    Check (@(Get-ChildItem -LiteralPath $work -Filter '*.import-*.json' | Where-Object Name -NotLike '*.result.json').Count -eq 0) 'No journal happens before driver selection'
    $failedShort = Get-ChildItem -LiteralPath $work -Filter '*.import-*.json.result.json' | Select-Object -First 1
    Check (-not (Get-Content -LiteralPath $failedShort.FullName -Raw | ConvertFrom-Json).writes_firmware) 'Failed short import records no firmware writes'
    $original = Join-Path $work 'OriginalSettings.txt'
    $settings = Join-Path $work 'BIOSSettings.txt'
    $export = Invoke-Json @('/o', '/s', $original, '/sd', (Join-Path $work 'Dupes.txt'), '--capture', $capture)
    Check ($export.ok) 'Lowercase export aliases work with spaced paths'
    $edit = Invoke-Json @('edit', '--script', $original, '--output', $settings, '--token', '1', '--value', '0')
    $shortPlan = Invoke-Json @('-i', $settings, '--capture', $capture, '--plan')
    Check ($shortPlan.patches.Count -eq 1) 'Short plan handles an edited file with an explicit capture'
    $advancedPlan = Invoke-Json @('ami', 'import', '--capture', $capture, '--script', $settings)
    Check ($advancedPlan.patches.Count -eq 1) 'Advanced AMI namespace preserves offline import behavior'
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
    try { & (Join-Path $root 'scripts\import.ps1') -Workspace $work -Apply -NonInteractive -Executable $Executable | Out-Null }
    catch { $rejected = $_.Exception.Message -match 'No verified AMI driver' }
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
    $missing = Join-Path $work 'missing-driver.json'
    & $Executable export --capture (Join-Path $work 'new-capture') --script (Join-Path $work 'new-settings.txt') --non-interactive 2> $missing | Out-Null
    Check ($LASTEXITCODE -eq 1) 'Headless export fails promptly when no driver is selected'
    Check (-not (Test-Path -LiteralPath (Join-Path $env:LUMINAMI_CONFIG_DIR 'driver.json'))) 'Missing-driver export does not install silently'
    $cache = Join-Path $work 'installed drivers'
    $installed = Invoke-Json @('install-drivers', '--directory', $cache, '--non-interactive')
    Check ($installed.drivers.Count -eq 2 -and -not $installed.loads_driver -and -not $installed.writes_firmware) 'Headless installer extracts both drivers without loading them'
    $status = Invoke-Json @('driver-status')
    Check ($status.configured -and $status.kind -eq 'amigendrv64.sys') 'Installer saves the provided default'
    Push-Location $Fixtures
    try { $status = Invoke-Json @('driver-status') } finally { Pop-Location }
    Check ($status.configured -and $status.driver -eq $installed.driver) 'Saved driver works from another directory'
    $standalone = Join-Path $work 'path-only'
    New-Item -ItemType Directory -Path $standalone | Out-Null
    Copy-Item -LiteralPath $Executable -Destination (Join-Path $standalone 'LuminAMI.exe')
    $previousExecutablePath = $env:PATH
    try {
        $env:PATH = $standalone + ';' + $previousExecutablePath
        Push-Location $Fixtures
        try {
            $pathOutput = & LuminAMI.exe install-drivers
            Check ($LASTEXITCODE -eq 0) 'Standalone executable installs offline when invoked through PATH'
            $pathInstall = $pathOutput -join "`n" | ConvertFrom-Json
            Check ($pathInstall.drivers.Count -eq 2 -and -not $pathInstall.loads_driver) 'PATH setup needs no scripts or external driver payloads'
        } finally { Pop-Location }
    } finally { $env:PATH = $previousExecutablePath }
    $custom = Join-Path $work 'my custom driver.sys'
    Copy-Item -LiteralPath (Join-Path $cache 'amifldrv64.sys') -Destination $custom
    $selection = Invoke-Json @('use-driver', '--driver', $custom)
    Check ($selection.kind -eq 'amifldrv64.sys') 'Custom driver is selected by content'
    $status = Invoke-Json @('driver-status')
    Check ($status.driver -eq $custom) 'Custom driver path persists across invocations'
    $offline = Invoke-Json @('import', '--capture', $capture, '--script', $settings)
    Check ($offline.patches.Count -eq 1) 'A saved driver does not turn offline import into a live write'
    . (Join-Path $root 'scripts\common.ps1')
    Check ((Get-LuminAmiDriver -Executable $Executable -NonInteractive) -eq $custom) 'Scripts reuse the same saved custom driver as the native CLI'
    $chosen = Get-LuminAmiDriver -Executable $Executable -InstallDrivers -NonInteractive
    Check ((Split-Path -Leaf $chosen) -eq 'amigendrv64.sys') 'Provided-driver selection works headlessly through the scripts'
    Set-Content -LiteralPath $chosen -Value 'tampered driver' -Encoding ascii
    $status = Invoke-Json @('driver-status')
    Check (-not $status.configured) 'Changed saved bytes are rejected on reuse'
    # The expected rejection above must not become the build script's final exit status.
    $global:LASTEXITCODE = 0
    Write-Output "PASS: $checks CLI/script checks; no driver was loaded."
} finally { $env:LUMINAMI_CONFIG_DIR = $previousConfig }
