# Port-only (the personal build): writes the setup's licences file, the open-source code built into the Windows
# program (MCRedux.exe, SDL3.dll, MCEditor.exe) with each one's licence, for MechCommanderRedux.iss.
#
#   pwsh -NoProfile -File src\installer\windows\make_licences.ps1 -LibDir src\lib -Out <file>
param(
    [Parameter(Mandatory)][string]$LibDir,
    [Parameter(Mandatory)][string]$Out
)

$ErrorActionPreference = 'Stop'

# Each library and its licence file under src\lib.
$licenceFiles = [ordered]@{
    'SDL 3.4.12'      = 'SDL-release-3.4.12\LICENSE.txt'
    'dav1d 1.5.4'     = 'dav1d-1.5.4\COPYING'
    'libdeflate 1.26' = 'libdeflate-1.26\COPYING'
    'mimalloc 3.3.1'  = 'mimalloc-3.3.1\LICENSE'
    'miniz 3.0.2'     = 'miniz-3.0.2\LICENSE'
    'xxHash 0.8.4'    = 'xxHash-0.8.4\LICENSE'
    'bc7enc_rdo'      = 'bc7enc_rdo\LICENSE'
}
# Code whose licence is a line in its source, not a file.
$licenceNotes = [ordered]@{
    'dr_flac (David Reid)' = 'Public domain (Unlicense) or MIT No Attribution, at your choice (dr_flac.h).'
    'SQLite 3.50.4'        = 'Public domain (https://sqlite.org/copyright.html).'
}

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add('The open-source code in MechCommander Redux, and its licences.')
$lines.Add('')

foreach ($name in $licenceFiles.Keys) {
    $file = Join-Path $LibDir $licenceFiles[$name]

    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "No licence file for ${name}: $file"
    }

    $lines.Add("==== $name ====")
    $lines.Add('')
    $lines.Add((Get-Content -LiteralPath $file -Raw -Encoding utf8).Trim())
    $lines.Add('')
}

foreach ($name in $licenceNotes.Keys) {
    $lines.Add("==== $name ====")
    $lines.Add('')
    $lines.Add($licenceNotes[$name])
    $lines.Add('')
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Out) | Out-Null
# One kind of line ending throughout (the licence files differ), as Notepad shows it.
$text = ($lines -join "`n") -replace "`r`n", "`n" -replace "`n", "`r`n"
[System.IO.File]::WriteAllText($Out, $text, [System.Text.UTF8Encoding]::new($false))
