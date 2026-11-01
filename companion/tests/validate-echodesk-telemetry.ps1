$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$mainPath = Join-Path $repo 'companion\Sources\main.swift'
$telemetryPath = Join-Path $repo 'companion\Sources\EchoDeskTelemetry.swift'
$codexPath = Join-Path $repo 'companion\Sources\CodexBarSource.swift'
$buildPath = Join-Path $repo 'companion\build.sh'
$plistPath = Join-Path $repo 'companion\Resources\EchoDesk-Info.plist'
$entitlementsPath = Join-Path $repo 'companion\Resources\EchoDesk.entitlements'

function Assert-Contains([string] $Text, [string] $Needle, [string] $Message) {
    if (-not $Text.Contains($Needle)) { throw $Message }
}

if (-not (Test-Path -LiteralPath $telemetryPath)) {
    throw 'macOS Music and Performance telemetry producer is missing.'
}

$main = Get-Content -LiteralPath $mainPath -Raw
$telemetry = Get-Content -LiteralPath $telemetryPath -Raw
if ($telemetry.Contains('HOST_VM_INFO64_COUNT')) {
    throw 'Telemetry must not use the unavailable HOST_VM_INFO64_COUNT Swift macro.'
}
$codex = Get-Content -LiteralPath $codexPath -Raw
$build = Get-Content -LiteralPath $buildPath -Raw
$plist = Get-Content -LiteralPath $plistPath -Raw
$entitlements = Get-Content -LiteralPath $entitlementsPath -Raw

foreach ($needle in @(
    'devicePerformanceProtocol', 'deviceMusicProtocol', 'deviceEchoDeskUpdateProtocol',
    'EchoDeskTelemetry', 'sendTelemetryFrame', 'prepareForFirmwareUpdate'
)) {
    Assert-Contains $main $needle "macOS serial integration is missing '$needle'."
}
Assert-Contains $plist 'NSAppleEventsUsageDescription' 'Now Playing is missing its macOS Automation permission description.'
Assert-Contains $entitlements 'com.apple.security.automation.apple-events' 'Now Playing is missing its Hardened Runtime Apple Events entitlement.'

foreach ($needle in @(
    'host_statistics64', 'HOST_CPU_LOAD_INFO', 'getifaddrs',
    'type": "performance', 'type": "music',
    'com.apple.Music', 'com.spotify.client', 'NSAppleScript',
    'canPlayPause', 'ramUsedBytes', 'downBps'
)) {
    Assert-Contains $telemetry $needle "macOS telemetry producer is missing '$needle'."
}

Assert-Contains $codex 'Contents/Helpers/CodexBarCLI' 'Codex lookup does not prefer the bundled helper.'
Assert-Contains $codex '"--source", "oauth"' 'Codex CLI does not force the OAuth source.'
foreach ($needle in @(
    'CODEXBAR_VERSION=', 'CODEXBAR_SHA256_', 'Contents/Helpers/CodexBarCLI',
    'EchoDeskTelemetry.swift', 'codexbar usage --provider codex --json --source oauth'
)) {
    Assert-Contains $build $needle "macOS package contract is missing '$needle'."
}
Assert-Contains $build 'MAIN_ENTITLEMENT_ARGS=(--entitlements "$ECHODESK_ENTITLEMENTS")' 'The EchoDesk signing path does not apply the Automation entitlement.'
Assert-Contains $main '#if ECHODESK_DESKTOP' 'EchoDesk-only telemetry is not compile-time isolated from AI Monitor.'

Write-Host 'EchoDesk macOS telemetry validation passed.'
