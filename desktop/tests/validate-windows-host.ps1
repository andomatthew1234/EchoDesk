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
    -Path 'desktop\src\EchoDesk.Desktop\ClockWindow.xaml.cs' `
    -Pattern 'ShowOnlyRequestedMode\(requestedMode\)' `
    -Message 'The Clock window still renders every mode instead of the requested utility.'

Require-SourcePattern `
    -Path 'desktop\src\EchoDesk.Desktop\ClockWindow.xaml.cs' `
    -Pattern 'if \(await viewModel\.ConfigureCountdownForDeviceAsync\(\)\) Close\(\)' `
    -Message 'Countdown Done closes without a broker-confirmed device command.'

Require-SourcePattern `
    -Path 'desktop\src\EchoDesk.Desktop\ClockWindow.xaml.cs' `
    -Pattern 'if \(await viewModel\.ConfigureFocusForDeviceAsync\(\)\) Close\(\)' `
    -Message 'Focus Done closes without a broker-confirmed device command.'

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

Require-SourcePattern `
    -Path 'companion-windows\src-tauri\src\settings.rs' `
    -Pattern 'notification_setup_survives_an_application_restart[\s\S]{0,700}to_disk_bytes\(\)[\s\S]{0,300}Settings::from_disk' `
    -Message 'Notification Stack setup has no restart-persistence regression test.'

Require-SourcePattern `
    -Path 'companion-windows\src-tauri\src\serial_service.rs' `
    -Pattern 'dashboard_apply_builds_the_exact_versioned_device_command_fields[\s\S]{0,900}echodesk_pc_dashboard_set_config' `
    -Message 'PC Dashboard Apply does not have an exact serial-command regression test.'

Require-SourcePattern `
    -Path 'companion-windows\src-tauri\src\music.rs' `
    -Pattern 'late_artwork_is_retried_without_spamming_or_reloading_success[\s\S]{0,1400}artwork_delivery_deduplicates_completed_and_in_flight_images' `
    -Message 'Now Playing artwork retry and deduplication are not regression tested.'

Require-SourcePattern `
    -Path 'companion-windows\src-tauri\src\serial_service.rs' `
    -Pattern 'bedside_apply_is_whole_config_strict_and_persistent[\s\S]{0,1800}echodesk_bedside_set_config[\s\S]{0,2200}"persist"\], json!\(true\)' `
    -Message 'Bedside Mode does not have a strict persistent whole-config serial regression test.'

Require-SourcePattern `
    -Path 'desktop\src\EchoDesk.Desktop\MainWindow.xaml' `
    -Pattern 'Visibility="\{Binding IsBedsideMode' `
    -Message 'The dedicated Bedside Mode page is missing.'

Require-SourcePattern `
    -Path 'desktop\src\EchoDesk.Desktop\MainWindow.xaml' `
    -Pattern 'Preview · 320 × 240 landscape' `
    -Message 'The Bedside Mode device preview is missing.'

Require-SourcePattern `
    -Path 'desktop\src\EchoDesk.Desktop\MainWindow.xaml' `
    -Pattern 'IsBedsideDisplaySection[\s\S]{0,1000}Auto-brightness &amp; display' `
    -Message 'The dedicated Bedside Mode display customisation page is missing.'

Require-SourcePattern `
    -Path 'desktop\src\EchoDesk.Desktop\App.xaml' `
    -Pattern '<Style TargetType="TextBox">[\s\S]{0,1800}FocusVisualStyle' `
    -Message 'Text fields do not have a complete theme-safe focus treatment.'

Require-SourcePattern `
    -Path 'desktop\src\EchoDesk.Desktop\App.xaml' `
    -Pattern '<Style TargetType="PasswordBox">[\s\S]{0,1200}SurfaceAltBrush' `
    -Message 'Password fields can fall back to unreadable native colours in dark mode.'

Require-SourcePattern `
    -Path 'desktop\src\EchoDesk.Desktop\App.xaml' `
    -Pattern '<Style TargetType="ListBoxItem">[\s\S]{0,1800}IsKeyboardFocusWithin' `
    -Message 'Selectable device rows have no keyboard-focus or selected-state treatment.'

Require-SourcePattern `
    -Path 'desktop\src\EchoDesk.Desktop\MainWindow.xaml' `
    -Pattern 'x:Name="BedsideLayout"[\s\S]{0,9000}Binding IsCompact[\s\S]{0,1200}Property="Grid.Row" Value="1"' `
    -Message 'Bedside Mode still forces a desktop-only two-column layout at compact widths.'

Require-SourcePattern `
    -Path 'desktop\src\EchoDesk.Desktop\MainWindow.xaml' `
    -Pattern 'AutomationProperties.LiveSetting="Polite"' `
    -Message 'Background status changes are not exposed to assistive technology.'

Require-SourcePattern `
    -Path 'desktop\src\EchoDesk.Desktop\MainWindow.xaml' `
    -Pattern 'Background="\{DynamicResource WarningSurfaceBrush\}"' `
    -Message 'The recovery warning still uses hard-coded light-only colours.'

Write-Output 'validate-windows-host: PASS'
