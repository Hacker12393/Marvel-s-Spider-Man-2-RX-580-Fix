$ErrorActionPreference = "Stop"

param(
    [Parameter(Mandatory = $true)]
    [string]$GamePath
)

$repoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$dll = Join-Path $repoRoot "release\d3d12.dll"
$target = Join-Path $GamePath "d3d12.dll"
$exe = Join-Path $GamePath "Spider-Man2.exe"
$cache = Join-Path $GamePath "cache.pso"

if (!(Test-Path $dll)) {
    throw "Nao encontrei release\d3d12.dll."
}

if (!(Test-Path $exe)) {
    throw "Nao encontrei Spider-Man2.exe em: $GamePath"
}

$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$backup = Join-Path $GamePath "RX580FixBackup_$stamp"
New-Item -ItemType Directory -Force -Path $backup | Out-Null

if (Test-Path $target) {
    Copy-Item -LiteralPath $target -Destination (Join-Path $backup "d3d12.dll") -Force
}

Copy-Item -LiteralPath $dll -Destination $target -Force

Write-Host "Fix instalado em:"
Write-Host $target
Write-Host ""
Write-Host "Backup criado em:"
Write-Host $backup
Write-Host ""

if (Test-Path $cache) {
    Write-Host "Dica: se o jogo travar no loading, renomeie cache.pso para cache.pso.disabled."
}

