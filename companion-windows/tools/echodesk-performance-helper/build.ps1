[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$project = Join-Path $PSScriptRoot 'echodesk-performance-helper.csproj'

if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
    throw '.NET 8 SDK not found. Install the SDK from https://dotnet.microsoft.com/download/dotnet/8.0'
}

$sdks = dotnet --list-sdks
if (-not ($sdks -match '^8\.')) {
    throw '.NET 8 SDK not found. A runtime alone cannot build the helper.'
}

dotnet publish $project -c Release -r win-x64 --self-contained true
if ($LASTEXITCODE -ne 0) {
    throw "dotnet publish failed with exit code $LASTEXITCODE"
}

$output = Join-Path $PSScriptRoot 'bin\Release\net8.0\win-x64\publish\echodesk-performance-helper.exe'
if (-not (Test-Path -LiteralPath $output -PathType Leaf)) {
    throw "Published helper missing: $output"
}

& $output --self-test
if ($LASTEXITCODE -ne 0) {
    throw "Performance helper self-test failed with exit code $LASTEXITCODE"
}

Write-Host "Published: $output"
