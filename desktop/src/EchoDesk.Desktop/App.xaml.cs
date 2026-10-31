using System.Threading;
using System.Windows;
using EchoDesk.Desktop.Core.Services;
using EchoDesk.Desktop.Services;
using EchoDesk.Desktop.ViewModels;

namespace EchoDesk.Desktop;

public partial class App : System.Windows.Application
{
    private Mutex? instanceMutex;
    private TrayService? tray;
    private BrokerClient? broker;
    private HostProcessManager? host;
    private MainViewModel? viewModel;
    private MainWindow? window;
    public static bool IsExiting { get; private set; }

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        instanceMutex = new Mutex(initiallyOwned: true, "Local\\EchoDesk.Desktop.v0.1", out var firstInstance);
        if (!firstInstance)
        {
            System.Windows.MessageBox.Show("EchoDesk Desktop is already running in the system tray.", "EchoDesk Desktop",
                MessageBoxButton.OK, MessageBoxImage.Information);
            Shutdown();
            return;
        }

        host = new HostProcessManager();
        try { host.Start(); }
        catch (Exception error)
        {
            System.Windows.MessageBox.Show(
                $"EchoDesk could not start its bundled background service.\n\n{error.Message}\n\nReinstall EchoDesk Desktop to restore missing files.",
                "EchoDesk Desktop", MessageBoxButton.OK, MessageBoxImage.Error);
            Shutdown();
            return;
        }

        broker = new BrokerClient();
        viewModel = new MainViewModel(broker, new PreferencesService());
        window = new MainWindow(viewModel);
        MainWindow = window;
        tray = new TrayService();
        tray.OpenRequested += (_, _) => window.ShowAndActivate();
        tray.ExitRequested += async (_, _) => await ExitAsync();
        Microsoft.Win32.SystemEvents.PowerModeChanged += OnPowerModeChanged;
        broker.StateChanged += (_, state) => Dispatcher.Invoke(() => tray.Update(state.IsConnected, state.Port));
        viewModel.PropertyChanged += (_, args) =>
        {
            if (args.PropertyName == nameof(MainViewModel.DesktopAppearance)) window.RefreshTitleBar();
        };
        viewModel.Start();

        if (e.Args.Contains("--tray", StringComparer.OrdinalIgnoreCase) && viewModel.MinimizeToTray)
        {
            window.Show();
            window.Hide();
        }
        else
        {
            window.Show();
        }
    }

    private async Task ExitAsync()
    {
        if (IsExiting) return;
        IsExiting = true;
        tray?.Dispose();
        tray = null;
        host?.PrepareForShutdown();
        if (broker is not null)
        {
            using var shutdownTimeout = new CancellationTokenSource(TimeSpan.FromSeconds(3));
            try { await broker.ShutdownHostAsync(shutdownTimeout.Token); }
            catch (Exception) { /* HostProcessManager applies a bounded owned-process fallback. */ }
        }
        if (viewModel is not null) await viewModel.DisposeAsync();
        if (host is not null) await host.DisposeAsync();
        host = null;
        window?.Close();
        Shutdown();
    }

    private void OnPowerModeChanged(object sender, Microsoft.Win32.PowerModeChangedEventArgs args)
    {
        if (args.Mode == Microsoft.Win32.PowerModes.Resume)
        {
            try { host?.Start(); } catch (Exception) { }
            Dispatcher.BeginInvoke(() =>
            {
                if (viewModel?.ReconnectCommand.CanExecute(null) == true)
                    viewModel.ReconnectCommand.Execute(null);
            });
        }
    }

    protected override void OnExit(ExitEventArgs e)
    {
        IsExiting = true;
        Microsoft.Win32.SystemEvents.PowerModeChanged -= OnPowerModeChanged;
        tray?.Dispose();
        if (host is not null)
        {
            host.PrepareForShutdown();
            host.DisposeAsync().AsTask().GetAwaiter().GetResult();
            host = null;
        }
        instanceMutex?.ReleaseMutex();
        instanceMutex?.Dispose();
        base.OnExit(e);
    }
}
