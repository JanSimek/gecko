# _swap_and_launch.ps1 - open gecko on ONE game's data, in isolation from your editing
# profile, then put your profile back when the window closes.
#
# You normally never run this by hand: tools\build_gecko_browsers.py writes one launcher
# (.bat) per game next to it, and `python foc.py browse` lists them. Why a swap and not
# more --data paths: see the docstring of tools\build_gecko_browsers.py. Also, gecko's own
# --data flag only counts when settings.json has no data paths yet (first run), so passing
# it on the command line cannot work once you have a profile - the file has to be swapped.

param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string[]]$DataPaths,
    # tools\sources.json key for this game. When set, stamps saved during the session are
    # converted into your mod's id space when gecko closes (tools\port_stamp.py).
    [string]$Source = ''
)

$ErrorActionPreference = 'Stop'

$info = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'launch.json') -Raw | ConvertFrom-Json
$geckoExe = $info.gecko
if (-not $geckoExe -or -not (Test-Path -LiteralPath $geckoExe)) {
    throw "gecko.exe not found ('$geckoExe'). Put its path in workflow\config.json (""gecko"") or run: python setup.py"
}

$settingsDir = if ($env:GECKO_SETTINGS_DIR) { $env:GECKO_SETTINGS_DIR } else { Join-Path $env:LOCALAPPDATA 'gecko\gecko' }
$settingsPath = Join-Path $settingsDir 'settings.json'
$backupPath   = Join-Path $settingsDir 'settings.json.foc-live'
New-Item -ItemType Directory -Force -Path $settingsDir | Out-Null

# REFUSE rather than kill. Only one gecko session - browsing or editing - can exist at a
# time, because there is only one settings.json. Opening a second raced the first for the
# file and left settings.json pointed at the wrong game with no backup to restore from.
$existing = Get-Process gecko -ErrorAction SilentlyContinue
if ($existing) {
    Write-Host ""
    Write-Host "gecko is already running (PID $($existing.Id -join ', '))." -ForegroundColor Red
    Write-Host "Close that window first, then run this launcher again." -ForegroundColor Red
    Write-Host ""
    exit 1
}

# Exactly one backup of your profile. If one exists we are being re-run after a session that
# did not get to restore (gecko crashed, the window was killed): backing up again would
# overwrite the real profile with this run's already-swapped file.
$hadProfile = Test-Path -LiteralPath $settingsPath
if ($hadProfile -and -not (Test-Path -LiteralPath $backupPath)) {
    Copy-Item -LiteralPath $settingsPath -Destination $backupPath -Force
    Write-Host "Backed up your gecko profile."
} elseif (Test-Path -LiteralPath $backupPath) {
    Write-Host "Your profile is already backed up from an earlier session - leaving it as it is."
}

# A tile id that a game's own tiles.lst does not cover needs SOME art (blank.frm) or gecko
# fails outright. _fallback_art holds the six small files gecko ships, and nothing else.
$resourcesPath = Join-Path $PSScriptRoot '_fallback_art'
$allPaths = @($resourcesPath) + $DataPaths

$browse = [ordered]@{
    dataPaths       = $allPaths
    gameLocation    = @{}
    selectionColors = @{}
    textEditor      = @{ mode = 'system' }
    ui              = @{ edgeScrollEnabled = $true; mergeSelectionOutlines = $true; windowMaximized = $true }
    version         = '1.1'
}
$browse | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $settingsPath -Encoding utf8

Write-Host ""
Write-Host "Opening gecko on: $Name"
Write-Host "  This is a look-and-copy session. Do not use File > Save on this game's maps:"
Write-Host "  gecko would write into that game's own install."
Write-Host "  Save STAMPS (Edit > Stamp Pattern) - those are what you came for."
Write-Host "Close the gecko window when you are done."
Write-Host ""

$sessionStart = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds() - 5
Start-Process -FilePath $geckoExe -Wait

if (Test-Path -LiteralPath $backupPath) {
    Copy-Item -LiteralPath $backupPath -Destination $settingsPath -Force
    Remove-Item -LiteralPath $backupPath -Force
    Write-Host "Restored your gecko profile."
} elseif (-not $hadProfile) {
    # first ever gecko session: there was no profile to put back. Leave none rather than one
    # that points at the game just browsed.
    Remove-Item -LiteralPath $settingsPath -Force -ErrorAction SilentlyContinue
    Write-Host "No earlier gecko profile existed, so settings were reset. Run: python foc.py edit"
} else {
    Write-Host "WARNING: no backup found - settings.json still points at $Name." -ForegroundColor Yellow
    Write-Host "Run:  python foc.py edit   to point gecko at your mod again." -ForegroundColor Yellow
}

# Stamps saved in this session hold THIS game's proto, art and tile numbers, and every gecko
# session saves into the same patterns folder. Stamped into your mod they would draw a random
# mess. Convert them now (originals are kept in workflow\_safety_backups\stamp_originals\),
# then repack so any art they use sits below the engine's 4096 cap. Both are no-ops when
# nothing was saved; a failure here never blocks the restore above.
if ($Source) {
    $tools = Split-Path $PSScriptRoot -Parent
    $py = Get-Command python -ErrorAction SilentlyContinue
    if (-not $py) {
        Write-Host "python not found - convert this session's stamps by hand:" -ForegroundColor Yellow
        Write-Host "  python tools\port_stamp.py --all --since $sessionStart --source $Source" -ForegroundColor Yellow
    } else {
        Write-Host ""
        Write-Host "Converting stamps saved this session to your mod's ids (a minute or two)..."
        try {
            & $py.Source "$tools\port_stamp.py" --all --since $sessionStart --source $Source
            if ($LASTEXITCODE -eq 0) { & $py.Source "$tools\repack_art.py" }
        } catch {
            Write-Host "Stamp conversion failed: $_" -ForegroundColor Yellow
        }
    }
    Write-Host ""
    Read-Host "Done - press Enter to close this window"
}
