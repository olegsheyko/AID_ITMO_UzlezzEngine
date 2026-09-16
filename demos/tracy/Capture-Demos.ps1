param(
    [ValidateSet('all', 'zones', 'memory', 'locks', 'messages', 'gpu')]
    [string]$Demo = 'all',
    [ValidateRange(2, 30)]
    [int]$Seconds = 5
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$outputDir = Join-Path $projectRoot 'build/tracy-demo-captures'
$toolsDir = Join-Path $projectRoot 'build/tracy-tools'
$captureTool = Join-Path $toolsDir 'tracy-capture.exe'
$exportTool = Join-Path $toolsDir 'tracy-csvexport.exe'
if (!(Test-Path -LiteralPath $captureTool) -or !(Test-Path -LiteralPath $exportTool)) {
    throw 'Extract the official Tracy 0.14.1 Windows tools into build/tracy-tools first. See README.md.'
}
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$demos = if ($Demo -eq 'all') { @('zones', 'memory', 'locks', 'messages', 'gpu') } else { @($Demo) }
$savedPort = $env:TRACY_PORT
try {
    foreach ($name in $demos) {
        $env:TRACY_PORT = '18286'
        $exe = Join-Path $projectRoot "build/tracy-demos/Release/demo_$name.exe"
        if (!(Test-Path -LiteralPath $exe)) { throw "Build TracyDemos in Release first: $exe" }
        $trace = Join-Path $outputDir "demo_$name.tracy"
        $shot = Join-Path $outputDir "demo_$name.bmp"
        $app = $null
        $capture = $null
        try {
            $app = Start-Process -FilePath $exe -ArgumentList @('--hidden', '--auto', '--seconds', ($Seconds + 3), '--screenshot', ('"' + $shot + '"')) `
                -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru `
                -RedirectStandardOutput (Join-Path $outputDir "$name.log") -RedirectStandardError (Join-Path $outputDir "$name-errors.log")
            $capture = Start-Process -FilePath $captureTool -ArgumentList @('-a', '127.0.0.1', '-p', '18286', '-s', $Seconds, '-o', ('"' + $trace + '"'), '-f') `
                -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $outputDir "$name-capture.log") `
                -RedirectStandardError (Join-Path $outputDir "$name-capture-errors.log")
            if (!$capture.WaitForExit(($Seconds + 15) * 1000)) { throw "Capture timed out: $name" }
            if ($capture.ExitCode -ne 0) { throw "Capture failed: $name; inspect capture log" }
            if (!$app.WaitForExit(10000)) { throw "Demo did not exit cleanly: $name" }
            if ($app.ExitCode -ne 0) { throw "Demo failed: $name; inspect error log" }
            & $exportTool $trace | Set-Content -LiteralPath (Join-Path $outputDir "demo_$name.csv") -Encoding utf8
            if ($LASTEXITCODE -ne 0) { throw "Invalid trace: $name" }
            if ($name -eq 'messages') {
                & $exportTool -m $trace | Set-Content -LiteralPath (Join-Path $outputDir 'messages-events.csv') -Encoding utf8
                if ($LASTEXITCODE -ne 0) { throw 'Message export failed' }
            }
            if ($name -eq 'gpu') {
                & $exportTool -g $trace | Set-Content -LiteralPath (Join-Path $outputDir 'gpu-events.csv') -Encoding utf8
                if ($LASTEXITCODE -ne 0) { throw 'GPU export failed' }
            }
            if (!(Test-Path -LiteralPath $shot)) { throw "Demo did not render its verification frame: $name" }
            Write-Output "PASS: demo_$name - capture, screenshot and clean shutdown"
        } finally {
            if ($capture -and !$capture.HasExited) { Stop-Process -Id $capture.Id }
            if ($app -and !$app.HasExited) { Stop-Process -Id $app.Id }
        }
    }
} finally {
    $env:TRACY_PORT = $savedPort
}
