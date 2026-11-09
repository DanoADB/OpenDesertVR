# SPDX-License-Identifier: MIT
# Test-only Microsoft runtime. Never install globally or include in app packages.
param([string]$Destination = "$PSScriptRoot/../out/deps/warp-1.0.22")
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path -LiteralPath "$PSScriptRoot/..").ProviderPath
$target = [IO.Path]::GetFullPath($Destination)
if (-not $target.StartsWith($repoRoot + '\out\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'WARP test inputs must stay under the repository out directory'
}
New-Item -ItemType Directory -Path $target -Force | Out-Null
$package = Join-Path $target 'microsoft.direct3d.warp.1.0.22.nupkg'
$packageHash = 'afc058f4d1066edc4231a38f3ab3592b1930f7287d4ac700e6a056e0874bb0be'
if (-not (Test-Path -LiteralPath $package)) {
    Invoke-WebRequest -Uri 'https://api.nuget.org/v3-flatcontainer/microsoft.direct3d.warp/1.0.22/microsoft.direct3d.warp.1.0.22.nupkg' -OutFile $package
}
if ((Get-FileHash -LiteralPath $package -Algorithm SHA256).Hash.ToLowerInvariant() -ne $packageHash) {
    throw 'Preserving mismatched WARP package; hash verification failed'
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($package)
try {
    foreach ($entryName in @('build/native/bin/x64/d3d10warp.dll', 'LICENSE.TXT')) {
        $entry = $archive.GetEntry($entryName)
        if ($null -eq $entry -or $entry.Length -gt 64MB) { throw 'Invalid bounded WARP package entry' }
        $output = Join-Path $target ([IO.Path]::GetFileName($entryName))
        $expected = if ($entryName.EndsWith('.dll')) {
            'bb8ee56fd0f7ee665abb61178301e612181e5e2f57af0e5ab64e456b8f1a8c4f'
        } else { '5435f10305a92870b45735dfc169c5d6010617f6556e0859689a058d2c6b59c4' }
        if (-not (Test-Path -LiteralPath $output)) {
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $output, $false)
        }
        if ((Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
            throw 'Preserving mismatched extracted WARP input; hash verification failed'
        }
    }
} finally { $archive.Dispose() }
$dll = Join-Path $target 'd3d10warp.dll'
$signature = Get-AuthenticodeSignature -LiteralPath $dll
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') {
    throw 'Microsoft WARP signature verification failed'
}
Write-Output "Verified test-only Microsoft WARP 1.0.22: $dll"
