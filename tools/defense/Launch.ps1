param(
    [ValidateSet('Menu','Check','Editor','Tracy','Capture','Bench','Tests','Report','Trace')]
    [string]$Mode='Menu',
    [ValidateRange(1,300)][int]$Seconds=30,
    [string]$EngineExe='', [string]$TracyDir='', [string]$BuildDir='',
    [switch]$NoOpen, [switch]$Headless
)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$configPath=Join-Path $PSScriptRoot 'local.json'
if (Test-Path -LiteralPath $configPath) {
    $config=Get-Content -LiteralPath $configPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if (!$EngineExe) { $EngineExe=$config.EngineExe }
    if (!$TracyDir) { $TracyDir=$config.TracyDir }
    if (!$BuildDir) { $BuildDir=$config.BuildDir }
}
function Resolve-ProjectPath([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $root $Path))
}
if (!$BuildDir) { $BuildDir='build/windows' }
$BuildDir=Resolve-ProjectPath $BuildDir
if (!$EngineExe) { $EngineExe=Join-Path $BuildDir 'Release/GameEngine.exe' }
$EngineExe=Resolve-ProjectPath $EngineExe
if (!$TracyDir) { $TracyDir='build/tracy-tools' }
$TracyDir=Resolve-ProjectPath $TracyDir
function Require-File([string]$Path) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing file: $Path. Set paths in tools/defense/local.json." }
}
function Start-Engine {
    Require-File $EngineExe
    if (Get-Process GameEngine -ErrorAction SilentlyContinue) { throw 'Close the existing GameEngine first. No processes were stopped.' }
    $parameters=@{FilePath=$EngineExe; WorkingDirectory=(Split-Path -Parent $EngineExe); PassThru=$true}
    $parameters.ArgumentList=@('--editor-culling','--no-vsync','--editor-tab','renderer')
    if ($Headless) {
        $parameters.ArgumentList+=@('--editor-screenshot',('"'+(Join-Path $run 'editor.png')+'"'),'--editor-frames','100000')
        $parameters.WindowStyle='Hidden'
    }
    Start-Process @parameters
}
function Check-Tracy {
    Require-File (Join-Path $TracyDir 'tracy-profiler.exe')
    Require-File (Join-Path $TracyDir 'tracy-capture.exe')
    $help=& (Join-Path $TracyDir 'tracy-capture.exe') --help 2>&1 | Out-String
    if ($help -notmatch '0\.14\.1') { throw 'Tracy 0.14.1 is required for this engine.' }
    $cache=Join-Path $BuildDir 'CMakeCache.txt'
    Require-File $cache
    if (!(Select-String -LiteralPath $cache -Pattern '^ENGINE_ENABLE_TRACY:BOOL=ON$' -Quiet)) { throw 'Rebuild Release with ENGINE_ENABLE_TRACY=ON.' }
    if (Get-NetTCPConnection -LocalPort 8091 -State Listen -ErrorAction SilentlyContinue) { throw 'Tracy port 8091 is busy. Close the previous profiling session.' }
}
try {
    if ($Mode -eq 'Menu') {
        Write-Host 'DEFENSE: Culling + submission'
        Write-Host '1 Editor (10,000 objects / 100 animated)'
        Write-Host '2 Editor + Tracy live'
        Write-Host '3 Record 30-second trace, then open it'
        Write-Host '4 Formal benchmark (3 runs per mode, without Tracy)'
        Write-Host '5 Run tests'
        Write-Host '6 Open report'
        Write-Host '7 Open latest saved trace'
        Write-Host '8 Check paths and profiler'
        $choice=Read-Host 'Choose 1-8'
        $modes=@{'1'='Editor';'2'='Tracy';'3'='Capture';'4'='Bench';'5'='Tests';'6'='Report';'7'='Trace';'8'='Check'}
        if (!$modes.ContainsKey($choice)) { throw 'Unknown choice' }
        $Mode=$modes[$choice]
    }
    $output=Join-Path $PSScriptRoot 'output'
    if ($Mode -eq 'Report') { Start-Process (Join-Path $root 'docs/culling/README.md'); return }
    if ($Mode -eq 'Trace') {
        Require-File (Join-Path $TracyDir 'tracy-profiler.exe')
        $trace=Get-ChildItem -LiteralPath $output -Filter '*.tracy' -Recurse | Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if (!$trace) { throw 'No saved trace. Run Capture first.' }
        Start-Process (Join-Path $TracyDir 'tracy-profiler.exe') -ArgumentList ('"'+$trace.FullName+'"'); return
    }
    Require-File $EngineExe
    Require-File (Join-Path (Split-Path -Parent $EngineExe) 'assets/shaders/mesh_vertex.glsl')
    if ($Mode -in @('Check','Tracy','Capture')) { Check-Tracy }
    if ($Mode -eq 'Check') { Write-Host "OK: $EngineExe`nTracy 0.14.1: $TracyDir`nPort: 8091"; return }
    if ($Mode -eq 'Tests') {
        $ctest=Get-Command ctest -ErrorAction SilentlyContinue
        if ($ctest) { $ctestPath=$ctest.Source } else {
            $cachePath=Join-Path $BuildDir 'CMakeCache.txt'
            Require-File $cachePath
            $entry=Select-String -LiteralPath $cachePath -Pattern '^CMAKE_CTEST_COMMAND:INTERNAL=(.+)$' | Select-Object -First 1
            if (!$entry) { throw 'CTest not found. Add CMake to PATH or reconfigure the build.' }
            $ctestPath=$entry.Matches[0].Groups[1].Value
        }
        Require-File $ctestPath
        & $ctestPath --test-dir $BuildDir -C Release --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }; return
    }
    $run=Join-Path $output ((Get-Date -Format 'yyyyMMdd-HHmmss-fff')+'-'+$Mode)
    New-Item -ItemType Directory -Path $run -Force | Out-Null
    if ($Mode -eq 'Bench') {
        if (Get-Process GameEngine,tracy-profiler,tracy-capture -ErrorAction SilentlyContinue) { throw 'Close GameEngine and Tracy before formal measurements.' }
        & python (Join-Path $root 'tools/run_culling_bench.py') $EngineExe --animated --out $run
        if ($LASTEXITCODE -ne 0) { throw 'Benchmark failed' }
        Write-Host "Results: $run"; return
    }
    $previousPort=$env:TRACY_PORT
    try {
        $env:TRACY_PORT='8091'
        $game=Start-Engine
        $null=$game.Handle
        if ($Mode -eq 'Editor') { Write-Host "Editor PID $($game.Id)"; return }
        # Inspect the listener without opening a TCP connection to Tracy's protocol.
        $ready=$false
        for ($i=0;$i -lt 60;$i++) {
            if ($game.HasExited) { throw 'Engine exited before Tracy started.' }
            $listener=Get-NetTCPConnection -LocalPort 8091 -State Listen -ErrorAction SilentlyContinue
            if ($listener | Where-Object OwningProcess -eq $game.Id) { $ready=$true; break }
            Start-Sleep -Milliseconds 250
        }
        if (!$ready) { throw 'Tracy listener did not start. Check Release build and instrumentation.' }
        if ($Mode -eq 'Tracy') {
            Start-Process (Join-Path $TracyDir 'tracy-profiler.exe') -ArgumentList '-a','127.0.0.1','-p','8091'
            Write-Host 'Tracy connecting to 127.0.0.1:8091. Close both windows after the demonstration.'
        } else {
            $trace=Join-Path $run 'culling.tracy'
            $capture=Start-Process (Join-Path $TracyDir 'tracy-capture.exe') -WindowStyle Hidden -PassThru -ArgumentList @('-a','127.0.0.1','-p','8091','-s',"$Seconds",'-o',('"'+$trace+'"')) -RedirectStandardOutput (Join-Path $run 'capture.log') -RedirectStandardError (Join-Path $run 'capture.err')
            $null=$capture.Handle
            Write-Host "Recording $Seconds seconds. Move camera / toggle culling and sorting now."
            if (!$capture.WaitForExit(($Seconds+45)*1000)) { $capture.Kill(); throw 'Tracy capture timed out; see logs.' }
            $capture.WaitForExit()
            if ($capture.ExitCode -ne 0) { throw 'Tracy capture failed; see capture logs.' }
            Require-File $trace
            $exporter=Join-Path $TracyDir 'tracy-csvexport.exe'
            Require-File $exporter
            & $exporter -f 'Scene visibility' $trace | Set-Content (Join-Path $run 'zones.csv') -Encoding UTF8
            if ($LASTEXITCODE -ne 0) { throw 'Trace could not be parsed.' }
            if (!(Select-String -LiteralPath (Join-Path $run 'zones.csv') -SimpleMatch 'Scene visibility and submission' -Quiet)) { throw 'Trace is missing engine render zones.' }
            Write-Host "Saved and parsed: $trace"
            if (!$NoOpen) { Start-Process (Join-Path $TracyDir 'tracy-profiler.exe') -ArgumentList ('"'+$trace+'"') }
        }
    } finally {
        $env:TRACY_PORT=$previousPort
        if ($Headless -and $game -and !$game.HasExited) { Stop-Process -Id $game.Id }
    }
} catch { Write-Host $_ -ForegroundColor Red; exit 1 }
