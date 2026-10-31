param(
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $projectRoot "docs\assets\os"
}

$toolchain = Join-Path $env:USERPROFILE ".platformio\packages\toolchain-gccmingw32\bin"
$gcc = Join-Path $toolchain "gcc.exe"
$gxx = Join-Path $toolchain "g++.exe"
$ar = Join-Path $toolchain "ar.exe"
$lvgl = Join-Path $projectRoot ".pio\libdeps\freenove-fnk0114a-2p4\lvgl"
$source = Join-Path $projectRoot "src"
$shim = Join-Path $projectRoot "scripts\site-render"
$build = Join-Path $env:TEMP "echodesk-site-render-v1"

foreach ($required in @($gcc, $gxx, $ar, $lvgl)) {
    if (-not (Test-Path $required)) {
        throw "Missing renderer dependency: $required. Run the Freenove PlatformIO build first."
    }
}

$env:PATH = "$toolchain;$env:PATH"
New-Item -ItemType Directory -Force -Path $build, $OutputDirectory | Out-Null

$commonDefines = @(
    "-DLV_CONF_INCLUDE_SIMPLE=1",
    "-DLV_KCONFIG_IGNORE=1",
    "-DLV_LVGL_H_INCLUDE_SIMPLE=1",
    "-DECHODESK_OS=1",
    "-DMUSIC_DATA_NO_JSON=1"
)
$includes = @("-I$shim", "-I$source", "-I$lvgl", "-I$(Join-Path $lvgl 'src')")
$cFlags = @("-O2", "-std=c99", "-ffunction-sections", "-fdata-sections") + $commonDefines + $includes
$cppFlags = @("-O2", "-std=c++14", "-ffunction-sections", "-fdata-sections") + $commonDefines + $includes

$objects = @()
foreach ($file in Get-ChildItem (Join-Path $lvgl "src") -Filter "*.c" -Recurse) {
    $relative = $file.FullName.Substring($lvgl.Length + 1) -replace "[\\/:]", "_"
    $object = Join-Path $build "$relative.o"
    $objects += $object
    if (-not (Test-Path $object) -or $file.LastWriteTimeUtc -gt (Get-Item $object).LastWriteTimeUtc) {
        & $gcc @cFlags -c $file.FullName -o $object
        if ($LASTEXITCODE -ne 0) { throw "Failed to compile $($file.FullName)" }
    }
}

$library = Join-Path $build "liblvgl.a"
if (-not $library.StartsWith($build, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to replace an archive outside the renderer build directory"
}
Remove-Item -LiteralPath $library -Force -ErrorAction SilentlyContinue
for ($offset = 0; $offset -lt $objects.Count; $offset += 30) {
    $last = [Math]::Min($offset + 29, $objects.Count - 1)
    $batch = $objects[$offset..$last]
    & $ar rcs $library @batch
    if ($LASTEXITCODE -ne 0) { throw "Failed to archive LVGL objects $offset through $last" }
}

$clockObject = Join-Path $build "font_standby_clock_76.o"
& $gcc @cFlags -c (Join-Path $source "font_standby_clock_76.c") -o $clockObject
if ($LASTEXITCODE -ne 0) { throw "Failed to compile the EchoDesk clock font" }

$cppSources = @(
    (Join-Path $shim "render_echodesk.cpp"),
    (Join-Path $source "ui_echodesk.cpp"),
    (Join-Path $source "ui_now_playing.cpp"),
    (Join-Path $source "now_playing_layout.cpp")
)
$cppObjects = @()
foreach ($file in $cppSources) {
    $name = ([IO.Path]::GetFileName($file) -replace "[.]", "_") + ".o"
    $object = Join-Path $build $name
    $cppObjects += $object
    & $gxx @cppFlags -c $file -o $object
    if ($LASTEXITCODE -ne 0) { throw "Failed to compile $file" }
}

$renderer = Join-Path $build "render-echodesk.exe"
& $gxx @cppObjects $clockObject $library "-Wl,--gc-sections" -o $renderer
if ($LASTEXITCODE -ne 0) { throw "Failed to link EchoDesk renderer" }

$ffmpeg = (Get-Command ffmpeg -ErrorAction Stop).Source
foreach ($screen in @("codex", "clock", "now-playing")) {
    $ppm = Join-Path $build "$screen.ppm"
    $png = Join-Path $OutputDirectory "$screen.png"
    & $renderer $screen $ppm
    if ($LASTEXITCODE -ne 0) { throw "Failed to render $screen" }
    & $ffmpeg -hide_banner -loglevel error -y -i $ppm -frames:v 1 $png
    if ($LASTEXITCODE -ne 0) { throw "Failed to encode $png" }
}

Get-ChildItem $OutputDirectory -Filter "*.png" |
    Select-Object Name, Length, LastWriteTime
