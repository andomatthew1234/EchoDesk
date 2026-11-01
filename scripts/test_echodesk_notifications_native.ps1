$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$compiler = (Get-Command g++.exe -ErrorAction SilentlyContinue).Source
if (-not $compiler) {
    $compiler = Join-Path $env:USERPROFILE '.platformio\packages\toolchain-gccmingw32\bin\g++.exe'
}
if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) {
    throw 'No native C++ compiler found.'
}
$env:PATH = "$(Split-Path -Parent $compiler);$env:PATH"

$output = Join-Path $projectRoot 'test-results\echodesk_notifications_native.exe'
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $output) | Out-Null
& $compiler -std=c++17 -Wall -Wextra -Werror `
    (Join-Path $projectRoot 'tests\echodesk_notifications_native.cpp') `
    (Join-Path $projectRoot 'src\echodesk_notifications.cpp') `
    -o $output
if ($LASTEXITCODE -ne 0) { throw "Native compile failed with exit code $LASTEXITCODE" }
& $output
if ($LASTEXITCODE -ne 0) { throw "Native test failed with exit code $LASTEXITCODE" }
