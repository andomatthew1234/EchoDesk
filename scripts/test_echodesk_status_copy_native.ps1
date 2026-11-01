$ErrorActionPreference = 'Stop'
$projectDir = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $projectDir '.pio\native-tests'
$output = Join-Path $buildDir 'echodesk_status_copy_native.exe'
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$platformIoCompiler = Join-Path $env:USERPROFILE '.platformio\packages\toolchain-gccmingw32\bin\g++.exe'
$compiler = (Get-Command g++ -ErrorAction SilentlyContinue).Source
if (-not $compiler -and (Test-Path -LiteralPath $platformIoCompiler -PathType Leaf)) {
    $compiler = $platformIoCompiler
    $env:PATH = "$(Split-Path -Parent $compiler);$env:PATH"
}
if (-not $compiler) { throw 'No host C++ compiler found' }

& $compiler -std=c++17 -Wall -Wextra -Werror `
    -DMUSIC_DATA_NO_JSON -DPERFORMANCE_DATA_NO_JSON `
    (Join-Path $projectDir 'tests\echodesk_status_copy_native.cpp') `
    (Join-Path $projectDir 'src\echodesk_status_copy.cpp') `
    -o $output
if ($LASTEXITCODE -ne 0) { throw 'echodesk_status_copy_native compilation failed' }
& $output
if ($LASTEXITCODE -ne 0) { throw 'echodesk_status_copy_native failed' }
