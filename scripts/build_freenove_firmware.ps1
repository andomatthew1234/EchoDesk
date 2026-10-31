$ErrorActionPreference = "Stop"

$projectDir = Split-Path -Parent $PSScriptRoot
$environmentName = "freenove-fnk0114a-2p4"
$buildDir = Join-Path $projectDir ".pio\build\$environmentName"
$outputDir = Join-Path $projectDir "dist"
$outputBin = Join-Path $outputDir "ai-monitor-freenove-fnk0114a-2p4.bin"
$checksumFile = "$outputBin.sha256"
$platformioHome = Join-Path $env:USERPROFILE ".platformio"
$esptool = Join-Path $platformioHome "packages\tool-esptoolpy\esptool.py"
$bootApp = Join-Path $platformioHome "packages\framework-arduinoespressif32\tools\partitions\boot_app0.bin"
$python = (Get-Command py.exe -ErrorAction SilentlyContinue).Source
if (-not $python) { $python = (Get-Command python.exe -ErrorAction Stop).Source }

Push-Location $projectDir
try {
    & $python -m platformio run -e $environmentName
    if ($LASTEXITCODE -ne 0) {
        throw "PlatformIO build failed with exit code $LASTEXITCODE"
    }

    $requiredFiles = @(
        (Join-Path $buildDir "bootloader.bin"),
        (Join-Path $buildDir "partitions.bin"),
        (Join-Path $buildDir "firmware.bin"),
        $bootApp,
        $esptool
    )
    foreach ($file in $requiredFiles) {
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
            throw "Required build file is missing: $file"
        }
    }

    New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
    & $python $esptool --chip esp32 merge_bin `
        -o $outputBin `
        --flash_mode dio `
        --flash_freq 40m `
        --flash_size 4MB `
        0x1000 (Join-Path $buildDir "bootloader.bin") `
        0x8000 (Join-Path $buildDir "partitions.bin") `
        0xe000 $bootApp `
        0x10000 (Join-Path $buildDir "firmware.bin")
    if ($LASTEXITCODE -ne 0) {
        throw "Firmware merge failed with exit code $LASTEXITCODE"
    }

    $artifact = Get-Item -LiteralPath $outputBin
    $hash = Get-FileHash -LiteralPath $outputBin -Algorithm SHA256
    $checksumTemp = "$checksumFile.tmp"
    Set-Content -LiteralPath $checksumTemp `
        -Value "$($hash.Hash)  $($artifact.Name)" `
        -Encoding ascii
    Move-Item -LiteralPath $checksumTemp -Destination $checksumFile -Force
    Write-Host "Firmware: $($artifact.FullName)"
    Write-Host "Size:     $($artifact.Length) bytes"
    Write-Host "SHA-256:  $($hash.Hash)"
    Write-Host "Checksum: $checksumFile"
}
finally {
    Pop-Location
}
