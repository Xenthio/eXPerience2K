[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$GccX86Path,
      [Parameter(Mandatory=$true)][string]$GccX64Path)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
foreach ($arch in @('32', '64')) {
    $compiler = if ($arch -eq '32') { $GccX86Path } else { $GccX64Path }
    $savedPath = $env:PATH
    try {
        $env:PATH = "$(Split-Path -Parent $compiler);$env:PATH"
        $output = Join-Path $root "build\taskbar-tests$arch.exe"
        & $compiler -std=c99 -Wall -Wextra -Werror -static -D_WIN32_WINNT=0x0501 -D_WIN32_IE=0x0600 `
            (Join-Path $root 'tests\taskbar-tests.c') -o $output -lcomctl32 -luxtheme -lgdi32
        if ($LASTEXITCODE -ne 0) { throw "Taskbar $arch test build failed." }
        & $output
        if ($LASTEXITCODE -ne 0) { throw "Taskbar $arch tests failed." }
    } finally { $env:PATH = $savedPath }
}
