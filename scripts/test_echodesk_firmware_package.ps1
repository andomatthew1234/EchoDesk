$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$packager = Join-Path $PSScriptRoot 'package_echodesk_firmware.ps1'
$manifest = Join-Path $repositoryRoot 'desktop\firmware\firmware-manifest.json'
$firmware = Join-Path $repositoryRoot 'dist\ai-monitor-freenove-fnk0114a-2p4.bin'
$scratch = Join-Path ([IO.Path]::GetTempPath()) ("echodesk-firmware-test-" + [guid]::NewGuid().ToString('N'))

function Write-ManifestDigest([string]$Path) {
    $hash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    Set-Content -LiteralPath "$Path.sha256" -Value "$hash  firmware-manifest.json" -Encoding ascii
}

function Expect-Failure([string]$Name, [scriptblock]$Action) {
    try {
        & $Action
        throw "Negative test '$Name' unexpectedly succeeded."
    } catch {
        if ($_.Exception.Message -like "Negative test '$Name' unexpectedly succeeded.*") { throw }
        Write-Output "PASS: $Name rejected"
    }
}

try {
    New-Item -ItemType Directory -Path $scratch -Force | Out-Null
    & $packager -ManifestPath $manifest -SourceFirmware $firmware -ValidationOnly

    $packageRoot = Join-Path $scratch 'package'
    & $packager -ManifestPath $manifest -SourceFirmware $firmware -DestinationRoot $packageRoot
    $release = (Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json).releases[0]
    $packagedFirmware = Join-Path $packageRoot ($release.imageFile -replace '/', [IO.Path]::DirectorySeparatorChar)
    if (-not (Test-Path -LiteralPath $packagedFirmware -PathType Leaf)) { throw 'Packager did not copy the firmware image.' }
    if ((Get-FileHash -LiteralPath $packagedFirmware -Algorithm SHA256).Hash -ne $release.sha256) { throw 'Packaged image hash changed.' }
    Write-Output 'PASS: valid package copied and verified'

    $tamperedFirmware = Join-Path $scratch 'tampered.bin'
    Copy-Item -LiteralPath $firmware -Destination $tamperedFirmware
    $bytes = [IO.File]::ReadAllBytes($tamperedFirmware)
    $bytes[0x1000] = 0
    [IO.File]::WriteAllBytes($tamperedFirmware, $bytes)
    Expect-Failure 'tampered firmware' { & $packager -ManifestPath $manifest -SourceFirmware $tamperedFirmware -ValidationOnly }

    $unsafeManifest = Join-Path $scratch 'firmware-manifest.json'
    $unsafe = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
    $unsafe.releases[0].imageFile = '../outside.bin'
    $unsafe | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $unsafeManifest -Encoding utf8
    Write-ManifestDigest $unsafeManifest
    Expect-Failure 'path traversal' { & $packager -ManifestPath $unsafeManifest -SourceFirmware $firmware -ValidationOnly }

    Copy-Item -LiteralPath $manifest -Destination $unsafeManifest -Force
    Set-Content -LiteralPath "$unsafeManifest.sha256" -Value "$('0' * 64)  firmware-manifest.json" -Encoding ascii
    Expect-Failure 'manifest digest mismatch' { & $packager -ManifestPath $unsafeManifest -SourceFirmware $firmware -ValidationOnly }

    Write-Output 'EchoDesk firmware packaging tests passed.'
} finally {
    if (Test-Path -LiteralPath $scratch) { Remove-Item -LiteralPath $scratch -Recurse -Force }
}
