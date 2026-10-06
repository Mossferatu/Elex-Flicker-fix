[CmdletBinding()]
param(
    [string]$Zig = 'zig',
    [switch]$RunTests,
    [string]$OriginalShader,
    [string]$CorrectedShader
)

$ErrorActionPreference = 'Stop'
if ([bool]$OriginalShader -ne [bool]$CorrectedShader) {
    throw 'Supply both -OriginalShader and -CorrectedShader, or neither.'
}
if ($OriginalShader -and -not $RunTests) {
    throw 'Shader inputs require -RunTests.'
}
$compiler = (Get-Command $Zig -ErrorAction Stop).Source
if ($OriginalShader) {
    $OriginalShader = (Resolve-Path -LiteralPath $OriginalShader).Path
    $CorrectedShader = (Resolve-Path -LiteralPath $CorrectedShader).Path
}

Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Path 'build', 'build\package\system', 'dist' -Force | Out-Null
    & $compiler cc -target x86_64-windows-gnu -std=c11 -Wall -Wextra -Werror -O2 -s `
        -shared src\d3d11.c src\patch.c src\d3d11.def -o build\d3d11.dll -lbcrypt -luser32
    if ($LASTEXITCODE -ne 0) { throw "DLL compilation failed: $LASTEXITCODE" }

    if ($RunTests) {
        & $compiler cc -target x86_64-windows-gnu -std=c11 -Wall -Wextra -Werror -O2 -s `
            -Isrc tests\smoke.c src\patch.c -o build\smoke.exe -lbcrypt -luser32
        if ($LASTEXITCODE -ne 0) { throw "Smoke test compilation failed: $LASTEXITCODE" }
        Push-Location 'build'
        try {
            if ($OriginalShader) {
                .\smoke.exe $OriginalShader $CorrectedShader
            } else {
                .\smoke.exe
            }
            if ($LASTEXITCODE -ne 0) { throw "Smoke tests failed: $LASTEXITCODE" }
        } finally {
            Pop-Location
        }
    }

    Copy-Item -LiteralPath 'build\d3d11.dll' -Destination 'build\package\system\d3d11.dll'
    Copy-Item -LiteralPath 'README.md', 'LICENSE' -Destination 'build\package'
    $archive = 'dist\ELEX-Cloud-Flicker-Fix-0.1.0.zip'
    Compress-Archive -LiteralPath 'build\package\system', 'build\package\README.md', `
        'build\package\LICENSE' -DestinationPath $archive -Force
    $dllHash = (Get-FileHash -LiteralPath 'build\d3d11.dll' -Algorithm SHA256).Hash.ToLowerInvariant()
    $zipHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    @(
        "$dllHash  system/d3d11.dll"
        "$zipHash  ELEX-Cloud-Flicker-Fix-0.1.0.zip"
    ) | Set-Content -LiteralPath 'dist\SHA256SUMS.txt' -Encoding ASCII
    Write-Output "Built $archive and dist\SHA256SUMS.txt"
} finally {
    Pop-Location
}
