# Build core.lvl directly - uses tools from packages\shaderpatch-tools
$repoRoot = "C:\Users\tehpa\source\repos\shaderpatch"
$coreDir = Join-Path $repoRoot "assets\core"
$binPath = Join-Path $repoRoot "packages\shaderpatch-tools"

if (-not (Test-Path (Join-Path $binPath "sp_texture_munge.exe"))) {
    Write-Error "Tools not found at: $binPath"
    Write-Error "Make sure packages\shaderpatch-tools contains sp_texture_munge.exe and lvl_pack.exe"
    exit 1
}

Push-Location $coreDir

$old_path = $env:Path
$env:Path += ";" + $binPath

Write-Host "Munging textures..." -ForegroundColor Cyan
sp_texture_munge --outputdir "munged\" --sourcedir "textures\"

Write-Host "Packing core.lvl..." -ForegroundColor Cyan
lvl_pack -i "munged\" -i "fonts\" --sourcedir ".\" --outputdir ".\"

$env:Path = $old_path

Pop-Location

Write-Host "Done!" -ForegroundColor Green
