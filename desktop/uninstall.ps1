$ErrorActionPreference = 'Stop'
$target = Join-Path $env:LOCALAPPDATA 'Programs\EchoDesk Desktop'
$shortcut = Join-Path ([Environment]::GetFolderPath('StartMenu')) 'Programs\EchoDesk Desktop.lnk'
Remove-Item -LiteralPath $shortcut -Force -ErrorAction SilentlyContinue
Remove-ItemProperty -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name 'EchoDesk Desktop' -ErrorAction SilentlyContinue
if (Test-Path -LiteralPath $target) {
    Write-Output "Close EchoDesk Desktop, then remove: $target"
}
