[CmdletBinding()]
param(
    [string]$ManifestPath,
    [string]$SourceFirmware,
    [string]$DestinationRoot,
    [switch]$ValidationOnly
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
    $ManifestPath = Join-Path $repositoryRoot 'desktop\firmware\firmware-manifest.json'
}
if ([string]::IsNullOrWhiteSpace($SourceFirmware)) {
    $SourceFirmware = Join-Path $repositoryRoot 'dist\ai-monitor-freenove-fnk0114a-2p4.bin'
}

function Assert-That([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Read-RequiredText($Object, [string]$Name) {
    $property = $Object.PSObject.Properties[$Name]
    Assert-That ($null -ne $property) "Firmware manifest is missing '$Name'."
    $value = [string]$property.Value
    Assert-That (-not [string]::IsNullOrWhiteSpace($value)) "Firmware manifest field '$Name' is empty."
    return $value
}

function Assert-SemVer([string]$Value, [string]$Name) {
    Assert-That ($Value -match '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-[0-9A-Za-z.-]+)?$') "Firmware manifest field '$Name' is not a supported semantic version."
}

function Assert-MergedEsp32Image([byte[]]$Image) {
    Assert-That ($Image.Length -ge 0x11000 -and $Image.Length -le 0x400000) 'Firmware image is outside the supported EchoDesk Mini flash size.'
    Assert-That ($Image[0x1000] -eq 0xE9) 'Firmware image has no ESP32 bootloader at offset 0x1000.'
    $chipId = [uint16]$Image[0x100C] -bor ([uint16]$Image[0x100D] -shl 8)
    Assert-That ($chipId -eq 0) 'Firmware bootloader does not target the original ESP32 chip family.'
    Assert-That ($Image[0x8000] -eq 0xAA -and $Image[0x8001] -eq 0x50) 'Firmware image has no valid partition table at offset 0x8000.'
    Assert-That ($Image[0x10000] -eq 0xE9) 'Firmware image has no ESP32 application at offset 0x10000.'
}

Assert-That (Test-Path -LiteralPath $ManifestPath -PathType Leaf) "Firmware manifest does not exist: $ManifestPath"
Assert-That (Test-Path -LiteralPath $SourceFirmware -PathType Leaf) "Firmware image does not exist: $SourceFirmware"

$manifestFile = Get-Item -LiteralPath $ManifestPath
$checksumPath = "$ManifestPath.sha256"
Assert-That (Test-Path -LiteralPath $checksumPath -PathType Leaf) "Signed-package manifest digest is missing: $checksumPath"
$checksumLine = (Get-Content -LiteralPath $checksumPath -Raw).Trim()
Assert-That ($checksumLine -match '^([0-9A-Fa-f]{64})\s{2}firmware-manifest\.json$') 'Firmware manifest digest file has an invalid format.'
$expectedManifestHash = $Matches[1].ToUpperInvariant()
$actualManifestHash = (Get-FileHash -LiteralPath $ManifestPath -Algorithm SHA256).Hash.ToUpperInvariant()
Assert-That ($actualManifestHash -eq $expectedManifestHash) 'Firmware manifest digest does not match; refusing to package it.'

try {
    $manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
} catch {
    throw "Firmware manifest is not valid JSON: $($_.Exception.Message)"
}
Assert-That ($manifest.schemaVersion -eq 1) 'Only firmware manifest schema version 1 is supported.'
Assert-That ($manifest.releases -is [System.Array] -and $manifest.releases.Count -eq 1) 'The bundled manifest must contain exactly one EchoDesk Mini release.'
$release = $manifest.releases[0]

$version = Read-RequiredText $release 'firmwareVersion'
$minimumDesktopVersion = Read-RequiredText $release 'minimumDesktopVersion'
Assert-SemVer $version 'firmwareVersion'
Assert-SemVer $minimumDesktopVersion 'minimumDesktopVersion'
Assert-That ((Read-RequiredText $release 'hardwareModel') -eq 'EchoDesk Mini') 'Bundled firmware must identify EchoDesk Mini.'
Assert-That ((Read-RequiredText $release 'hardwareId') -eq 'freenove-fnk0114a-2p4') 'Bundled firmware has an unsupported hardware identifier.'
Assert-That ((Read-RequiredText $release 'supportedChip') -eq 'esp32') 'Bundled firmware must target the original ESP32.'
Assert-That ((Read-RequiredText $release 'imageType') -eq 'merged') 'Bundled firmware must be a merged image.'
Assert-That ([uint64]$release.flashAddress -eq 0) 'Bundled firmware must be flashed at address 0x0.'
Assert-That ((Read-RequiredText $release 'flashMode') -eq 'dio') 'Bundled firmware must use DIO flash mode.'
Assert-That ((Read-RequiredText $release 'flashFrequency') -eq '40m') 'Bundled firmware must use a 40 MHz flash frequency.'
Assert-That ((Read-RequiredText $release 'flashSize') -eq '4MB') 'Bundled firmware must target a 4 MB flash device.'
Assert-That (-not [string]::IsNullOrWhiteSpace((Read-RequiredText $release 'releaseNotes'))) 'Bundled firmware release notes must not be empty.'

$relativeImagePath = Read-RequiredText $release 'imageFile'
Assert-That ($relativeImagePath -match '^[A-Za-z0-9][A-Za-z0-9._/-]*$') 'Firmware imageFile contains unsupported characters.'
Assert-That (-not [IO.Path]::IsPathRooted($relativeImagePath) -and $relativeImagePath -notmatch '(^|/)\.\.(/|$)') 'Firmware imageFile must be a safe relative path.'
$expectedRelativeImagePath = "echodesk-mini/$version/echodesk-os.bin"
Assert-That ($relativeImagePath -eq $expectedRelativeImagePath) 'Firmware imageFile does not match its hardware and version.'

$downloadProperty = $release.PSObject.Properties['downloadUrl']
Assert-That ($null -ne $downloadProperty) "Firmware manifest is missing 'downloadUrl'."
if ($null -ne $downloadProperty.Value) {
    $downloadUrl = [uri]([string]$downloadProperty.Value)
    Assert-That ($downloadUrl.IsAbsoluteUri -and $downloadUrl.Scheme -eq 'https') 'Remote firmware URLs must use HTTPS.'
}

$expectedHash = Read-RequiredText $release 'sha256'
Assert-That ($expectedHash -match '^[0-9A-Fa-f]{64}$') 'Firmware SHA-256 has an invalid format.'
$expectedHash = $expectedHash.ToUpperInvariant()
$firmware = Get-Item -LiteralPath $SourceFirmware
Assert-That ([uint64]$release.size -eq [uint64]$firmware.Length) 'Firmware size does not match the manifest.'
$actualHash = (Get-FileHash -LiteralPath $SourceFirmware -Algorithm SHA256).Hash.ToUpperInvariant()
Assert-That ($actualHash -eq $expectedHash) 'Firmware SHA-256 does not match the manifest.'
$image = [IO.File]::ReadAllBytes($firmware.FullName)
Assert-MergedEsp32Image $image

$configPath = Join-Path $repositoryRoot 'src\config.h'
if (Test-Path -LiteralPath $configPath -PathType Leaf) {
    $versionLine = Select-String -LiteralPath $configPath -Pattern '^\s*#define\s+APP_VERSION\s+"([^"]+)"\s*$' | Select-Object -First 1
    Assert-That ($null -ne $versionLine) 'src/config.h does not define APP_VERSION.'
    Assert-That ($versionLine.Matches[0].Groups[1].Value -eq $version) 'Firmware manifest version does not match src/config.h APP_VERSION.'
}

if (-not $ValidationOnly) {
    Assert-That (-not [string]::IsNullOrWhiteSpace($DestinationRoot)) 'DestinationRoot is required unless ValidationOnly is used.'
    $destinationFull = [IO.Path]::GetFullPath($DestinationRoot)
    $destinationImage = [IO.Path]::GetFullPath((Join-Path $destinationFull ($relativeImagePath -replace '/', [IO.Path]::DirectorySeparatorChar)))
    $destinationPrefix = $destinationFull.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    Assert-That ($destinationImage.StartsWith($destinationPrefix, [StringComparison]::OrdinalIgnoreCase)) 'Firmware image destination escapes the package firmware directory.'

    New-Item -ItemType Directory -Path $destinationFull -Force | Out-Null
    New-Item -ItemType Directory -Path (Split-Path -Parent $destinationImage) -Force | Out-Null
    Copy-Item -LiteralPath $manifestFile.FullName -Destination (Join-Path $destinationFull 'firmware-manifest.json') -Force
    Copy-Item -LiteralPath $checksumPath -Destination (Join-Path $destinationFull 'firmware-manifest.json.sha256') -Force
    Copy-Item -LiteralPath $firmware.FullName -Destination $destinationImage -Force
    $copiedHash = (Get-FileHash -LiteralPath $destinationImage -Algorithm SHA256).Hash.ToUpperInvariant()
    Assert-That ($copiedHash -eq $expectedHash) 'Packaged firmware changed during copying.'
}

Write-Output "EchoDesk Mini firmware $version validated ($($firmware.Length) bytes, SHA-256 $actualHash)."
