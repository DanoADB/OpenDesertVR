param([string]$Destination = "out/packages")
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    foreach ($step in @(
        @('cmake', '--preset', 'windows-msvc-release'),
        @('cmake', '--build', '--preset', 'windows-msvc-release'),
        @('ctest', '--preset', 'windows-msvc-release')
    )) {
        $program = $step[0]
        $arguments = $step[1..($step.Length - 1)]
        & $program @arguments
        if ($LASTEXITCODE -ne 0) { throw "$program failed: $LASTEXITCODE" }
    }
    $packageRoot = [IO.Path]::GetFullPath($Destination)
    $stage = Join-Path $packageRoot ('stage-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    cmake --install out/build/release --config Release --component Runtime --prefix $stage
    if ($LASTEXITCODE -ne 0) { throw 'Runtime install failed' }
    $revision = git rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Cannot determine source revision' }
    $dirty = [bool](git status --porcelain)
    $manifest = @{schema_version=1; source_commit=$revision; source_dirty=$dirty; configuration='Release'; architecture='x64'; headset_validation='NOT_RUN'; game_validation='NOT_RUN'}
    $manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stage 'build-info.json') -Encoding UTF8
    $zip = Join-Path $packageRoot 'OpenDesertVR-M0-win64.zip'
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -Force
    $hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  OpenDesertVR-M0-win64.zip" | Set-Content -LiteralPath ($zip + '.sha256') -Encoding ASCII
    Write-Output $zip
    # Keep stage for inspection; never recursively remove a caller-computed path.
} finally { Pop-Location }
