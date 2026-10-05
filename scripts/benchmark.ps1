[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Scewin,
    [string]$Executable,
    [ValidateRange(3, 50)][int]$Runs = 10,
    [string]$Output = (Join-Path (Get-Location) ('benchmark-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))),
    [switch]$IncludeImport
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
if (-not ([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run the benchmark from an administrator PowerShell terminal.'
}
$exe = Get-LuminAmiExecutable $Executable
$Scewin = (Resolve-Path -LiteralPath $Scewin).Path
$Output = [IO.Path]::GetFullPath($Output)
if (Test-Path -LiteralPath $Output) { throw 'Choose a new benchmark output folder.' }
if (Get-Service -Name GENERICDRV -ErrorAction SilentlyContinue) { throw 'Close other AMI utilities first: GENERICDRV already exists.' }
New-Item -ItemType Directory -Path $Output | Out-Null
Start-Transcript -LiteralPath (Join-Path $Output 'session.log') | Out-Null
$previousConfig = $env:LUMINAMI_CONFIG_DIR
$env:LUMINAMI_CONFIG_DIR = Join-Path $Output 'driver-state'
$results = [Collections.Generic.List[object]]::new()
$importsAttempted = $false
$baselineReady = $false
$failure = $null
$finalVerified = $false
function Save-Json($Path, $Value) {
    [IO.File]::WriteAllText($Path, (($Value | ConvertTo-Json -Depth 30) + "`n"), [Text.UTF8Encoding]::new($false))
}
function Quote-Argument([string]$Text) {
    # Windows CommandLineToArgvW escaping, including trailing slashes before a quote.
    return '"' + ([regex]::Replace([regex]::Replace($Text, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1')) + '"'
}
function Measure-Command([string]$Tool, [string]$Operation, [string[]]$Arguments, [string]$Directory, [string]$Label, [bool]$Measured, [int]$Round) {
    $program = if ($Tool -eq 'LuminAMI') { $exe } else { $sceExe }
    $commandLine = ($Arguments | ForEach-Object { Quote-Argument $_ }) -join ' '
    Save-Json (Join-Path $Output 'progress.json') @{phase=$Label; tool=$Tool; operation=$Operation; round=$Round; started=(Get-Date).ToString('o')}
    Write-Output "$Label : $Tool $Operation"
    $m = [LuminAMIBenchmark.Meter]::Run($program, $commandLine, $Directory)
    [IO.File]::WriteAllText((Join-Path $Output "$Label.stdout.txt"), $m.Stdout)
    [IO.File]::WriteAllText((Join-Path $Output "$Label.stderr.txt"), $m.Stderr)
    $row = [ordered]@{tool=$Tool; operation=$Operation; round=$Round; measured=$Measured; exit_code=$m.ExitCode;
        wall_ms=$m.WallMs; cpu_ms=$m.CpuMs; kernel_cpu_ms=$m.KernelCpuMs; user_cpu_ms=$m.UserCpuMs;
        peak_working_set_mib=$m.PeakWorkingSetMiB; peak_commit_mib=$m.PeakCommitMiB;
        read_mib=$m.ReadMiB; write_mib=$m.WriteMiB; other_io_mib=$m.OtherIoMiB;
        system_busy_pct=$m.SystemBusyPct; max_sampler_gap_ms=$m.MaxSampleGapMs;
        p95_sampler_gap_ms=$m.P95SampleGapMs; samples=$m.Samples}
    $results.Add([pscustomobject]$row)
    Save-Json (Join-Path $Output 'runs.json') @($results.ToArray())
    if ($m.ExitCode -ne 0) { throw "$Tool $Operation failed with exit $($m.ExitCode). See $Label stderr/stdout." }
    if (Get-Service -Name GENERICDRV -ErrorAction SilentlyContinue) { throw "$Tool left GENERICDRV present; refusing to overlap driver ownership." }
    Write-Output ('  {0:N1} ms wall, {1:N1} ms CPU, {2:N1} MiB peak RAM' -f $m.WallMs,$m.CpuMs,$m.PeakWorkingSetMiB)
}
try {
    Add-Type -Path (Join-Path $PSScriptRoot 'benchmark-metrics.cs')
    $sceWork = Join-Path $Output 'reference-tool'
    New-Item -ItemType Directory -Path $sceWork | Out-Null
    $sceExe = Join-Path $sceWork 'SCEWIN_64.exe'
    Copy-Item -LiteralPath $Scewin -Destination $sceExe
    $setup = Invoke-LuminAmiJson $exe @('install-drivers', '--directory', $sceWork)
    $driver = $setup.driver
    $baseline = Join-Path $Output 'baseline'
    New-Item -ItemType Directory -Path $baseline | Out-Null
    $luminSettings = Join-Path $baseline 'LuminAMI.txt'
    $sceSettings = Join-Path $baseline 'SCEWIN.txt'
    $capture = Join-Path $baseline 'capture'
    Measure-Command 'LuminAMI' 'export' @('export','--driver',$driver,'--capture',$capture,'--script',$luminSettings,'--dupes',(Join-Path $baseline 'LuminAMI-dupes.txt'),'--non-interactive') $Output 'baseline-lumin' $false 0
    Measure-Command 'SCEWIN' 'export' @('/O','/S',$sceSettings,'/SD',(Join-Path $baseline 'SCEWIN-dupes.txt')) $sceWork 'baseline-scewin' $false 0
    $baselineReady = $true
    $baselineManifest = Get-Content -LiteralPath (Join-Path $capture 'capture.json') -Raw | ConvertFrom-Json
    $plan = Invoke-LuminAmiJson $exe @('import','--capture',$capture,'--script',$luminSettings)
    if ($plan.patches.Count -ne 0) { throw 'The current LuminAMI export did not produce an unchanged import plan.' }
    $board = Get-CimInstance Win32_BaseBoard
    $bios = Get-CimInstance Win32_BIOS
    $cpu = Get-CimInstance Win32_Processor
    $os = Get-CimInstance Win32_OperatingSystem
    $machine = Get-CimInstance Win32_ComputerSystem
    $metadata = [ordered]@{format='luminami-benchmark-v1'; started_local=(Get-Date).ToString('o'); runs_per_tool=$Runs;
        warmups_per_tool=1; import_workload='Unmodified full current-settings file; LuminAMI skips unchanged variable writes';
        board=$board.Product; board_vendor=$board.Manufacturer; bios=$bios.SMBIOSBIOSVersion;
        cpu=@($cpu | ForEach-Object Name); logical_processors=$machine.NumberOfLogicalProcessors;
        ram_gib=$machine.TotalPhysicalMemory/1GB; windows=$os.Caption; windows_build=$os.BuildNumber;
        luminami_version=(& $exe --version); luminami_sha256=(Get-FileHash -LiteralPath $exe).Hash.ToLowerInvariant();
        scewin_sha256=(Get-FileHash -LiteralPath $sceExe).Hash.ToLowerInvariant();
        driver_sha256=(Get-FileHash -LiteralPath $driver).Hash.ToLowerInvariant();
        hii_sha256=$baselineManifest.hii_sha256; hii_bytes=(Get-Item -LiteralPath (Join-Path $capture 'hii.bin')).Length;
        baseline_varstores=$baselineManifest.variables.Count; driver_service_lifecycle_included_in_timing=$true;
        driver_file_extraction_included_in_timing=$false;
        sampler_interval_ms=10; measurements='Child process lifetime; fresh outputs; alternating AB/BA order; no priorities/affinities/security changes';
        idle_before=[LuminAMIBenchmark.Meter]::Idle(3000)}
    Save-Json (Join-Path $Output 'metadata.json') $metadata
    $operations = @('export')
    if ($IncludeImport) { $operations += 'import' }
    foreach ($operation in $operations) {
        for ($round = 0; $round -le $Runs; $round++) {
            $tools = if (($round % 2) -eq 0) { @('LuminAMI','SCEWIN') } else { @('SCEWIN','LuminAMI') }
            foreach ($tool in $tools) {
                $label = "$operation-$round-$tool"
                $directory = Join-Path $Output $label
                New-Item -ItemType Directory -Path $directory | Out-Null
                if ($operation -eq 'export') {
                    $settings = Join-Path $directory 'settings.txt'
                    $dupes = Join-Path $directory 'Dupes.txt'
                    if ($tool -eq 'LuminAMI') {
                        $arguments = @('export','--driver',$driver,'--capture',(Join-Path $directory 'capture'),'--script',$settings,'--dupes',$dupes,'--non-interactive')
                    } else { $arguments = @('/O','/S',$settings,'/SD',$dupes) }
                } elseif ($tool -eq 'LuminAMI') {
                    $arguments = @('import','--driver',$driver,'--capture',$capture,'--script',$luminSettings,'--journal',(Join-Path $directory 'journal.json'),'--non-interactive')
                    $importsAttempted = $true
                } else {
                    $arguments = @('/I','/S',$sceSettings)
                    $importsAttempted = $true
                }
                Measure-Command $tool $operation $arguments $(if($tool -eq 'SCEWIN'){$sceWork}else{$Output}) $label ($round -gt 0) $round
                Start-Sleep -Milliseconds 1000
            }
        }
    }
} catch {
    $failure = $_.Exception.Message
    Write-Warning $failure
} finally {
    try {
        if ($baselineReady) {
            if ($importsAttempted) {
                # Last reference import uses the untouched starting profile. Native restore
                # rebases supported fields onto a fresh read, preserving unrelated live bytes.
                try { Measure-Command 'SCEWIN' 'import' @('/I','/S',$sceSettings) $sceWork 'final-reference-import' $false 0 }
                catch { $failure = "$failure Final reference import: $($_.Exception.Message)" }
                try { Measure-Command 'LuminAMI' 'restore' @('restore','--driver',$driver,'--capture',$capture,'--journal',(Join-Path $Output 'final-restore.json'),'--non-interactive') $Output 'final-current-settings-import' $false 0 }
                catch { $failure = "$failure Final native restore: $($_.Exception.Message)" }
            }
            $finalCapture = Join-Path $Output 'final-capture'
            Measure-Command 'LuminAMI' 'export' @('capture-ami','--driver',$driver,'--output',$finalCapture,'--non-interactive') $Output 'final-verification' $false 0
            $finalManifest = Get-Content -LiteralPath (Join-Path $finalCapture 'capture.json') -Raw | ConvertFrom-Json
            $differences = @()
            foreach ($variable in $baselineManifest.variables) {
                $current = @($finalManifest.variables | Where-Object { $_.name -eq $variable.name -and $_.guid -eq $variable.guid })
                if ($current.Count -ne 1 -or $current[0].data -ne $variable.data -or $current[0].attributes -ne $variable.attributes) {
                    $differences += [pscustomobject]@{name=$variable.name; guid=$variable.guid; attributes=$variable.attributes}
                }
            }
            $finalVerified = $differences.Count -eq 0 -and $finalManifest.hii_sha256 -eq $baselineManifest.hii_sha256
            Save-Json (Join-Path $Output 'final-verification.json') @{verified=$finalVerified; compared_varstores=$baselineManifest.variables.Count; differences=$differences; hii_matches=($finalManifest.hii_sha256 -eq $baselineManifest.hii_sha256)}
            if (-not $finalVerified) { throw 'Final varstores differ from the starting snapshot. Preserve all benchmark recovery files.' }
            $metadata.idle_after = [LuminAMIBenchmark.Meter]::Idle(3000)
            Save-Json (Join-Path $Output 'metadata.json') $metadata
        }
    } catch { $failure = "$failure Final verification/recovery: $($_.Exception.Message)" }
    Save-Json (Join-Path $Output 'completion.json') @{ok=($null -eq $failure); error=$failure; final_settings_verified=$finalVerified; imports_attempted=$importsAttempted}
    $env:LUMINAMI_CONFIG_DIR = $previousConfig
    Stop-Transcript | Out-Null
}
if ($failure) { throw $failure }
Write-Output "Benchmark complete. Final starting settings verified. Results: $Output"
