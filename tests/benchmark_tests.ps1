param([Parameter(Mandatory = $true)][string]$Fixtures)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Add-Type -Path (Join-Path $root 'scripts\benchmark-metrics.cs')
$work = Join-Path $Fixtures 'benchmark meter calibration'
New-Item -ItemType Directory -Path $work | Out-Null
$fixture = Join-Path $work 'workload.ps1'
@'
param([string]$Output)
$buffer = New-Object byte[] (32MB)
for ($i = 0; $i -lt $buffer.Length; $i += 4096) { $buffer[$i] = 1 }
$busy = [Diagnostics.Stopwatch]::StartNew()
while ($busy.ElapsedMilliseconds -lt 150) { $null = [Math]::Sqrt(12345.67) }
[IO.File]::WriteAllBytes($Output, $buffer)
Start-Sleep -Milliseconds 300
Write-Output 'calibration-complete'
'@ | Set-Content -LiteralPath $fixture -Encoding utf8
$powershell = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
$arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + $fixture + '" -Output "' + (Join-Path $work 'io.bin') + '"'
$meter = [LuminAMIBenchmark.Meter]::Run($powershell, $arguments, $work)
if ($meter.ExitCode -ne 0 -or $meter.Stdout -notmatch 'calibration-complete') { throw 'Meter lost exit status or stdout.' }
if ($meter.WallMs -lt 450 -or $meter.CpuMs -lt 100 -or $meter.CpuMs -gt $meter.WallMs * 2) { throw 'Meter CPU/wall counters failed calibration.' }
if ($meter.WriteMiB -lt 32 -or $meter.PeakWorkingSetMiB -lt 32) { throw 'Meter did not observe the known memory/I/O workload.' }
if ($meter.Samples -lt 10 -or $meter.MaxSampleGapMs -lt $meter.P95SampleGapMs) { throw 'Meter scheduling-gap samples are invalid.' }
$idle = [LuminAMIBenchmark.Meter]::Idle(200)
if ($idle.WallMs -lt 200 -or $idle.Samples -lt 5) { throw 'Idle control sampling failed.' }
$global:LASTEXITCODE = 0
Write-Output 'PASS: 5 benchmark meter calibration checks; firmware was not accessed.'
