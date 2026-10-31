using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media.Animation;
using EchoDesk.Desktop.Services;
using EchoDesk.Desktop.ViewModels;

namespace EchoDesk.Desktop;

public partial class MainWindow : Window
{
    public MainWindow(MainViewModel viewModel)
    {
        InitializeComponent();
        DataContext = viewModel;
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

    private void FirmwareAction_Click(object sender, RoutedEventArgs e)
    {
        if (DataContext is not MainViewModel viewModel || sender is not FrameworkElement { Tag: string mode }) return;
        var destructive = mode is "reinstall" or "repair";
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
