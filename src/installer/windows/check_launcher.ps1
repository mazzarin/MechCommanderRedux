# Port-only (the personal build): checks the launcher the setup ships (launcher\MCX.exe) against its pin
# (launcher\MCX.exe.pin), which .github/workflows/windows.yml runs after each build. The setup ships one launcher
# build release after release, so Defender's cloud, which flags a new unsigned program it hasn't seen widely, learns one
# file once (the launcher reads MCX.EXE's neighbours and starts another program, which its machine learning took for a
# trojan, 6 Nov).
#
# Fails when the pinned file isn't the one its pin names, or when the launcher's sources (src/apps/MCXLauncher and its
# icon) changed since the pin: then pin this run's build (its artifact's MCX.exe) with the lines printed. With -Built
# (this build's MCX.exe) it says whether the build is the pinned file byte for byte; one that differs from the same
# sources (a new compiler, or MechCommander.props changed) is a warning, as the setup ships the pinned one regardless.
#
#   check_launcher.ps1 [-Built <the build's MCX.exe>]

param(
    [string]$Built = ''
)

$ErrorActionPreference = 'Stop'

$PinFolder = Join-Path $PSScriptRoot 'launcher'
$PinnedExe = Join-Path $PinFolder 'MCX.exe'
$PinFile = Join-Path $PinFolder 'MCX.exe.pin'
# What the launcher is built from, as the pin names them (git's ids at HEAD).
$Sources = 'src/apps/MCXLauncher'
$Icon = 'src/res/MCRedux.ico'
$PinKeys = 'sha256', 'bytes', 'sources', 'icon', 'built'

function Read-Pin([string]$Path) {
    $pin = @{}
    $number = 0
    foreach ($line in Get-Content -LiteralPath $Path) {
        $number++
        $text = $line.Trim()
        if ($text -eq '' -or $text.StartsWith('#')) { continue }
        $equals = $text.IndexOf('=')
        if ($equals -lt 1) { throw "${Path} line ${number}: not a key=value line: $text" }
        $pin[$text.Substring(0, $equals)] = $text.Substring($equals + 1)
    }
    foreach ($key in $PinKeys) {
        if (-not $pin.ContainsKey($key)) { throw "${Path} has no ${key}=." }
    }
    return $pin
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-GitId([string]$Path) {
    $id = git rev-parse "HEAD:$Path"
    if ($LASTEXITCODE -ne 0 -or -not $id) { throw "git has no $Path at HEAD." }
    return $id.Trim()
}

# The pin a build of these sources would have, for the message that asks for one.
function Format-Pin([string]$Exe, [string]$SourcesId, [string]$IconId) {
    $commit = (git rev-parse HEAD).Trim()
    $run = if ($env:GITHUB_RUN_ID) { " run $env:GITHUB_RUN_ID" } else { '' }
    return @(
        "sha256=$(Get-Sha256 $Exe)",
        "bytes=$((Get-Item -LiteralPath $Exe).Length)",
        "sources=$SourcesId",
        "icon=$IconId",
        "built=$commit$run"
    ) -join "`n"
}

$pin = Read-Pin $PinFile
$problems = @()

$pinnedSha = Get-Sha256 $PinnedExe
$pinnedBytes = (Get-Item -LiteralPath $PinnedExe).Length
$pinHolds = $pinnedSha -eq $pin.sha256 -and "$pinnedBytes" -eq $pin.bytes
if (-not $pinHolds) {
    $problems += "launcher\MCX.exe is $pinnedSha ($pinnedBytes bytes), but its pin names $($pin.sha256) ($($pin.bytes) bytes)."
}

$sourcesId = Get-GitId $Sources
$iconId = Get-GitId $Icon
$sourcesChanged = $sourcesId -ne $pin.sources -or $iconId -ne $pin.icon
if ($sourcesChanged) {
    $problems += "The launcher's sources changed since its pin ($Sources $($pin.sources) -> $sourcesId, $Icon $($pin.icon) -> $iconId), " +
        'but the setup still ships the pinned build. Pin a build of them: its MCX.exe as launcher\MCX.exe (a new file to ' +
        "Defender's cloud, which may flag it until Microsoft clears it)" +
        $(if ($Built) { ", and launcher\MCX.exe.pin as:`n$(Format-Pin $Built $sourcesId $iconId)" } else { '.' })
}

if ($Built -and $pinHolds -and -not $sourcesChanged) {
    $builtSha = Get-Sha256 $Built
    if ($builtSha -eq $pinnedSha) {
        Write-Host "This build's launcher is the pinned one byte for byte ($pinnedSha, built $($pin.built))."
    } else {
        Write-Host ("::warning::This build's launcher ($builtSha) differs from the pinned one ($pinnedSha) built from the " +
            'same sources: a new compiler, or MechCommander.props changed. The setup ships the pinned one.')
    }
}

if ($problems) {
    foreach ($problem in $problems) { Write-Host "::error::$problem" }
    throw "The setup's launcher doesn't match its pin."
}
Write-Host "The setup's launcher is its pin: $pinnedSha, from $Sources $sourcesId."
