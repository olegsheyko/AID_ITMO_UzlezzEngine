param([int]$Seconds=300, [int]$Repeats=20, [string]$OutDir='docs/lab1/acceptance/stability')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$exeDir=Join-Path $root 'build/Release'
$out=Join-Path $root $OutDir
New-Item -ItemType Directory -Force -Path $out | Out-Null
function Run-Checked([string]$name,[string[]]$argsList,[int]$timeout) {
    $stdout=Join-Path $out "$name.log"
    $process=Start-Process -FilePath (Join-Path $exeDir 'GameEngine.exe') -WorkingDirectory $exeDir -WindowStyle Hidden -PassThru `
        -ArgumentList $argsList -RedirectStandardOutput $stdout -RedirectStandardError (Join-Path $out "$name.err")
    $null = $process.Handle # Cache the native handle before fast processes exit.
    if (-not $process.WaitForExit($timeout)) { $process.Kill(); throw "Timeout: $name" }
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) { throw "Nonzero exit: $name ($($process.ExitCode))" }
    if (-not (Select-String -LiteralPath $stdout -SimpleMatch 'Shutdown complete' -Quiet)) { throw "Shutdown missing: $name" }
    if (Select-String -LiteralPath $stdout -SimpleMatch '[ERROR]' -Quiet) { throw "Engine error: $name" }
    Write-Output "$name passed"
}
$results=@()
for ($i=1; $i -le $Repeats; $i++) {
    $name="exit_loading_$i"
    $csv=Join-Path $out "$name.csv"
    Run-Checked $name @('--animation-bench','parallel','--animation-characters','64','--animation-exit-loading','--animation-out',('"'+$csv+'"')) 30000
    $pendingLine=Select-String -LiteralPath $csv -Pattern '^# pending_at_exit=([1-9][0-9]*)$'
    if (-not $pendingLine) { throw "No live loading at exit: $name" }
    $results+=[pscustomobject]@{scenario=$name;exit=0;pending=[int]$pendingLine.Matches[0].Groups[1].Value}
}
for ($i=1; $i -le $Repeats; $i++) {
    $name="restart_$i"
    $csv=Join-Path $out "$name.csv"
    Run-Checked $name @('--animation-bench','parallel','--animation-characters','64','--animation-frames','60','--animation-out',('"'+$csv+'"')) 30000
    $results+=[pscustomobject]@{scenario=$name;exit=0;pending=0}
}
$results | Export-Csv -NoTypeInformation -Encoding utf8 (Join-Path $out 'launches.csv')
Run-Checked 'soak' @('--animation-bench','parallel','--animation-characters','128','--animation-frames','10000000',
    '--stress-seconds',$Seconds,'--stress-out',('"'+(Join-Path $out 'soak.txt')+'"')) (($Seconds+30)*1000)
