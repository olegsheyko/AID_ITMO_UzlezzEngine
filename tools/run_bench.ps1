# Замеры ЛР 1 на Windows: собирает Release, гоняет оба сценария N раз, печатает сводку.
# Аналог tools/run_bench.sh. Написан без возможности запустить на Windows — при ошибке
# сверяйся с run_bench.sh, логика та же.
#
#   powershell -ExecutionPolicy Bypass -File tools\run_bench.ps1 -Label before
#   powershell -ExecutionPolicy Bypass -File tools\run_bench.ps1 -Label after -Runs 3 -Mode async
#
# CSV складываются в build\win\Release\bench\<метка>\<сценарий>_<i>.csv.
# Во время прогонов не сворачивай окно движка.
param(
    [Parameter(Mandatory = $true)][string]$Label,
    [int]$Runs = 3,
    [ValidateSet('async', 'sync')][string]$Mode = 'async',
    [string]$BuildDir = 'build\win'
)
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root $BuildDir
if (-not (Test-Path (Join-Path $build 'CMakeCache.txt'))) {
    cmake -S $root -B $build -G 'Visual Studio 17 2022' -A x64
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
}
cmake --build $build --config Release --target GameEngine CopyAssets
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }

$exeDir = Join-Path $build 'Release'
$out = Join-Path $exeDir "bench\$Label"
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force $out | Out-Null

foreach ($scenario in 'burst', 'stream') {
    for ($i = 1; $i -le $Runs; $i++) {
        Write-Host "[$Label] $scenario, ${Mode}: run $i of $Runs"
        $arguments = @('--bench', $scenario, '--load-mode', $Mode, '--bench-out', "bench\$Label\${scenario}_$i.csv")
        $process = Start-Process -FilePath (Join-Path $exeDir 'GameEngine.exe') -WorkingDirectory $exeDir `
            -ArgumentList $arguments -PassThru
        if (-not $process.WaitForExit(120000)) {
            $process.Kill()
            throw "Run hung: $scenario $i"
        }
        if ($process.ExitCode -ne 0) { throw "Run failed with exit code $($process.ExitCode): $scenario $i" }
    }
}

$python = if (Get-Command python -ErrorAction SilentlyContinue) { 'python' } else { 'py' }
& $python (Join-Path $root 'tools\bench_stats.py') (Get-ChildItem (Join-Path $out '*.csv') | ForEach-Object { $_.FullName })
