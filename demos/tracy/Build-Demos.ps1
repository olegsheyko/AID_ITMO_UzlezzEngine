param(
    [ValidateSet('Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [switch]$Detail,
    [switch]$ManualSampling
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$buildDir = Join-Path $projectRoot 'build'

$cmakeCommand = Get-Command cmake.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
$cmakePath = if ($cmakeCommand) { $cmakeCommand.Source } else { $null }
if (!$cmakePath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $cmakePath = & $vswhere -products '*' -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' |
            Select-Object -First 1
    }
}
if (!$cmakePath -or !(Test-Path -LiteralPath $cmakePath)) {
    throw 'CMake was not found. In Visual Studio Installer, install C++ CMake tools for Windows, or add a standalone CMake installation to PATH.'
}

Write-Host "Using CMake: $cmakePath"
$detailValue = if ($Detail) { 'ON' } else { 'OFF' }
$samplingValue = if ($ManualSampling) { 'ON' } else { 'OFF' }
& $cmakePath -S $projectRoot -B $buildDir -DENGINE_BUILD_TRACY_DEMOS=ON -DENGINE_ENABLE_TRACY=ON `
    "-DENGINE_TRACY_DEMO_DETAIL=$detailValue" "-DENGINE_TRACY_MANUAL_SAMPLING=$samplingValue"
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed (exit $LASTEXITCODE)." }
& $cmakePath --build $buildDir --config $Configuration --target TracyDemos --parallel 4
if ($LASTEXITCODE -ne 0) { throw "Tracy demo build failed (exit $LASTEXITCODE)." }
Write-Host "Demos are ready: $(Join-Path $buildDir "tracy-demos/$Configuration")"
