[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$theme = Join-Path $root 'payload\Themes\windows-98-nt5'
$assets = @(Import-Csv (Join-Path $theme 'assets.tsv') -Delimiter "`t")
if ($assets.Count -lt 100) { throw 'Legacy theme manifest is missing or incomplete.' }
$seen = @{}
foreach ($entry in $assets) {
    if ($entry.asset -notmatch '^(Resources/eXPerience2K|Sounds/Windows2000)/[a-zA-Z0-9_./-]+$' -or
        $entry.asset.Contains('..') -or $seen.ContainsKey($entry.asset)) {
        throw "Invalid/duplicate theme asset path: $($entry.asset)"
    }
    $seen[$entry.asset] = $true
    $file = Join-Path $theme $entry.asset
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ine $entry.sha256) {
        throw "Theme asset hash mismatch: $($entry.asset)"
    }
    if (-not (Test-Path -LiteralPath (Join-Path "$root\payload" $entry.asset))) {
        throw "Overlay has no base counterpart: $($entry.asset)"
    }
}
$installer = Get-Content (Join-Path $root 'installer\eXPerience2K.nsi') -Raw
if (-not $installer.Contains('Target x86-unicode')) { throw 'Installer must also run on XP x86.' }
Write-Host "Verified $($assets.Count) Windows 98 / NT 5.0 preset assets and provenance hashes."
foreach ($id in @('130', '131', '146', '147')) {
    $entry = @($assets | Where-Object { $_.asset -eq "Resources/eXPerience2K/shell32/$id.bmp" })
    if ($entry.Count -ne 1 -or $entry[0].source -ne 'NT5-1877' -or
        $entry[0].module -ne 'msgina.dll' -or $entry[0].resource -ne 'BITMAP:101') {
        throw "Missing authentic NT 5.0 winver header: $id"
    }
    if ((Get-FileHash (Join-Path $theme $entry[0].asset)).Hash -ne
        (Get-FileHash (Join-Path $theme 'Resources/eXPerience2K/msgina/101.bmp')).Hash) {
        throw "Winver header differs from the original NT workstation artwork: $id"
    }
}

$lowRoot = Join-Path $theme 'LowColor'
$lowAssets = @(Import-Csv (Join-Path $lowRoot 'assets.tsv') -Delimiter "`t")
$seenLow = @{}
foreach ($entry in $lowAssets) {
    if ($entry.asset -notmatch '^Resources/eXPerience2K/[a-zA-Z0-9_./-]+\.ico$' -or
        $entry.asset.Contains('..') -or $entry.source.Contains('..') -or
        $entry.source -notmatch '^(Themes/windows-98-nt5/)?Resources/eXPerience2K/[a-zA-Z0-9_./-]+\.ico$' -or
        $seenLow.ContainsKey($entry.asset)) { throw 'Invalid low-colour manifest path.' }
    $seenLow[$entry.asset] = $true
    $path = Join-Path $lowRoot $entry.asset
    $source = Join-Path "$root\payload" $entry.source
    if ((Get-FileHash $path).Hash -ine $entry.sha256 -or
        (Get-FileHash $source).Hash -ine $entry.source_sha256) { throw "Low-colour hash mismatch: $($entry.asset)" }
    $bytes = [IO.File]::ReadAllBytes($path)
    $original = [IO.File]::ReadAllBytes($source)
    $count = [BitConverter]::ToUInt16($bytes, 4)
    if ($count -lt 1 -or $bytes.Length -lt 6 + 16 * $count) { throw 'Invalid low-colour ICO.' }
    $originalCount = [BitConverter]::ToUInt16($original, 4)
    for ($i = 0; $i -lt $count; $i++) {
        $offset = 6 + 16 * $i
        $depth = [BitConverter]::ToUInt16($bytes, $offset + 6)
        $size = [BitConverter]::ToUInt32($bytes, $offset + 8)
        $start = [BitConverter]::ToUInt32($bytes, $offset + 12)
        if ($depth -notin @(1,4) -or $size -lt 40 -or $start -lt 6 + 16 * $count -or
            ([long]$start + $size) -gt $bytes.Length -or
            [BitConverter]::ToUInt16($bytes, $start + 14) -ne $depth) { throw 'Icon is not genuine low-colour DIB data.' }
        $image = [Convert]::ToBase64String($bytes, $start, $size)
        $found = $false
        for ($j = 0; $j -lt $originalCount; $j++) {
            $originalOffset = 6 + 16 * $j
            $originalSize = [BitConverter]::ToUInt32($original, $originalOffset + 8)
            $originalStart = [BitConverter]::ToUInt32($original, $originalOffset + 12)
            if ($size -eq $originalSize -and
                $image -ceq [Convert]::ToBase64String($original, $originalStart, $originalSize)) { $found = $true; break }
        }
        if (-not $found) { throw 'Low-colour frame differs from its original source bytes.' }
    }
}
foreach ($asset in $assets | Where-Object { $_.asset.EndsWith('.ico') }) {
    if (-not $seenLow.ContainsKey($asset.asset)) { throw "Missing low-colour 98 icon: $($asset.asset)" }
}
if ((Get-ChildItem $lowRoot -Filter '*.ico' -File -Recurse).Count -ne $lowAssets.Count) {
    throw 'Unmanifested low-colour icons are present.'
}
Write-Host "Verified $($lowAssets.Count) low-colour icons, original frame bytes and complete 98 icon coverage."
