$ErrorActionPreference = 'Stop'
$projectDir = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $projectDir '.pio\native-tests'
$output = Join-Path $buildDir 'screen_contract_test.exe'
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$platformIoCompiler = Join-Path $env:USERPROFILE '.platformio\packages\toolchain-gccmingw32\bin\g++.exe'
$compiler = (Get-Command g++ -ErrorAction SilentlyContinue).Source
if (-not $compiler -and (Test-Path -LiteralPath $platformIoCompiler -PathType Leaf)) {
    $compiler = $platformIoCompiler
    $env:PATH = "$(Split-Path -Parent $compiler);$env:PATH"
}
if (-not $compiler) { throw 'No host C++ compiler found' }

& $compiler -std=c++17 -Wall -Wextra -Werror `
    (Join-Path $projectDir 'tests\echodesk_update_native.cpp') `
    (Join-Path $projectDir 'src\echodesk_update.cpp') `
    -o $output
if ($LASTEXITCODE -ne 0) { throw 'echodesk_update_native compilation failed' }
& $output
if ($LASTEXITCODE -ne 0) { throw 'echodesk_update_native failed' }
