$ErrorActionPreference = 'Stop'
$source = Split-Path -Parent $MyInvocation.MyCommand.Path
$target = Join-Path $env:LOCALAPPDATA 'Programs\EchoDesk Desktop'
New-Item -ItemType Directory -Path $target -Force | Out-Null
Get-ChildItem -LiteralPath $source | Where-Object Name -NotLike '*.zip' | Copy-Item -Destination $target -Recurse -Force
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut((Join-Path ([Environment]::GetFolderPath('StartMenu')) 'Programs\EchoDesk Desktop.lnk'))
$shortcut.TargetPath = Join-Path $target 'EchoDesk Desktop.exe'
$shortcut.WorkingDirectory = $target
$shortcut.Description = 'Control and customise EchoDesk'
$shortcut.Save()
Write-Output "EchoDesk Desktop installed for the current user at $target"
