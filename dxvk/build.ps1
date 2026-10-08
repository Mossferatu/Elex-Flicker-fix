[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ToolchainDirectory,
    [Parameter(Mandatory = $true)]
    [string]$GlslangDirectory,
    [string]$SourceDirectory,
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?$')]
    [string]$Version = '0.1.0'
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$commit = 'b1a1c99ab52b687cf950d62c88bc2fa316b41663'
$ToolchainDirectory = (Resolve-Path -LiteralPath $ToolchainDirectory).Path
$GlslangDirectory = (Resolve-Path -LiteralPath $GlslangDirectory).Path
if (-not $SourceDirectory) {
    $SourceDirectory = Join-Path $repo 'build\dxvk-source'
}
$SourceDirectory = [IO.Path]::GetFullPath($SourceDirectory)
$oldPath, $oldCC, $oldCXX = $env:PATH, $env:CC, $env:CXX

function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    $savedAction = $ErrorActionPreference
    try {
        # Windows PowerShell treats redirected native stderr as PowerShell errors.
        $ErrorActionPreference = 'Continue'
        & $Program @Arguments
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedAction
    }
    if ($code -ne 0) {
        throw "$Program failed with exit code $code"
    }
}

try {
    $env:PATH = "$ToolchainDirectory\bin;$GlslangDirectory\bin;$env:PATH"
    $env:CC = 'clang'
    $env:CXX = 'clang++'
    foreach ($tool in 'git', 'meson', 'ninja', 'clang', 'clang++', 'windres', 'glslang') {
        $null = Get-Command $tool -ErrorAction Stop
    }
    if (-not (Test-Path -LiteralPath $SourceDirectory)) {
        New-Item -ItemType Directory -Path (Split-Path $SourceDirectory -Parent) -Force | Out-Null
        Invoke-Checked git @('clone', '--branch', 'v3.1.1', '--depth', '1',
            'https://github.com/doitsujin/dxvk.git', $SourceDirectory)
    }
    $head = & git -C $SourceDirectory rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $head -ne $commit) {
        throw "DXVK checkout must be pinned to $commit"
    }
    Invoke-Checked git @('-C', $SourceDirectory, 'submodule', 'update', '--init', '--recursive', '--depth', '1')
    $patch = Join-Path $PSScriptRoot 'elex-cloud-barrier.patch'
    $savedAction = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $reverseOutput = & git -C $SourceDirectory apply --reverse --check $patch 2>&1
        $alreadyPatched = $LASTEXITCODE -eq 0
    } finally {
        $ErrorActionPreference = $savedAction
    }
    if (-not $alreadyPatched) {
        Invoke-Checked git @('-C', $SourceDirectory, 'apply', '--check', $patch)
        Invoke-Checked git @('-C', $SourceDirectory, 'apply', $patch)
    }

    $build = Join-Path $SourceDirectory 'build-elex'
    if (-not (Test-Path -LiteralPath (Join-Path $build 'build.ninja'))) {
        Invoke-Checked meson @('setup', $build, $SourceDirectory, '--buildtype=release',
            '-Denable_d3d8=false', '-Denable_d3d9=false')
    }
    Invoke-Checked meson @('compile', '-C', $build, '-j', '6')

    # A fresh staging directory prevents private test files or stale DLLs entering a release.
    $stage = Join-Path $repo ('build\dxvk-package-' + [guid]::NewGuid().ToString('N'))
    $dist = Join-Path $repo 'dist'
    New-Item -ItemType Directory -Path "$stage\system", "$stage\licenses", $dist -Force | Out-Null
    $hashes = @()
    foreach ($name in 'd3d11', 'dxgi') {
        $dll = Join-Path $build "src\$name\$name.dll"
        $bytes = [IO.File]::ReadAllBytes($dll)
        if ($bytes.Length -lt 64 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) {
            throw "Invalid DLL: $dll"
        }
        $pe = [BitConverter]::ToInt32($bytes, 60)
        if ($pe -lt 0 -or $pe -gt $bytes.Length - 6 -or
            [BitConverter]::ToUInt32($bytes, $pe) -ne 0x4550 -or
            [BitConverter]::ToUInt16($bytes, $pe + 4) -ne 0x8664) {
            throw "Expected x64 PE DLL: $dll"
        }
        Copy-Item -LiteralPath $dll -Destination "$stage\system\$name.dll"
        $hashes += "$((Get-FileHash $dll -Algorithm SHA256).Hash.ToLowerInvariant())  system/$name.dll"
    }
    Copy-Item -LiteralPath "$PSScriptRoot\README.md", $patch -Destination $stage
    Copy-Item -LiteralPath "$PSScriptRoot\dxvk.conf" -Destination "$stage\system"
    $notices = @{
        'DXVK.txt' = "$SourceDirectory\LICENSE"
        'dxbc-spirv.txt' = "$SourceDirectory\subprojects\dxbc-spirv\LICENSE"
        'libdisplay-info.txt' = "$SourceDirectory\subprojects\libdisplay-info\LICENSE"
        'Vulkan-Headers.md' = "$SourceDirectory\include\vulkan\LICENSE.md"
        'Vulkan-Headers-Apache-2.0.txt' = "$SourceDirectory\include\vulkan\LICENSES\Apache-2.0.txt"
        'Vulkan-Headers-MIT.txt' = "$SourceDirectory\include\vulkan\LICENSES\MIT.txt"
        'SPIRV-Headers.txt' = "$SourceDirectory\include\spirv\LICENSE"
        'LLVM.txt' = "$ToolchainDirectory\LICENSE.TXT"
        'ELEX-patch-MIT.txt' = "$repo\LICENSE"
    }
    foreach ($name in $notices.Keys) {
        Copy-Item -LiteralPath $notices[$name] -Destination (Join-Path "$stage\licenses" $name)
    }
    $hashes | Set-Content -LiteralPath "$stage\SHA256SUMS.txt" -Encoding ASCII
    $archiveName = "ELEX-Cloud-Flicker-Fix-DXVK-$Version.zip"
    $archive = Join-Path $dist $archiveName
    Compress-Archive -LiteralPath "$stage\system", "$stage\licenses", "$stage\README.md",
        "$stage\elex-cloud-barrier.patch", "$stage\SHA256SUMS.txt" -DestinationPath $archive -Force
    $zipHash = (Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    @($hashes; "$zipHash  $archiveName") |
        Set-Content -LiteralPath "$dist\DXVK-SHA256SUMS.txt" -Encoding ASCII
    Write-Output "Built $archive and dist\DXVK-SHA256SUMS.txt"
} finally {
    $env:PATH, $env:CC, $env:CXX = $oldPath, $oldCC, $oldCXX
}
