param(
    [switch]$Arena,
    [switch]$Culling,
    [switch]$SmokeTest
)

$ErrorActionPreference = "Stop"

$workspaceDirectory = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$profilerPath = Join-Path $workspaceDirectory "build\tracy-profiler\tracy-profiler.exe"
$enginePath = Join-Path $workspaceDirectory "build\vscode\Release\GameEngine.exe"

if (-not (Test-Path -LiteralPath $profilerPath)) {
    throw "Tracy Profiler is missing: $profilerPath"
}
if (-not (Test-Path -LiteralPath $enginePath)) {
    throw "GameEngine is missing: $enginePath"
}

$startedProfiler = $false
$profiler = Get-Process -Name "tracy-profiler" -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $profiler) {
    $profilerOptions = @{
        FilePath = $profilerPath
        ArgumentList = @("-a", "127.0.0.1")
        WorkingDirectory = $workspaceDirectory
        PassThru = $true
    }
    if ($SmokeTest) {
        $profilerOptions.WindowStyle = "Hidden"
    }
    $profiler = Start-Process @profilerOptions
    $startedProfiler = $true
}

$engineArguments = @()
if ($Arena) {
    $engineArguments += "--editor-arena"
}
if ($Culling) {
    $engineArguments += @("--editor-culling", "--no-vsync")
}
if ($SmokeTest) {
    $engineArguments += @("--stress-seconds", "3", "--stress-out", "build/vscode/tracy-task-smoke.json")
}

$engineOptions = @{
    FilePath = $enginePath
    WorkingDirectory = $workspaceDirectory
    PassThru = $true
    Wait = $true
}
if ($engineArguments.Count -gt 0) {
    $engineOptions.ArgumentList = $engineArguments
}
if ($SmokeTest) {
    $engineOptions.WindowStyle = "Hidden"
}

$engine = Start-Process @engineOptions
if ($SmokeTest -and $startedProfiler -and -not $profiler.HasExited) {
    Stop-Process -Id $profiler.Id
}
exit $engine.ExitCode
