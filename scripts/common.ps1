Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-LuminAmiExecutable {
    param([string]$Executable)
    if ($Executable) {
        if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
            throw "Executable not found: $Executable"
        }
        return (Resolve-Path -LiteralPath $Executable).Path
    }
    $root = Split-Path -Parent $PSScriptRoot
    foreach ($candidate in @((Join-Path $root 'LuminAMI.exe'), (Join-Path $root 'build\LuminAMI.exe'))) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    }
    throw 'LuminAMI.exe was not found. Extract the release ZIP or run build.ps1 first.'
}

function Invoke-LuminAmi {
    param([string]$Executable, [string[]]$Arguments)
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "LuminAMI failed (exit code $LASTEXITCODE). See the error above." }
}

function Get-LuminAmiRunId {
    return (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8)
}

function Assert-LuminAmiDriver {
    param([string]$Driver)
    if (-not $Driver -or -not (Test-Path -LiteralPath $Driver -PathType Leaf)) {
        throw 'Pass -Driver with the path to your supported AMI driver. See docs/DRIVERS.md.'
    }
}
