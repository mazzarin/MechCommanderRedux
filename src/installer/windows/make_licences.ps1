# Port-only (the personal build): writes the setup's licences file, the open-source code built into the Windows
# program (MCRedux.exe, SDL3.dll, MCEditor.exe) with each one's licence, for MechCommanderRedux.iss. The libraries and
# their licences are src\lib\licences.json's (the Mac bundle's are made from it too); a folder in src\lib it doesn't
# name stops it.
#
#   pwsh -NoProfile -File src\installer\windows\make_licences.ps1 -LibDir src\lib -Out <file>
param(
    [Parameter(Mandatory)][string]$LibDir,
    [Parameter(Mandatory)][string]$Out
)

$ErrorActionPreference = 'Stop'

$platform = 'windows'
$manifest = Get-Content -LiteralPath (Join-Path $LibDir 'licences.json') -Raw -Encoding utf8 | ConvertFrom-Json
$named = @($manifest.Ours) + @($manifest.Libraries | ForEach-Object { $_.Folder })
$unnamed = @(Get-ChildItem -LiteralPath $LibDir -Directory | Where-Object { $named -notcontains $_.Name })

if ($unnamed.Count -gt 0) {
    throw "src\lib holds $(($unnamed | ForEach-Object Name) -join ', '), which licences.json doesn't name: add its licence there"
}

$missing = @($named | Where-Object { -not (Test-Path -LiteralPath (Join-Path $LibDir $_) -PathType Container) })

if ($missing.Count -gt 0) {
    throw "licences.json names $($missing -join ', '), which src\lib doesn't hold"
}

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add('The open-source code in MechCommander Redux, and its licences.')
$lines.Add('')

foreach ($library in $manifest.Libraries) {
    if ($library.Only -and $library.Only -ne $platform) {
        continue
    }

    if ($library.Note) {
        $licence = $library.Note
    }
    else {
        $file = Join-Path (Join-Path $LibDir $library.Folder) $library.File

        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
            throw "No licence file for $($library.Name): $file"
        }

        $licence = (Get-Content -LiteralPath $file -Raw -Encoding utf8).Trim()
    }

    $lines.Add("==== $($library.Name) ====")
    $lines.Add('')
    $lines.Add($licence)
    $lines.Add('')
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Out) | Out-Null
# One kind of line ending throughout (the licence files differ), as Notepad shows it.
$text = ($lines -join "`n") -replace "`r`n", "`n" -replace "`n", "`r`n"
[System.IO.File]::WriteAllText($Out, $text, [System.Text.UTF8Encoding]::new($false))
