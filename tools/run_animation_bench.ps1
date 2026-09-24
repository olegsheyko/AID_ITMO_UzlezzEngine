param([string]$BuildDir='build', [string]$OutDir='docs/lab1/acceptance/animation', [int]$Runs=3, [int]$Characters=512, [int]$Frames=360)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$exeDir=Join-Path $root "$BuildDir/Release"
$out=Join-Path $root $OutDir
New-Item -ItemType Directory -Force -Path $out | Out-Null
for ($run=1; $run -le $Runs; $run++) {
    $modes=if ($run % 2) { @('sequential','parallel') } else { @('parallel','sequential') }
    foreach ($mode in $modes) {
        $csv=Join-Path $out "${mode}_${run}.csv"
        if (Test-Path -LiteralPath $csv) { throw "Output already exists: $csv; choose another OutDir" }
        $stdout=Join-Path $out "${mode}_${run}.log"
        $stderr=Join-Path $out "${mode}_${run}.err"
        $process=Start-Process -FilePath (Join-Path $exeDir 'GameEngine.exe') -WorkingDirectory $exeDir -WindowStyle Hidden -PassThru `
            -ArgumentList @('--animation-bench',$mode,'--animation-characters',$Characters,'--animation-frames',$Frames,'--animation-out',('"'+$csv+'"')) `
            -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        $null = $process.Handle
        if (-not $process.WaitForExit(120000)) { $process.Kill(); throw "Benchmark timeout: $mode $run" }
        $process.WaitForExit()
        if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $csv)) { throw "Benchmark failed: $mode $run ($($process.ExitCode))" }
        if (-not (Select-String -LiteralPath $csv -Pattern '^# complete=1$' -Quiet)) { throw 'Incomplete benchmark' }
        Write-Output "$mode run $run complete: $csv"
    }
}
