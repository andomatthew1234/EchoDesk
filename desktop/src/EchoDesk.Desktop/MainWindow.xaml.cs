using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media.Animation;
using EchoDesk.Desktop.Services;
using EchoDesk.Desktop.ViewModels;

namespace EchoDesk.Desktop;

public partial class MainWindow : Window
{
    private ClockWindow? clockWindow;

    public MainWindow(MainViewModel viewModel)
    {
        InitializeComponent();
        DataContext = viewModel;
        viewModel.PropertyChanged += (_, args) =>
        {
            if ((args.PropertyName == nameof(MainViewModel.IsGuidedCompleteStep)
                 && viewModel.IsGuidedCompleteStep)
                || (args.PropertyName == nameof(MainViewModel.IsGuidedConnectStep)
                    && viewModel.IsGuidedConnectStep)
                || (args.PropertyName == nameof(MainViewModel.IsGuidedSetup)
                    && !viewModel.IsGuidedSetup))
            {
                GuidedWifiPasswordBox.Clear();
            }
            if (args.PropertyName == nameof(MainViewModel.IsGuidedCompleteStep)
                && viewModel.IsGuidedCompleteStep)
            {
                GuidedConfetti.BeginAnimation(
                    OpacityProperty,
                    new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(700))
                    { EasingFunction = new QuadraticEase { EasingMode = EasingMode.EaseOut } });
            }
        };
        viewModel.ClockWindowRequested += (_, mode) => Dispatcher.Invoke(() => ShowClockWindow(viewModel, mode));
        Loaded += OnLoaded;
        SizeChanged += (_, _) =>
        {
            viewModel.IsCompact = ActualWidth < 790;
            SidebarColumn.Width = new GridLength(viewModel.IsCompact ? 78 : 220);
        };
        Closing += (_, eventArgs) =>
        {
            if (!App.IsExiting && viewModel.MinimizeToTray)
            {
                eventArgs.Cancel = true;
                Hide();
            }
        };
    }

    private void ShowClockWindow(MainViewModel viewModel, string? requestedMode)
    {
        // A configuration request originates on the physical device. Make the
        // owner visible first so the owned utility cannot remain hidden behind
        // the tray/minimised main window.
        if (requestedMode is "countdown" or "focus") ShowAndActivate();
        if (clockWindow is null)
        {
            clockWindow = new ClockWindow(viewModel, requestedMode) { Owner = this };
            clockWindow.Closed += (_, _) => clockWindow = null;
            clockWindow.Show();
        }
        else
        {
            clockWindow.Show();
        }
        if (clockWindow.WindowState == WindowState.Minimized) clockWindow.WindowState = WindowState.Normal;
        clockWindow.Activate();
        clockWindow.Topmost = true;
        clockWindow.Topmost = false;
        clockWindow.Focus();
    }

    public void ShowAndActivate()
    {
        Show();
        if (WindowState == WindowState.Minimized) WindowState = WindowState.Normal;
        Activate();
        Topmost = true;
        Topmost = false;
        Focus();
    }

    private void OnLoaded(object sender, RoutedEventArgs args)
    {
        Opacity = 0;
        BeginAnimation(OpacityProperty, new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(180)));
        ApplyWindowsBackdrop();
    }

    private async void Restart_Click(object sender, RoutedEventArgs e)
    {
        if (DataContext is not MainViewModel viewModel) return;
        var result = System.Windows.MessageBox.Show(
            this,
            "Restart the physical EchoDesk now? The USB connection will briefly disconnect.",
            "Restart EchoDesk",
            MessageBoxButton.YesNo,
            MessageBoxImage.Warning,
            MessageBoxResult.Cancel);
        if (result == MessageBoxResult.Yes) await viewModel.RestartAsync();
    }

    private async void FactoryReset_Click(object sender, RoutedEventArgs e)
    {
        if (DataContext is not MainViewModel viewModel) return;
        var result = System.Windows.MessageBox.Show(
            this,
            "Factory Reset clears the device's setup completion and returns it to the Hello screen. " +
            "EchoDesk OS remains installed and unrelated flash partitions are not erased. Continue?",
            "Factory Reset EchoDesk",
            MessageBoxButton.YesNo,
            MessageBoxImage.Warning,
            MessageBoxResult.No);
        if (result == MessageBoxResult.Yes) await viewModel.FactoryResetAsync();
    }

    private void GuidedWifiPassword_Changed(object sender, RoutedEventArgs e)
    {
        if (DataContext is MainViewModel viewModel && sender is System.Windows.Controls.PasswordBox passwordBox)
        {
            viewModel.SetGuidedWifiPassword(passwordBox.Password);
        }
    }

    private void FirmwareAction_Click(object sender, RoutedEventArgs e)
    {
        if (DataContext is not MainViewModel viewModel || sender is not FrameworkElement { Tag: string mode }) return;
        var destructive = mode is "reinstall" or "repair" or "manualRecovery";
        if (mode == "manualRecovery")
        {
            var recoveryResult = System.Windows.MessageBox.Show(
                this,
                "Manual Recovery bypasses EchoDesk model, USB-adapter, installed-version and flash-size checks. " +
                "It will overwrite the bootloader, partitions and EchoDesk OS on the selected serial port. " +
                "The bundled firmware is still integrity-checked and writing proceeds only if the ROM bootloader positively identifies an original ESP32.\n\n" +
                "Close serial terminals and confirm the selected port belongs to your EchoDesk Mini. Continue?",
                "Confirm Manual Recovery",
                MessageBoxButton.YesNo,
                MessageBoxImage.Warning,
                MessageBoxResult.No);
            if (recoveryResult == MessageBoxResult.Yes && viewModel.InstallFirmwareCommand.CanExecute(mode))
            {
                viewModel.InstallFirmwareCommand.Execute(mode);
            }
            return;
        }
        var action = mode switch
        {
            "update" => "install the available EchoDesk OS update",
            "reinstall" => "reinstall the current EchoDesk OS version",
            "repair" => "start device recovery",
            _ => "install EchoDesk OS",
        };
        var warning = destructive
            ? " This may reset device settings; supported settings will be backed up when the device is readable."
            : string.Empty;
        var result = System.Windows.MessageBox.Show(
            this,
            $"Ready to {action}? Keep the device connected until it restarts and reconnects.{warning}",
            "Confirm firmware installation",
            MessageBoxButton.YesNo,
            destructive ? MessageBoxImage.Warning : MessageBoxImage.Information,
            MessageBoxResult.No);
        if (result == MessageBoxResult.Yes && viewModel.InstallFirmwareCommand.CanExecute(mode))
        {
            viewModel.InstallFirmwareCommand.Execute(mode);
        }
    }

    public void RefreshTitleBar() => ApplyWindowsBackdrop();

    private void ApplyWindowsBackdrop()
    {
        if (!OperatingSystem.IsWindowsVersionAtLeast(10, 0, 22000)) return;
        var handle = new WindowInteropHelper(this).Handle;
        var dark = DataContext is MainViewModel viewModel && ThemeService.IsDark(viewModel.DesktopAppearance) ? 1 : 0;
        _ = DwmSetWindowAttribute(handle, 20, ref dark, sizeof(int));
        var backdrop = 2; // DWMSBT_MAINWINDOW (Mica) on Windows 11.
        _ = DwmSetWindowAttribute(handle, 38, ref backdrop, sizeof(int));
    }

    [DllImport("dwmapi.dll")]
    private static extern int DwmSetWindowAttribute(nint hwnd, int attribute, ref int value, int valueSize);
}
