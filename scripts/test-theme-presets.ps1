[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$GccX86Path)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
New-Item -ItemType Directory -Force "$root\build" | Out-Null
$savedPath = $env:PATH
try {
    $env:PATH = "$(Split-Path -Parent $GccX86Path);$env:PATH"
    & $GccX86Path -std=c99 -Wall -Wextra -Werror -static -D_WIN32_WINNT=0x0501 `
        "$root\tests\theme-core-tests.c" -ladvapi32 -o "$root\build\theme-core-tests.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Theme core tests did not compile.' }
    & "$root\build\theme-core-tests.exe" "$root\payload" "$root\build"
    if ($LASTEXITCODE -ne 0) { throw 'Theme core tests failed.' }
} finally { $env:PATH = $savedPath }
& "$PSScriptRoot\verify-theme-assets.ps1"
