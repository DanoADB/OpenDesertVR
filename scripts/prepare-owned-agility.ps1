# SPDX-License-Identifier: MIT
# Isolated Windows test runtime. No global installation or app packaging.
param([string]$Destination = "$PSScriptRoot/../out/deps/agility-1.619.6")
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path -LiteralPath "$PSScriptRoot/..").ProviderPath
$target = [IO.Path]::GetFullPath($Destination)
if (-not $target.StartsWith($repoRoot + '\out\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Agility test inputs must stay under the repository out directory'
}
New-Item -ItemType Directory -Path $target -Force | Out-Null
$package = Join-Path $target 'microsoft.direct3d.d3d12.1.619.6.nupkg'
if (-not (Test-Path -LiteralPath $package)) {
    Invoke-WebRequest -Uri 'https://api.nuget.org/v3-flatcontainer/microsoft.direct3d.d3d12/1.619.6/microsoft.direct3d.d3d12.1.619.6.nupkg' -OutFile $package
}
if ((Get-FileHash -LiteralPath $package -Algorithm SHA256).Hash.ToLowerInvariant() -ne '08f0489281401aa430fc37322d6c3fc98a8025175aacd714c10d562f4963f1e9') {
    throw 'Preserving mismatched Agility package; hash verification failed'
}
$pins = @{
    'build/native/bin/x64/D3D12Core.dll' = '37fa14281a58cc834076971873006feb8a8d25cddc908d1a345bda1b149ffc7d'
    'build/native/bin/x64/d3d12SDKLayers.dll' = '7ab48d56d1c18708ebcf16c7c9259b2d783e2b935b85add4a244573b9f03cab4'
    'LICENSE.txt' = '5239850894610071566f7ecee0b751fde43c862032d92b99d7d0f596b3433ebd'
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($package)
try {
    foreach ($entryName in $pins.Keys) {
        $entry = $archive.GetEntry($entryName)
        if ($null -eq $entry -or $entry.Length -gt 64MB) { throw 'Invalid bounded Agility package entry' }
        $output = Join-Path $target ([IO.Path]::GetFileName($entryName))
        if (-not (Test-Path -LiteralPath $output)) {
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $output, $false)
        }
        if ((Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash.ToLowerInvariant() -ne $pins[$entryName]) {
            throw 'Preserving mismatched extracted Agility input; hash verification failed'
        }
        if ($entryName.EndsWith('.dll')) {
            $signature = Get-AuthenticodeSignature -LiteralPath $output
            if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') {
                throw 'Microsoft Agility signature verification failed'
            }
        }
    }
} finally { $archive.Dispose() }
Write-Output "Verified isolated test-only Microsoft Agility 1.619.6: $target"
