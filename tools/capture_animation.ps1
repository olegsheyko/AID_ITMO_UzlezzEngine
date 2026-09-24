param([string]$OutDir='docs/lab1/acceptance/traces')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$exeDir=Join-Path $root 'build/Release'
$out=Join-Path $root $OutDir
New-Item -ItemType Directory -Force -Path $out | Out-Null
foreach ($mode in 'sequential','parallel') {
    $trace=Join-Path $out "animation_$mode.tracy"
    if (Test-Path -LiteralPath $trace) { throw "Trace exists: $trace" }
    $env:TRACY_PORT='8091'
    $game=Start-Process -FilePath (Join-Path $exeDir 'GameEngine.exe') -WorkingDirectory $exeDir -WindowStyle Hidden -PassThru `
        -ArgumentList @('--animation-bench',$mode,'--animation-wait-tracy','--animation-frames','480','--animation-out',('"'+(Join-Path $out "$mode.csv")+'"')) `
        -RedirectStandardOutput (Join-Path $out "$mode-game.log") -RedirectStandardError (Join-Path $out "$mode-game.err")
    $null = $game.Handle
    $capture=Start-Process -FilePath (Join-Path $root 'build/tracy-tools/tracy-capture.exe') -WindowStyle Hidden -PassThru `
        -ArgumentList @('-a','127.0.0.1','-p','8091','-o',('"'+$trace+'"')) `
        -RedirectStandardOutput (Join-Path $out "$mode-capture.log") -RedirectStandardError (Join-Path $out "$mode-capture.err")
    $null = $capture.Handle
    if (-not $game.WaitForExit(60000)) { $game.Kill(); if (-not $capture.HasExited) { $capture.Kill() }; throw 'Game timed out' }
    $game.WaitForExit()
    if (-not $capture.WaitForExit(60000)) { $capture.Kill(); throw 'Capture timed out' }
    $capture.WaitForExit()
    if ($game.ExitCode -ne 0 -or $capture.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $trace)) { throw "Capture failed: $mode" }
    & (Join-Path $root 'build/tracy-tools/tracy-csvexport.exe') -f Animation $trace | Set-Content -Encoding utf8 (Join-Path $out "$mode-zones.csv")
    if ($LASTEXITCODE -ne 0) { throw 'Trace validation failed' }
    Write-Output "Captured and parsed: $trace"
}
