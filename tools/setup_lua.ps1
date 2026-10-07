$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$downloads = @(
    @('https://www.lua.org/ftp/lua-5.4.8.tar.gz', 'external/lua/source.tar.gz', 'external/lua', '4F18DDAE154E793E46EEAB727C59EF1C0C0C2B744E7B94219710D76F530629AE'),
    @('https://github.com/ThePhD/sol2/archive/refs/tags/v3.3.1.zip', 'external/sol2/source.zip', 'external/sol2', '8976A3F5302AB6328E0C6B61FA8ECE3DF877D709EE0CD73FC1D2766245AF753E')
)
foreach ($item in $downloads) {
    $archive = Join-Path $repoRoot $item[1]
    $destination = Join-Path $repoRoot $item[2]
    New-Item -ItemType Directory -Force $destination | Out-Null
    if (-not (Test-Path $archive)) { Invoke-WebRequest $item[0] -OutFile $archive }
    if ((Get-FileHash $archive).Hash -ne $item[3]) { throw "Checksum mismatch: $archive" }
    if ($archive.EndsWith('.zip')) { Expand-Archive $archive $destination -Force }
    else { tar -xf $archive -C $destination; if ($LASTEXITCODE -ne 0) { throw 'Lua extraction failed' } }
}
Write-Output 'Lua 5.4.8 and sol2 3.3.1 are ready. Lua is linked statically.'
