$ErrorActionPreference = 'Stop'

$projectDir = Split-Path -Parent $PSScriptRoot
$platformIoCompiler = Join-Path $env:USERPROFILE '.platformio\packages\toolchain-gccmingw32\bin\g++.exe'
$compiler = (Get-Command g++ -ErrorAction SilentlyContinue).Source
if (-not $compiler -and (Test-Path -LiteralPath $platformIoCompiler -PathType Leaf)) {
    $compiler = $platformIoCompiler
    $env:PATH = "$(Split-Path -Parent $compiler);$env:PATH"
}
if (-not $compiler) {
    throw 'No host C++ compiler found. Install g++, or run: py -m platformio pkg install --global --tool platformio/toolchain-gccmingw32'
}

$outputDir = Join-Path $projectDir '.pio\native-tests'
$output = Join-Path $outputDir 'performance_data_native.exe'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

& $compiler -std=c++11 -Wall -Wextra -Werror `
    '-DPERFORMANCE_DATA_NO_JSON=1' `
    (Join-Path $projectDir 'tests\performance_data_native.cpp') `
    (Join-Path $projectDir 'src\performance_data.cpp') `
    "-I$(Join-Path $projectDir 'src')" `
    "-o$output"
if ($LASTEXITCODE -ne 0) { throw "Native test compile failed with exit code $LASTEXITCODE" }

& $output
if ($LASTEXITCODE -ne 0) { throw "Native tests failed with exit code $LASTEXITCODE" }
