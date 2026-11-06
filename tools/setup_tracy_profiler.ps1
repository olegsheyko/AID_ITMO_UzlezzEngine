param(
    [string]$Version = "0.14.1"
)

$ErrorActionPreference = "Stop"

$installDirectory = Join-Path $PSScriptRoot "..\build\tracy-profiler"
$profilerPath = Join-Path $installDirectory "tracy-profiler.exe"

if (Test-Path -LiteralPath $profilerPath) {
    Write-Host "Tracy Profiler $Version is already installed."
    exit 0
}

$archivePath = Join-Path $installDirectory "windows-$Version.zip"
$downloadUrl = "https://github.com/wolfpld/tracy/releases/download/v$Version/windows-$Version.zip"
$expectedSha256 = "f7499d74914aa3ba94a2c1ce72f36477d7b61d9d0f7c9790e05274c258c97fb5"

New-Item -ItemType Directory -Force -Path $installDirectory | Out-Null
Write-Host "Downloading Tracy Profiler $Version..."
Invoke-WebRequest -Uri $downloadUrl -OutFile $archivePath

$actualSha256 = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualSha256 -ne $expectedSha256) {
    throw "Tracy archive checksum mismatch. Expected $expectedSha256, got $actualSha256."
}

Expand-Archive -LiteralPath $archivePath -DestinationPath $installDirectory -Force

if (-not (Test-Path -LiteralPath $profilerPath)) {
    throw "Tracy Profiler was not found after extracting $archivePath."
}

Write-Host "Tracy Profiler $Version installed to $profilerPath"
