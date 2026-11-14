<#
    make-base.ps1 — build the clean working base install.

    Copies Fallout 2 + Restoration Project Unofficial out of the READ-ONLY
    source install ($Src) into the working base ($Dst, default workflow\base).
    `python setup.py --build-base` runs this for you.

    The source install is never written to. Nothing here modifies it.

    What is deliberately EXCLUDED and why:

      data\  (except sound\)   The source install's loose data\ is 445 files of
                              MDFG mod output from July 2026 plus a Sept 2025
                              savegame. None of it belongs in a clean base.
                              Every base .lst, vault13.gam and scripts.lst
                              comes from mods\rpu.dat instead.

      appearance\             190 MB Appearance mod. Not part of vanilla+RPU.

      backup\, Extras\        RPU installer backups and hi-res translations.
      translations\

      GOG launcher, unins*,   Not needed to run the game directly.
      webcache, logs, dumps

    Optional RPU add-ons (npc_armor, party_orders, cassidy_head, extended
    flamer, rifle/wakizashi animations, enhanced worldmap, improved mysterious
    stranger, ammomod) ARE copied but are NOT enabled: the generated
    mods_order.txt lists rpu.dat only. Each one adds protos and scripts to the
    ID space, and a predictable base is worth more than the extras during
    development. To re-enable one, add its filename to base\mods\mods_order.txt.
#>

param(
    [Parameter(Mandatory = $true)][string]$Src,   # your Fallout 2 install (with RPU), read-only
    [string]$Dst = (Join-Path (Split-Path $PSScriptRoot -Parent) 'base')
)
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Src)) { throw "Source install not found: $Src" }

Write-Host "Source (read-only): $Src"
Write-Host "Base   (target)   : $Dst"
Write-Host ""

# --- record the source state so we can prove we did not touch it -------------
$before = Get-ChildItem -LiteralPath $Src -Recurse -File -Force -EA SilentlyContinue |
          Measure-Object -Property Length -Sum
Write-Host ("Source before: {0} files, {1:N1} GB" -f $before.Count, ($before.Sum/1GB))

New-Item -ItemType Directory -Force -Path $Dst | Out-Null

# --- root files --------------------------------------------------------------
$rootFiles = @(
    'fallout2.exe','fallout2HR.exe','fallout2.cfg','fallout2.ico',
    'ddraw.dll','ddraw.ini','sfall.dat','sfall-FAQ.txt','sfall-readme.txt',
    'f2_res.dll','f2_res.ini','f2_res_config.exe','f2_res_patcher.exe',
    'd3dx9_31.dll','d3dx9_43.dll',
    'master.dat','critter.dat'
)
foreach ($f in $rootFiles) {
    $s = Join-Path $Src $f
    if (Test-Path -LiteralPath $s) {
        Copy-Item -LiteralPath $s -Destination (Join-Path $Dst $f) -Force
    } else {
        Write-Warning "missing in source: $f"
    }
}
Write-Host "root files copied"

# --- mods\ (flat) ------------------------------------------------------------
$null = robocopy "$Src\mods" "$Dst\mods" /E /NFL /NDL /NJH /NJS /NP /R:1 /W:1
if ($LASTEXITCODE -ge 8) { throw "robocopy mods failed ($LASTEXITCODE)" }
Write-Host "mods\ copied"

# --- music -------------------------------------------------------------------
foreach ($pair in @(@('sound','sound'), @('data\sound','data\sound'))) {
    $s = Join-Path $Src $pair[0]
    if (Test-Path -LiteralPath $s) {
        $null = robocopy $s (Join-Path $Dst $pair[1]) /E /NFL /NDL /NJH /NJS /NP /R:1 /W:1
        if ($LASTEXITCODE -ge 8) { throw "robocopy $($pair[0]) failed ($LASTEXITCODE)" }
        Write-Host "$($pair[0])\ copied"
    }
}

# --- empty savegame dir ------------------------------------------------------
New-Item -ItemType Directory -Force -Path (Join-Path $Dst 'data\SAVEGAME') | Out-Null

# --- mods load order: RPU and the add-ons that are present -------------------
$wanted = 'rpu.dat','rpu_enhanced_worldmap.dat','rpu_extended_flamer.dat',
          'rpu_rifle_animations.dat','rpu_wakizashi_animations.dat',
          'npc_armor.dat','party_orders.dat','cassidy_head.dat'
$order = @($wanted | Where-Object { Test-Path -LiteralPath (Join-Path $Dst "mods\$_") })
if ($order -notcontains 'rpu.dat') { Write-Warning 'mods\rpu.dat is missing: install the Restoration Project Unofficial into the source game first.' }
($order -join "`r`n") | Set-Content -LiteralPath (Join-Path $Dst 'mods\mods_order.txt') -Encoding ascii
Write-Host "mods_order.txt written ($($order.Count) mods)"

# --- convenience launcher ----------------------------------------------------
# The game must run from its own directory or it will not find its data.
@'
@echo off
rem Launch the working base. Must run from the game directory.
cd /d "%~dp0"
start "" fallout2.exe
'@ | Set-Content -LiteralPath (Join-Path $Dst 'PLAY.bat') -Encoding ascii
Write-Host "PLAY.bat written"

# --- verify source untouched -------------------------------------------------
$after = Get-ChildItem -LiteralPath $Src -Recurse -File -Force -EA SilentlyContinue |
         Measure-Object -Property Length -Sum
Write-Host ""
Write-Host ("Source after : {0} files, {1:N1} GB" -f $after.Count, ($after.Sum/1GB))
if ($after.Count -ne $before.Count -or $after.Sum -ne $before.Sum) {
    Write-Error "SOURCE INSTALL CHANGED. Investigate before continuing."
} else {
    Write-Host "Source install verified unchanged." -ForegroundColor Green
}

$b = Get-ChildItem -LiteralPath $Dst -Recurse -File -Force | Measure-Object -Property Length -Sum
Write-Host ("Base built   : {0} files, {1:N1} GB" -f $b.Count, ($b.Sum/1GB))
