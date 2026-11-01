[CmdletBinding()]
param(
    [switch]$FrameworkDependent,
    [string]$HostExecutable,
    [string]$CodexBarCli,
    [string]$NsisCompiler,
    [switch]$RequireInstaller
)

$ErrorActionPreference = 'Stop'
$desktopRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repositoryRoot = Split-Path -Parent $desktopRoot
$companionRoot = Join-Path $repositoryRoot 'companion-windows'
$localDotnet = Join-Path $env:USERPROFILE '.dotnet-sdk\dotnet.exe'
$dotnet = if (Test-Path -LiteralPath $localDotnet) { $localDotnet } else { (Get-Command dotnet -ErrorAction Stop).Source }
$configuration = 'Release'
$packageVersion = '0.2.6'
$expectedCliVersion = '0.70.0'
$expectedCliSha256 = 'F81570B0CD7593ABB9822EB2362F9B9EDA3A382A571457718EE37E664E9A62FC'
$expectedFlashEngineVersion = '4.6.0'
$firmwarePackager = Join-Path $repositoryRoot 'scripts\package_echodesk_firmware.ps1'
$firmwareManifest = Join-Path $desktopRoot 'firmware\firmware-manifest.json'
$firmwareImage = Join-Path $repositoryRoot 'dist\ai-monitor-freenove-fnk0114a-2p4.bin'

# Fail before the expensive application build if the bundled firmware release is
# malformed, targets the wrong ESP32 layout, or differs from its pinned digest.
& $firmwarePackager -ManifestPath $firmwareManifest -SourceFirmware $firmwareImage -ValidationOnly
$cargoLock = Get-Content -LiteralPath (Join-Path $companionRoot 'Cargo.lock') -Raw
if ($cargoLock -notmatch "(?ms)^name = `"espflash`"\r?\nversion = `"$([regex]::Escape($expectedFlashEngineVersion))`"") {
    throw "Cargo.lock does not pin the approved espflash version $expectedFlashEngineVersion."
}

& $dotnet build (Join-Path $desktopRoot 'EchoDesk.Desktop.sln') -c $configuration
if ($LASTEXITCODE -ne 0) { throw 'EchoDesk Desktop build failed.' }
& $dotnet run --project (Join-Path $desktopRoot 'tests\EchoDesk.Desktop.Tests\EchoDesk.Desktop.Tests.csproj') -c $configuration --no-build
if ($LASTEXITCODE -ne 0) { throw 'EchoDesk Desktop tests failed.' }

# Build the sole serial-owning process unless a CI-produced binary was supplied.
if ([string]::IsNullOrWhiteSpace($HostExecutable)) {
    $pnpm = (Get-Command pnpm.cmd -ErrorAction SilentlyContinue).Source
    if (-not $pnpm) { $pnpm = (Get-Command pnpm -ErrorAction Stop).Source }
    $cargo = (Get-Command cargo.exe -ErrorAction SilentlyContinue).Source
    if (-not $cargo) { $cargo = (Get-Command cargo -ErrorAction Stop).Source }
    & $pnpm --dir $companionRoot build
    if ($LASTEXITCODE -ne 0) { throw 'EchoDesk Host frontend build failed.' }
    & $cargo build --manifest-path (Join-Path $companionRoot 'Cargo.toml') -p aimonitor --release
    if ($LASTEXITCODE -ne 0) { throw 'EchoDesk Host build failed.' }
    $HostExecutable = Get-ChildItem -LiteralPath (Join-Path $companionRoot 'target') -Recurse -File -Filter 'aimonitor.exe' |
        Where-Object FullName -Match '[\\/]release[\\/]' |
        Sort-Object LastWriteTimeUtc -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $HostExecutable -or -not (Test-Path -LiteralPath $HostExecutable -PathType Leaf)) {
    throw 'EchoDesk Host executable was not produced. Pass -HostExecutable with a release build.'
}

# Build and self-test the low-privilege performance sidecar.
$helperProject = Join-Path $companionRoot 'tools\echodesk-performance-helper\echodesk-performance-helper.csproj'
$helperOutput = Join-Path $desktopRoot 'obj\performance-helper'
& $dotnet publish $helperProject -c Release -r win-x64 --self-contained true -o $helperOutput
if ($LASTEXITCODE -ne 0) { throw 'Performance helper build failed.' }
$helperExecutable = Join-Path $helperOutput 'echodesk-performance-helper.exe'
& $helperExecutable --self-test
if ($LASTEXITCODE -ne 0) { throw 'Performance helper self-test failed.' }

# A CLI-only provider is bundled; never silently fall back to a separate GUI app.
if ([string]::IsNullOrWhiteSpace($CodexBarCli)) {
    $CodexBarCli = @(
        (Join-Path $desktopRoot 'vendor\win-codexbar\codexbar-cli.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\CodexBar\codexbar-cli.exe')
    ) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
}
if (-not $CodexBarCli -or -not (Test-Path -LiteralPath $CodexBarCli -PathType Leaf)) {
    throw "Win-CodexBar CLI $expectedCliVersion is required for packaging. Pass -CodexBarCli with the verified CLI-only executable."
}
$cliHash = (Get-FileHash -LiteralPath $CodexBarCli -Algorithm SHA256).Hash.ToUpperInvariant()
if ($cliHash -ne $expectedCliSha256) {
    throw "Refusing to package unpinned codexbar-cli.exe. Expected $expectedCliSha256, got $cliHash."
}
$cliVersion = (& $CodexBarCli --version | Select-Object -First 1) -join ''
if ($cliVersion.IndexOf($expectedCliVersion, [StringComparison]::Ordinal) -lt 0) {
    throw "Pinned provider did not report expected version $expectedCliVersion (reported '$cliVersion')."
}

$publish = Join-Path $desktopRoot 'dist\EchoDesk-Desktop-win-x64'
if (Test-Path -LiteralPath $publish) { Remove-Item -LiteralPath $publish -Recurse -Force }
$selfContained = if ($FrameworkDependent) { 'false' } else { 'true' }
& $dotnet publish (Join-Path $desktopRoot 'src\EchoDesk.Desktop\EchoDesk.Desktop.csproj') `
    -c $configuration -r win-x64 --self-contained $selfContained `
    -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true `
    -o $publish
if ($LASTEXITCODE -ne 0) { throw 'EchoDesk Desktop publish failed.' }
Get-ChildItem -LiteralPath $publish -Filter '*.pdb' -File | Remove-Item -Force

$componentDirectory = New-Item -ItemType Directory -Path (Join-Path $publish 'components') -Force
$performanceDirectory = New-Item -ItemType Directory -Path (Join-Path $publish 'performance') -Force
$firmwareDirectory = Join-Path $publish 'firmware'
Copy-Item -LiteralPath $HostExecutable -Destination (Join-Path $publish 'EchoDesk Host.exe') -Force
Copy-Item -LiteralPath $CodexBarCli -Destination (Join-Path $componentDirectory 'codexbar-cli.exe') -Force
Copy-Item -LiteralPath $helperExecutable -Destination (Join-Path $performanceDirectory 'echodesk-performance-helper.exe') -Force
Copy-Item -LiteralPath (Join-Path $companionRoot 'tools\echodesk-performance-helper\THIRD-PARTY-NOTICES.md') -Destination $performanceDirectory -Force
Copy-Item -LiteralPath (Join-Path $desktopRoot 'THIRD-PARTY-NOTICES.md') -Destination $publish -Force
Copy-Item -LiteralPath (Join-Path $desktopRoot 'README.md') -Destination $publish -Force
Copy-Item -LiteralPath (Join-Path $desktopRoot 'install.ps1') -Destination $publish -Force
Copy-Item -LiteralPath (Join-Path $desktopRoot 'uninstall.ps1') -Destination $publish -Force
Copy-Item -LiteralPath (Join-Path $repositoryRoot 'LICENSE') -Destination $publish -Force
& $firmwarePackager -ManifestPath $firmwareManifest -SourceFirmware $firmwareImage -DestinationRoot $firmwareDirectory
$firmwareRelease = (Get-Content -LiteralPath $firmwareManifest -Raw | ConvertFrom-Json).releases[0]
$packagedFirmwarePath = Join-Path $firmwareDirectory ($firmwareRelease.imageFile -replace '/', [IO.Path]::DirectorySeparatorChar)

$manifest = [ordered]@{
    packageVersion = $packageVersion
    hostSha256 = (Get-FileHash -LiteralPath (Join-Path $publish 'EchoDesk Host.exe') -Algorithm SHA256).Hash
    flashEngine = 'espflash-in-process'
    flashEngineVersion = $expectedFlashEngineVersion
    codexBarCliVersion = $expectedCliVersion
    codexBarCliSha256 = $cliHash
    performanceHelperSha256 = (Get-FileHash -LiteralPath (Join-Path $performanceDirectory 'echodesk-performance-helper.exe') -Algorithm SHA256).Hash
    firmwareManifestSha256 = (Get-FileHash -LiteralPath (Join-Path $firmwareDirectory 'firmware-manifest.json') -Algorithm SHA256).Hash
    echoDeskMiniFirmwareVersion = $firmwareRelease.firmwareVersion
    echoDeskMiniFirmwareSha256 = (Get-FileHash -LiteralPath $packagedFirmwarePath -Algorithm SHA256).Hash
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $publish 'component-manifest.json') -Encoding utf8

$archive = Join-Path $desktopRoot "dist\EchoDesk-Desktop-v$packageVersion-win-x64.zip"
if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
Compress-Archive -Path (Join-Path $publish '*') -DestinationPath $archive -CompressionLevel Optimal
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
$archiveChecksum = "$archive.sha256"
Set-Content -LiteralPath $archiveChecksum -Value "$hash  $([IO.Path]::GetFileName($archive))" -Encoding ascii
Write-Output "Package: $archive"
Write-Output "SHA-256: $hash"
Write-Output "Checksum: $archiveChecksum"

# A release build may supply makensis.exe (or install NSIS in its standard
# location) to produce the per-user, no-admin consumer installer. Local
# developer builds can still validate and consume the ZIP without NSIS.
if ([string]::IsNullOrWhiteSpace($NsisCompiler)) {
    $NsisCompiler = @(
        (Get-Command makensis.exe -ErrorAction SilentlyContinue).Source,
        'C:\Program Files (x86)\NSIS\makensis.exe',
        'C:\Program Files\NSIS\makensis.exe'
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } | Select-Object -First 1
}
if ($NsisCompiler) {
    $installerScript = Join-Path $desktopRoot 'installer\EchoDesk-Desktop.nsi'
    & $NsisCompiler "/DPRODUCT_VERSION=$packageVersion" "/DPUBLISH_DIR=$publish" "/DOUTPUT_DIR=$(Join-Path $desktopRoot 'dist')" $installerScript
    if ($LASTEXITCODE -ne 0) { throw 'EchoDesk Desktop NSIS installer build failed.' }
    $installer = Join-Path $desktopRoot "dist\EchoDesk-Desktop-v$packageVersion-win-x64-setup.exe"
    if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) { throw "NSIS did not produce the expected installer: $installer" }
    $installerHash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash
    Set-Content -LiteralPath "$installer.sha256" -Value "$installerHash  $([IO.Path]::GetFileName($installer))" -Encoding ascii
    Write-Output "Installer: $installer"
    Write-Output "SHA-256: $installerHash"
} elseif ($RequireInstaller) {
    throw 'NSIS makensis.exe is required for this release build. Pass -NsisCompiler or install NSIS.'
} else {
    Write-Warning 'NSIS was not found; the validated portable ZIP was produced, but the optional setup EXE was not rebuilt.'
}
