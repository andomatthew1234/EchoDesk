param()

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path

function Require-SourcePattern {
    param(
        [Parameter(Mandatory)] [string] $Path,
        [Parameter(Mandatory)] [string] $Pattern,
        [Parameter(Mandatory)] [string] $Message
    )

    $source = Get-Content -LiteralPath (Join-Path $repoRoot $Path) -Raw
    if ($source -notmatch $Pattern) {
        throw $Message
    }
}

Require-SourcePattern `
    -Path 'companion-windows\src-tauri\src\performance.rs' `
    -Pattern 'creation_flags\(performance_helper_creation_flags\(\)\)' `
    -Message 'Performance helper is not protected by CREATE_NO_WINDOW.'

Require-SourcePattern `
    -Path 'companion-windows\src-tauri\src\firmware_manager.rs' `
    -Pattern 'request_device\(&app, "prepare_update", json!\(\{\}\)\)[\s\S]{0,500}pause_serial\(&app\)' `
    -Message 'The acknowledged update screen is not requested before the serial service enters flashing mode.'

Require-SourcePattern `
    -Path 'companion-windows\src-tauri\src\firmware_manager.rs' `
    -Pattern 'if !verified[\s\S]+request_device\(&app, "update_complete", json!\(\{\}\)\)' `
    -Message 'The non-fatal update-complete presentation is not sent after reconnect verification.'

Require-SourcePattern `
    -Path 'companion-windows\src-tauri\src\desktop_broker.rs' `
    -Pattern 'Provider diagnostic output was withheld because it was not safe to display' `
    -Message 'Codex diagnostics no longer have a safe fallback for untrusted stderr.'

Require-SourcePattern `
    -Path 'companion-windows\src-tauri\src\serial_service.rs' `
    -Pattern '"myDevice"' `
    -Message 'The Windows host does not allow the My device firmware app.'

Require-SourcePattern `
    -Path 'desktop\src\EchoDesk.Desktop.Core\Models\DeviceState.cs' `
    -Pattern '\("myDevice", "My device",' `
    -Message 'My device is missing from the Desktop app catalogue.'

Require-SourcePattern `
    -Path 'companion-windows\src-tauri\src\desktop_broker.rs' `
    -Pattern '"clock_get_state"\s*\|\s*"clock_control"' `
    -Message 'The Desktop broker does not expose the bounded Clock methods.'

Require-SourcePattern `
    -Path 'companion-windows\src-tauri\src\serial_service.rs' `
    -Pattern '"configure" if mode == "focus"[\s\S]{0,1600}"focusMinutes"[\s\S]{0,800}"breakMinutes"[\s\S]{0,800}"rounds"' `
    -Message 'The Windows host is missing validated versioned Focus controls.'

Require-SourcePattern `
    -Path 'desktop\src\EchoDesk.Desktop\MainWindow.xaml.cs' `
    -Pattern 'requestedMode is "countdown" or "focus"\) ShowAndActivate\(\)' `
    -Message 'A device Clock configuration request cannot surface the Desktop UI from the tray.'

Require-SourcePattern `
    -Path 'companion-windows\src-tauri\src\notifications.rs' `
    -Pattern 'Package::Current\(\)\.is_err\(\)[\s\S]{0,200}requiresPackagedBuild' `
    -Message 'Notification capture does not fail closed when the NSIS build lacks package identity.'

Require-SourcePattern `
    -Path 'companion-windows\src-tauri\src\notifications.rs' `
    -Pattern 'run_on_main_thread[\s\S]{0,500}RequestAccessAsync' `
    -Message 'UserNotificationListener access is not requested from the Windows UI thread.'

Require-SourcePattern `
    -Path 'companion-windows\src-tauri\src\notifications.rs' `
    -Pattern 'stable_id\(&source, os_id\)' `
    -Message 'Notification ids are not namespaced by source app.'

Require-SourcePattern `
    -Path 'companion-windows\src-tauri\src\settings.rs' `
    -Pattern 'notification_stack_exclusions: Vec<String>' `
    -Message 'Notification app exclusions are not persisted safely.'

Write-Output 'validate-windows-host: PASS'
