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

$output = Join-Path $projectRoot 'test-results\pc_dashboard_native.exe'
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $output) | Out-Null
& $compiler -std=c++17 -Wall -Wextra -Werror -DPERFORMANCE_DATA_NO_JSON=1 `
    (Join-Path $projectRoot 'tests\pc_dashboard_native.cpp') `
    (Join-Path $projectRoot 'src\pc_dashboard.cpp') `
    (Join-Path $projectRoot 'src\performance_data.cpp') `
    -o $output
if ($LASTEXITCODE -ne 0) { throw "Native compile failed with exit code $LASTEXITCODE" }
& $output
if ($LASTEXITCODE -ne 0) { throw "Native test failed with exit code $LASTEXITCODE" }
