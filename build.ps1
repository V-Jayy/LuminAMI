param([switch]$Test)
$ErrorActionPreference = 'Stop'
$luminAmiRoot = $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio 2022 C++ tools and the Windows SDK are required.' }
$visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) { throw 'Visual Studio C++ tools are required.' }
$compilerVersion = Get-ChildItem -LiteralPath (Join-Path $visualStudio 'VC\Tools\MSVC') -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
$sdkVersion = Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'Include') -Directory | Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'um\Windows.h') } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $compilerVersion -or -not $sdkVersion) { throw 'C++ compiler or Windows SDK was not found.' }
$compiler = Join-Path $compilerVersion.FullName 'bin\Hostx64\x64\cl.exe'
$oldInclude, $oldLib, $oldPath = $env:INCLUDE, $env:LIB, $env:PATH
try {
    $env:INCLUDE = @((Join-Path $compilerVersion.FullName 'include'), (Join-Path $sdkVersion.FullName 'ucrt'), (Join-Path $sdkVersion.FullName 'shared'), (Join-Path $sdkVersion.FullName 'um'), (Join-Path $sdkVersion.FullName 'winrt')) -join ';'
    $env:LIB = @((Join-Path $compilerVersion.FullName 'lib\x64'), (Join-Path $sdkRoot "Lib\$($sdkVersion.Name)\ucrt\x64"), (Join-Path $sdkRoot "Lib\$($sdkVersion.Name)\um\x64")) -join ';'
    $env:PATH = (Split-Path -Parent $compiler) + ';' + $oldPath
    $buildDirectory = Join-Path $luminAmiRoot 'build'
    New-Item -ItemType Directory -Path $buildDirectory -Force | Out-Null
    $sources = Get-ChildItem -LiteralPath (Join-Path $luminAmiRoot 'src') -Filter '*.cpp' | ForEach-Object FullName
    $common = @('/nologo', '/std:c++20', '/EHsc', '/O2', '/MT', '/W4', '/utf-8', '/DUNICODE', '/D_UNICODE', "/I$(Join-Path $luminAmiRoot 'include')", "/external:I$(Join-Path $luminAmiRoot 'third_party')", '/external:W0')
    Push-Location $buildDirectory
    try {
        & $compiler @common @sources '/Fe:LuminAMI.exe' '/link' 'advapi32.lib' 'bcrypt.lib' 'ole32.lib'
        if ($LASTEXITCODE -ne 0) { throw 'LuminAMI build failed.' }
        if ($Test) {
            $librarySources = $sources | Where-Object { (Split-Path -Leaf $_) -ne 'main.cpp' }
            & $compiler @common @librarySources (Join-Path $luminAmiRoot 'tests\core_tests.cpp') '/Fe:LuminAMI-tests.exe' '/link' 'advapi32.lib' 'bcrypt.lib' 'ole32.lib'
            if ($LASTEXITCODE -ne 0) { throw 'Test build failed.' }
            $testOutput = & (Join-Path $buildDirectory 'LuminAMI-tests.exe')
            if ($LASTEXITCODE -ne 0) { throw 'LuminAMI tests failed.' }
            $testOutput | Write-Output
            $fixtureLine = $testOutput | Where-Object { $_.StartsWith('Fixture artifacts: ') } | Select-Object -First 1
            if (-not $fixtureLine) { throw 'Offline tests did not report their fixture directory.' }
            & (Join-Path $luminAmiRoot 'tests\cli_tests.ps1') -Executable (Join-Path $buildDirectory 'LuminAMI.exe') -Fixtures $fixtureLine.Substring(19)
        }
    } finally { Pop-Location }
} finally { $env:INCLUDE, $env:LIB, $env:PATH = $oldInclude, $oldLib, $oldPath }
