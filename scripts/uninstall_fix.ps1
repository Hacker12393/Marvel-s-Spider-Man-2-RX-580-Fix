$ErrorActionPreference = "Stop"

param(
    [Parameter(Mandatory = $true)]
    [string]$GamePath
)

$target = Join-Path $GamePath "d3d12.dll"
$cacheDisabled = Join-Path $GamePath "cache.pso.disabled"
$cache = Join-Path $GamePath "cache.pso"

if (Test-Path $target) {
    Remove-Item -LiteralPath $target -Force
    Write-Host "d3d12.dll removida."
} else {
    Write-Host "Nenhuma d3d12.dll encontrada na pasta do jogo."
}

if ((Test-Path $cacheDisabled) -and !(Test-Path $cache)) {
    Rename-Item -LiteralPath $cacheDisabled -NewName "cache.pso"
    Write-Host "cache.pso restaurado."
}

Write-Host "Fix removido."

