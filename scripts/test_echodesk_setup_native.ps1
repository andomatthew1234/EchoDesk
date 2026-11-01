$ErrorActionPreference = 'Stop'
$projectDir = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $projectDir '.pio\native-tests'
$output = Join-Path $buildDir 'testx.exe'
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
$platformIoCompiler = Join-Path $env:USERPROFILE '.platformio\packages\toolchain-gccmingw32\bin\g++.exe'
$compiler = (Get-Command g++ -ErrorAction SilentlyContinue).Source
if (-not $compiler -and (Test-Path -LiteralPath $platformIoCompiler -PathType Leaf)) {
    $compiler = $platformIoCompiler
    $env:PATH = "$(Split-Path -Parent $compiler);$env:PATH"
}
if (-not $compiler) { throw 'No host C++ compiler found' }
& $compiler -std=c++17 -Wall -Wextra -Werror `
    "-I$(Join-Path $projectDir 'src')" `
    (Join-Path $projectDir 'src\echodesk_setup.cpp') `
    (Join-Path $projectDir 'tests\echodesk_setup_native.cpp') `
    -o $output
if ($LASTEXITCODE -ne 0) { throw 'echodesk_setup_native compilation failed' }
& $output
if ($LASTEXITCODE -ne 0) { throw 'echodesk_setup_native failed' }
