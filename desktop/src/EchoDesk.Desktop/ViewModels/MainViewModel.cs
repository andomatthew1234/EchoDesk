using System.Collections.ObjectModel;
using System.Windows;
using System.Windows.Input;
using System.IO;
using EchoDesk.Desktop.Core.Models;
using EchoDesk.Desktop.Core.Services;
using EchoDesk.Desktop.Infrastructure;
using EchoDesk.Desktop.Services;

namespace EchoDesk.Desktop.ViewModels;

public sealed class MainViewModel : ObservableObject, IAsyncDisposable
{
    private readonly BrokerClient broker;
    private readonly PreferencesService preferencesService;
    private readonly SynchronizationContext uiContext;
    private DesktopPreferences preferences;
    private BrokerState state = BrokerState.Offline();
    private string selectedPage = "Overview";
    private string feedback = "Starting EchoDesk services…";
    private int brightness = 50;
    private string deviceTheme = "light";
    private string nowPlayingLayout = "albumCover";
    private string nowPlayingBackground = "blurredAlbumCover";
    private bool applyingState;
    private bool isCompact;
    private string? selectedFirmwarePort;
    private string selectedFirmwareHardwareModel = "echodesk-mini-fnk0114a-2p4";
    private bool firmwareBoardConfirmed;
    private CancellationTokenSource? brightnessDebounce;

    public MainViewModel(BrokerClient broker, PreferencesService preferencesService)
    {
        this.broker = broker;
        this.preferencesService = preferencesService;
        uiContext = SynchronizationContext.Current ?? new SynchronizationContext();
        preferences = preferencesService.Load();
        broker.StateChanged += OnStateChanged;
        NavigateCommand = new RelayCommand(page => SelectedPage = page as string ?? "Overview");
        RefreshCommand = new AsyncRelayCommand(_ => ExecuteAsync(() => broker.RefreshAsync(), "Device information refreshed."));
        ReconnectCommand = new AsyncRelayCommand(_ => ExecuteAsync(() => broker.ReconnectDeviceAsync(), "Reconnect requested."));
        OpenAppCommand = new AsyncRelayCommand(OpenAppAsync, app => IsConnected && app is string value && SupportsApp(value));
        SetThemeCommand = new AsyncRelayCommand(SetThemeAsync, _ => IsConnected && SupportsSettings);
        SetNowPlayingCommand = new AsyncRelayCommand(_ => SetNowPlayingAsync(), _ => IsConnected && SupportsSettings);
        ToggleAiUsageCommand = new AsyncRelayCommand(
            _ => SetAiUsageEnabledAsync(!AiUsageEnabled),
            _ => IsBrokerAvailable && AiUsageAvailable);
        ScanFirmwareDevicesCommand = new AsyncRelayCommand(
            _ => ExecuteFirmwareAsync(() => broker.ScanFirmwareDevicesAsync(), "Device scan complete."),
            _ => IsBrokerAvailable && !FirmwareOperationActive);
        SelectFirmwareDeviceCommand = new AsyncRelayCommand(
            _ => SelectFirmwareDeviceAsync(),
            _ => CanSelectFirmwareDevice);
        CheckFirmwareUpdatesCommand = new AsyncRelayCommand(
            _ => ExecuteFirmwareAsync(() => broker.CheckFirmwareUpdatesAsync(), "Firmware update check complete."),
            _ => IsBrokerAvailable && !FirmwareOperationActive);
        InstallFirmwareCommand = new AsyncRelayCommand(
            mode => StartFirmwareOperationAsync(mode as string ?? "install"),
            mode => CanStartFirmwareOperation(mode as string));
        CancelFirmwareOperationCommand = new AsyncRelayCommand(
            _ => ExecuteFirmwareAsync(() => broker.CancelFirmwareOperationAsync(), "Firmware installation cancelled safely."),
            _ => FirmwareOperationActive && FirmwareManager.Stage is not "writing" and not "verifying_installation");
        ToggleUpdateNotificationsCommand = new AsyncRelayCommand(
            _ => ExecuteFirmwareAsync(
                () => broker.SetFirmwareUpdateNotificationsAsync(!FirmwareManager.UpdateNotificationsEnabled),
                FirmwareManager.UpdateNotificationsEnabled ? "Update notifications disabled." : "Update notifications enabled."),
            _ => IsBrokerAvailable && FirmwareManager.Available && !FirmwareOperationActive);
        AppearanceOptions = ["system", "light", "dark"];
        LayoutOptions =
        [
            new Choice("albumCover", "Album Cover"),
            new Choice("carPlay", "CarPlay Inspired"),
            new Choice("echoDesk", "EchoDesk Style"),
        ];
        BackgroundOptions =
        [
            new Choice("blurredAlbumCover", "Blurred Album Cover"),
            new Choice("echoDesk", "EchoDesk"),
            new Choice("audioBars", "Audio Bars"),
        ];
        FirmwareHardwareOptions =
        [
            new Choice("echodesk-mini-fnk0114a-2p4", "EchoDesk Mini · Freenove FNK0114A_2P4"),
        ];
        AppTiles = new ObservableCollection<AppTile>(EchoDeskApps.All.Select(app => new AppTile(app.Id, app.Label, app.Glyph)));
        ThemeService.Apply(preferences.Appearance);
    }

    public ICommand NavigateCommand { get; }
    public AsyncRelayCommand RefreshCommand { get; }
    public AsyncRelayCommand ReconnectCommand { get; }
    public AsyncRelayCommand OpenAppCommand { get; }
    public AsyncRelayCommand SetThemeCommand { get; }
    public AsyncRelayCommand SetNowPlayingCommand { get; }
    public AsyncRelayCommand ToggleAiUsageCommand { get; }
    public AsyncRelayCommand ScanFirmwareDevicesCommand { get; }
    public AsyncRelayCommand SelectFirmwareDeviceCommand { get; }
    public AsyncRelayCommand CheckFirmwareUpdatesCommand { get; }
    public AsyncRelayCommand InstallFirmwareCommand { get; }
    public AsyncRelayCommand CancelFirmwareOperationCommand { get; }
    public AsyncRelayCommand ToggleUpdateNotificationsCommand { get; }
    public ObservableCollection<AppTile> AppTiles { get; }
    public IReadOnlyList<string> AppearanceOptions { get; }
    public IReadOnlyList<Choice> LayoutOptions { get; }
    public IReadOnlyList<Choice> BackgroundOptions { get; }
    public IReadOnlyList<Choice> FirmwareHardwareOptions { get; }

    public string SelectedPage
    {
        get => selectedPage;
        set
        {
            if (!Set(ref selectedPage, value)) return;
            Raise(nameof(IsOverview)); Raise(nameof(IsDeviceControl)); Raise(nameof(IsCustomisation)); Raise(nameof(IsSetupUpdates));
            if (IsSetupUpdates && IsBrokerAvailable && !FirmwareOperationActive)
            {
                ScanFirmwareDevicesCommand.Execute(null);
            }
        }
    }

    public bool IsOverview => SelectedPage == "Overview";
    public bool IsDeviceControl => SelectedPage == "Device Control";
    public bool IsCustomisation => SelectedPage == "Customisation";
    public bool IsSetupUpdates => SelectedPage == "Setup & Updates";
    public bool IsCompact { get => isCompact; set => Set(ref isCompact, value); }
    public BrokerState State => state;
    public EchoDeskDevice? Device => state.Device;
    public bool IsConnected => state.IsConnected;
    public bool IsBrokerAvailable => state.BrokerAvailable;
    public string ConnectionLabel => IsConnected ? "CONNECTED" : IsBrokerAvailable ? state.Connection.ToUpperInvariant() : "HOST OFFLINE";
    public string ConnectionDetail => IsConnected
        ? $"{state.Port ?? "USB"} · EchoDesk is ready"
        : state.LastError ?? "EchoDesk is starting its background service and will reconnect automatically.";
    public string DeviceName => Device?.Name ?? "EchoDesk";
    public string Port => state.Port ?? "Not reported";
    public string Firmware => Device?.FirmwareVersion ?? "Not reported";
    public string Hardware => Device?.HardwareModel ?? "Not reported";
    public string ActiveApp => AppTiles.FirstOrDefault(item => item.Id == Device?.ActiveApp)?.Label ?? Device?.ActiveApp ?? "Not reported";
    public string Appearance => Device?.Theme ?? Device?.Settings.Theme ?? "Not reported";
    public string Feedback { get => feedback; private set => Set(ref feedback, value); }
    public bool SupportsSettings => Device?.Capabilities.ControlProtocol == 1;
    public bool AiUsageAvailable => state.AiUsage.Available;
    public bool AiUsageEnabled => state.AiUsage.Enabled;
    public bool AiUsageFetching => state.AiUsage.Fetching;
    public string AiUsageStatus => AiUsageAvailable ? FriendlyAiUsageStatus(state.AiUsage.Status) : "STARTING";
    public string AiUsageProvider => state.AiUsage.Provider;
    public string AiUsageLastUpdated => state.AiUsage.LastSuccessfulUpdate is { } updated
        ? updated.ToLocalTime().ToString("g")
        : "No successful update yet";
    public string AiUsageGuidance => AiUsageAvailable
        ? AiUsageGuidanceFor(state.AiUsage)
        : "Waiting for the bundled AI usage service.";
    public bool AiUsageNeedsAttention => state.AiUsage.Status is
        "authenticationRequired" or "sessionExpired" or "networkUnavailable" or "rateLimited"
        or "dependencyMissing" or "stale" or "error" or "unavailable";

    public FirmwareManagerState FirmwareManager => state.FirmwareManager;
    public IReadOnlyList<FirmwareDeviceCandidate> FirmwareDevices => FirmwareManager.Devices;
    public FirmwareDeviceCandidate? SelectedFirmwareDevice => FirmwareDevices.FirstOrDefault(device =>
        string.Equals(device.Port, SelectedFirmwarePort, StringComparison.OrdinalIgnoreCase));
    public string? SelectedFirmwarePort
    {
        get => selectedFirmwarePort;
        set
        {
            if (!Set(ref selectedFirmwarePort, value)) return;
            FirmwareBoardConfirmed = false;
            Raise(nameof(SelectedFirmwareDevice));
            RaiseFirmwareComputed();
        }
    }
    public string SelectedFirmwareHardwareModel
    {
        get => selectedFirmwareHardwareModel;
        set
        {
            if (!Set(ref selectedFirmwareHardwareModel, value)) return;
            FirmwareBoardConfirmed = false;
            RaiseFirmwareComputed();
        }
    }
    public bool FirmwareBoardConfirmed
    {
        get => firmwareBoardConfirmed;
        set
        {
            if (!Set(ref firmwareBoardConfirmed, value)) return;
            RaiseFirmwareComputed();
        }
    }
    public bool FirmwareOperationActive => FirmwareManager.Status is
        "downloading" or "preparing" or "installing" or "enteringBootloader" or "writing" or
        "verifying" or "restarting" or "reconnecting";
    public bool IsFirmwareConnectScreen => !FirmwareManager.Available || FirmwareManager.Status is
        "unavailable" or "idle" or "scanning" or "noDevice" or "selectionRequired";
    // Keep the current release details visible during the short manifest check;
    // a transient backend event must never leave Setup & Updates blank.
    public bool IsFirmwareReadyScreen => FirmwareManager.Status is "ready" or "checking" or "updateAvailable" or "upToDate";
    public bool IsFirmwareInstallingScreen => FirmwareOperationActive;
    public bool IsFirmwareSuccessScreen => FirmwareManager.Status == "success" && FirmwareManager.WriteCompleted && FirmwareManager.Reconnected;
    public bool IsFirmwarePartialScreen => FirmwareManager.Status == "partial" ||
        (FirmwareManager.Status == "success" && (!FirmwareManager.WriteCompleted || !FirmwareManager.Reconnected));
    public bool IsFirmwareFailureScreen => FirmwareManager.Status is "error" or "failed" or "cancelled";
    public bool ShowBoardConfirmation => SelectedFirmwareDevice?.RequiresBoardConfirmation == true;
    public bool CanSelectFirmwareDevice => SelectedFirmwareDevice is { Compatible: true } device
        && !FirmwareOperationActive
        && (!device.RequiresBoardConfirmation || FirmwareBoardConfirmed);
    public string FirmwareConnectionHeading => FirmwareManager.Status == "noDevice"
        ? "Connect EchoDesk"
        : FirmwareManager.Status == "selectionRequired" ? "Choose your EchoDesk" : "Connect EchoDesk";
    public string FirmwareConnectionMessage => FirmwareManager.Message ??
        "Connect EchoDesk Mini with its USB-C cable. We’ll identify the serial device and verify the ESP32 before any write is allowed.";
    public string FirmwareDeviceDetails => SelectedFirmwareDevice is null
        ? "No supported device selected"
        : string.Join(" · ", new[]
        {
            SelectedFirmwareDevice.Port,
            SelectedFirmwareDevice.Adapter ?? SelectedFirmwareDevice.DisplayName,
            SelectedFirmwareDevice.ChipFamily ?? "chip not identified",
        });
    public string FirmwareCurrentVersion => FirmwareManager.CurrentVersion ?? SelectedFirmwareDevice?.FirmwareVersion ?? "Not installed or not detected";
    public string FirmwareAvailableVersion => FirmwareManager.AvailableVersion ?? "Bundled verified release";
    public string FirmwareReleaseNotes => FirmwareManager.ReleaseNotes ?? "This verified EchoDesk OS build is bundled with EchoDesk Desktop.";
    public string FirmwareStageLabel => FriendlyFirmwareStage(FirmwareManager.Stage);
    public string FirmwareProgressLabel => FirmwareManager.ProgressPercent is { } progress
        ? $"{progress}%" : "Working…";
    public string FirmwareOperationMessage => FirmwareManager.Message ?? "Keep EchoDesk connected. Do not unplug it or allow this PC to sleep.";
    public string FirmwareErrorMessage => FirmwareManager.ErrorMessage ??
        "EchoDesk OS was not installed. Check the USB cable, close serial tools and try again.";
    public bool FirmwareHasMeasuredProgress => FirmwareManager.ProgressPercent is not null;
    public bool FirmwareHasNoMeasuredProgress => FirmwareManager.ProgressPercent is null;
    public bool FirmwareUpdateAvailable => FirmwareManager.Status == "updateAvailable" && FirmwareManager.AvailableVersion is not null;
    public bool FirmwareIsUpToDate => FirmwareManager.Status == "upToDate";
    public bool UpdateNotificationsEnabled => FirmwareManager.UpdateNotificationsEnabled;

    public int Brightness
    {
        get => brightness;
        set
        {
            value = Math.Clamp(value, 10, 100);
            if (!Set(ref brightness, value) || applyingState) return;
            ScheduleBrightnessUpdate();
        }
    }

    public string DeviceTheme
    {
        get => deviceTheme;
        set => Set(ref deviceTheme, value);
    }

    public string NowPlayingLayout
    {
        get => nowPlayingLayout;
        set
        {
            if (Set(ref nowPlayingLayout, value) && !applyingState) SetNowPlayingCommand.Execute(null);
        }
    }

    public string NowPlayingBackground
    {
        get => nowPlayingBackground;
        set
        {
            if (Set(ref nowPlayingBackground, value) && !applyingState) SetNowPlayingCommand.Execute(null);
        }
    }

    public string DesktopAppearance
    {
        get => preferences.Appearance;
        set
        {
            if (preferences.Appearance == value) return;
            preferences = preferences with { Appearance = value };
            SavePreferences();
            ThemeService.Apply(value);
            Raise();
        }
    }

    public bool MinimizeToTray
    {
        get => preferences.MinimizeToTray;
        set
        {
            if (preferences.MinimizeToTray == value) return;
            preferences = preferences with { MinimizeToTray = value };
            SavePreferences(); Raise();
        }
    }

    public bool LaunchWithWindows
    {
        get => preferences.LaunchWithWindows;
        set
        {
            if (preferences.LaunchWithWindows == value) return;
            try
            {
                StartupService.SetEnabled(value);
                preferences = preferences with { LaunchWithWindows = value };
                SavePreferences(); Raise();
            }
            catch (Exception error) { Feedback = $"Could not change Windows startup: {error.Message}"; }
        }
    }

    public void Start() => broker.Start();

    public Task RestartAsync() => ExecuteAsync(() => broker.RestartAsync(), "EchoDesk is restarting. Reconnection is automatic.");

    private Task SetAiUsageEnabledAsync(bool enabled) => ExecuteAsync(
        () => broker.SetAiUsageEnabledAsync(enabled),
        enabled ? "AI Usage Monitor enabled." : "AI Usage Monitor disabled. Background polling has stopped.");

    private void OnStateChanged(object? sender, BrokerState next) =>
        uiContext.Post(_ => ApplyState(next), null);

    private void ApplyState(BrokerState next)
    {
        if (next.BrokerRevision != 0 && state.BrokerRevision != 0
            && next.BrokerRevision < state.BrokerRevision) return;
        state = next;
        applyingState = true;
        try
        {
            if (next.Device?.Settings is { } settings)
            {
                Brightness = settings.Brightness ?? next.Device.Brightness ?? Brightness;
                DeviceTheme = settings.Theme ?? next.Device.Theme ?? DeviceTheme;
                NowPlayingLayout = settings.NowPlayingLayout ?? NowPlayingLayout;
                NowPlayingBackground = settings.NowPlayingBackground ?? NowPlayingBackground;
            }
        }
        finally { applyingState = false; }

        var managerPort = next.FirmwareManager.SelectedPort;
        if (!string.IsNullOrWhiteSpace(managerPort))
        {
            selectedFirmwarePort = managerPort;
            firmwareBoardConfirmed = next.FirmwareManager.BoardConfirmed;
        }
        else if (selectedFirmwarePort is null || !next.FirmwareManager.Devices.Any(device =>
                     string.Equals(device.Port, selectedFirmwarePort, StringComparison.OrdinalIgnoreCase)))
        {
            selectedFirmwarePort = next.FirmwareManager.Devices.Count == 1
                ? next.FirmwareManager.Devices[0].Port
                : null;
            firmwareBoardConfirmed = false;
        }

        foreach (var name in new[]
                 {
                     nameof(State), nameof(Device), nameof(IsConnected), nameof(IsBrokerAvailable),
                     nameof(ConnectionLabel), nameof(ConnectionDetail), nameof(DeviceName), nameof(Port),
                     nameof(Firmware), nameof(Hardware), nameof(ActiveApp), nameof(Appearance), nameof(SupportsSettings),
                     nameof(AiUsageAvailable), nameof(AiUsageEnabled), nameof(AiUsageFetching), nameof(AiUsageStatus),
                     nameof(AiUsageProvider), nameof(AiUsageLastUpdated), nameof(AiUsageGuidance), nameof(AiUsageNeedsAttention),
                     nameof(FirmwareManager), nameof(FirmwareDevices), nameof(SelectedFirmwarePort), nameof(SelectedFirmwareDevice),
                 }) Raise(name);
        RaiseFirmwareComputed();
        RefreshCommands();
        if (IsConnected) Feedback = "Device state is synchronised.";
    }

    private async Task OpenAppAsync(object? parameter)
    {
        if (parameter is not string app || !SupportsApp(app)) return;
        await ExecuteAsync(() => broker.OpenAppAsync(app), $"Opened {AppTiles.First(item => item.Id == app).Label} on EchoDesk.");
    }

    private async Task SetThemeAsync(object? parameter)
    {
        var requestedTheme = parameter as string ?? DeviceTheme;
        await SetSettingsAsync(new SettingsPatch { Theme = requestedTheme }, true, "Appearance updated.");
    }

    private async Task SelectFirmwareDeviceAsync()
    {
        if (!CanSelectFirmwareDevice || SelectedFirmwarePort is null) return;
        await ExecuteFirmwareAsync(
            () => broker.SelectFirmwareDeviceAsync(
                SelectedFirmwarePort, SelectedFirmwareHardwareModel, FirmwareBoardConfirmed),
            "Device selected and compatibility checked.");
    }

    private async Task StartFirmwareOperationAsync(string mode)
    {
        if (!CanStartFirmwareOperation(mode)) return;
        await ExecuteFirmwareAsync(
            () => broker.StartFirmwareOperationAsync(mode),
            "Firmware installation started. Keep EchoDesk connected.");
    }

    private bool CanStartFirmwareOperation(string? mode)
    {
        if (!IsBrokerAvailable || !FirmwareManager.Available || FirmwareOperationActive
            || FirmwareManager.Status == "checking") return false;
        if (mode == "update") return FirmwareUpdateAvailable;
        return IsFirmwareReadyScreen && FirmwareManager.SelectedPort is not null;
    }

    private async Task ExecuteFirmwareAsync(Func<Task<BrokerState>> action, string success)
    {
        try
        {
            var next = await action();
            uiContext.Post(_ => { ApplyState(next); Feedback = success; }, null);
        }
        catch (Exception error)
        {
            uiContext.Post(_ => Feedback = FriendlyFirmwareError(error), null);
        }
    }

    private Task SetNowPlayingAsync() => SetSettingsAsync(
        new SettingsPatch
        {
            NowPlayingLayout = NowPlayingLayout,
            NowPlayingBackground = NowPlayingBackground,
            NowPlayingBackgroundCustomized = true,
        }, true, "Now Playing customisation updated.");

    private void ScheduleBrightnessUpdate()
    {
        brightnessDebounce?.Cancel();
        brightnessDebounce?.Dispose();
        brightnessDebounce = new CancellationTokenSource();
        var token = brightnessDebounce.Token;
        _ = UpdateBrightnessAsync(token);
    }

    private async Task UpdateBrightnessAsync(CancellationToken cancellationToken)
    {
        try
        {
            await Task.Delay(100, cancellationToken);
            await SetSettingsAsync(new SettingsPatch { Brightness = Brightness }, false, "Brightness previewed.", cancellationToken);
            await Task.Delay(500, cancellationToken);
            await SetSettingsAsync(new SettingsPatch { Brightness = Brightness }, true, "Brightness saved.", cancellationToken);
        }
        catch (OperationCanceledException) { }
    }

    private async Task SetSettingsAsync(
        SettingsPatch patch,
        bool persist,
        string success,
        CancellationToken cancellationToken = default)
    {
        var revision = Device?.SettingsRevision ?? 0;
        try
        {
            var next = await broker.SetSettingsAsync(revision, patch, persist, cancellationToken);
            uiContext.Post(_ => { ApplyState(next); Feedback = success; }, null);
        }
        catch (BrokerException error) when (error.Code is "stale_revision" or "stale_settings")
        {
            await broker.RefreshAsync(cancellationToken);
            uiContext.Post(_ => Feedback = "Device settings changed on the touchscreen. Refreshed without overwriting them.", null);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested) { }
        catch (Exception error)
        {
            uiContext.Post(_ =>
            {
                ApplyState(state);
                Feedback = Friendly(error);
            }, null);
        }
    }

    private async Task ExecuteAsync(Func<Task<BrokerState>> action, string success)
    {
        try
        {
            var next = await action();
            uiContext.Post(_ => { ApplyState(next); Feedback = success; }, null);
        }
        catch (Exception error)
        {
            uiContext.Post(_ =>
            {
                // Reassert broker-confirmed values after a rejected command (notably toggle controls).
                ApplyState(state);
                Feedback = Friendly(error);
            }, null);
        }
    }

    private bool SupportsApp(string app) => Device?.SupportedApps.Contains(app, StringComparer.OrdinalIgnoreCase) == true;

    private void RefreshCommands()
    {
        RefreshCommand.Refresh(); ReconnectCommand.Refresh(); OpenAppCommand.Refresh();
        SetThemeCommand.Refresh(); SetNowPlayingCommand.Refresh();
        ToggleAiUsageCommand.Refresh();
        ScanFirmwareDevicesCommand.Refresh(); SelectFirmwareDeviceCommand.Refresh();
        CheckFirmwareUpdatesCommand.Refresh(); InstallFirmwareCommand.Refresh();
        CancelFirmwareOperationCommand.Refresh(); ToggleUpdateNotificationsCommand.Refresh();
    }

    private void RaiseFirmwareComputed()
    {
        foreach (var name in new[]
                 {
                     nameof(SelectedFirmwareDevice), nameof(FirmwareOperationActive), nameof(IsFirmwareConnectScreen),
                     nameof(IsFirmwareReadyScreen), nameof(IsFirmwareInstallingScreen), nameof(IsFirmwareSuccessScreen),
                     nameof(IsFirmwarePartialScreen), nameof(IsFirmwareFailureScreen), nameof(ShowBoardConfirmation),
                     nameof(CanSelectFirmwareDevice), nameof(FirmwareConnectionHeading), nameof(FirmwareConnectionMessage),
                     nameof(FirmwareDeviceDetails), nameof(FirmwareCurrentVersion), nameof(FirmwareAvailableVersion),
                     nameof(FirmwareReleaseNotes), nameof(FirmwareStageLabel), nameof(FirmwareProgressLabel),
                     nameof(FirmwareOperationMessage), nameof(FirmwareErrorMessage), nameof(FirmwareHasMeasuredProgress),
                     nameof(FirmwareHasNoMeasuredProgress), nameof(FirmwareUpdateAvailable), nameof(FirmwareIsUpToDate),
                     nameof(UpdateNotificationsEnabled),
                 }) Raise(name);
        RefreshCommands();
    }

    private static string FriendlyFirmwareStage(string? stage) => stage switch
    {
        "preparing" => "Preparing device",
        "verifying_firmware" => "Verifying firmware",
        "entering_bootloader" => "Entering bootloader",
        "writing" => "Writing firmware",
        "verifying_installation" => "Verifying installation",
        "restarting" => "Restarting",
        "reconnecting" => "Connecting to EchoDesk",
        _ => "Preparing installation",
    };

    private static string FriendlyFirmwareError(Exception error) => error switch
    {
        BrokerException { Code: "wrong_hardware" } => "This device is not an EchoDesk Mini. Nothing was written.",
        BrokerException { Code: "checksum_mismatch" } => "Firmware verification failed. Nothing was written; check for updates and try again.",
        BrokerException { Code: "port_busy" } => "Another application is using the device. Close serial terminals and try again.",
        BrokerException { Code: "permission_denied" } => "Windows denied access to the device. Reconnect it and try again.",
        BrokerException brokerError => brokerError.Message,
        TimeoutException => "The firmware service did not respond in time. No success has been assumed.",
        _ => $"Firmware action failed: {error.Message}",
    };

    private static string FriendlyAiUsageStatus(string status) => status switch
    {
        "disabled" => "DISABLED",
        "waiting" => "WAITING",
        "updating" or "fetching" or "checking" => "UPDATING",
        "ready" or "connected" or "live" => "LIVE",
        "authenticationRequired" => "SIGN-IN REQUIRED",
        "sessionExpired" => "SESSION EXPIRED",
        "networkUnavailable" => "OFFLINE",
        "rateLimited" => "RATE LIMITED",
        "dependencyMissing" => "REPAIR REQUIRED",
        "stale" => "STALE",
        "unavailable" => "UNAVAILABLE",
        "error" => "NEEDS ATTENTION",
        _ => status.Replace('_', ' ').ToUpperInvariant(),
    };

    private static string AiUsageGuidanceFor(AiUsageComponentState component) => component.Status switch
    {
        "disabled" => "Enable this component to resume private background usage checks.",
        "waiting" => "Waiting for the first Codex usage result. EchoDesk continues running in the background.",
        "updating" or "fetching" or "checking" => "Retrieving Codex usage in the background…",
        "ready" or "connected" or "live" => "Codex usage is updating automatically in the background.",
        "authenticationRequired" => "Sign in with the Codex CLI on this Windows account, then leave EchoDesk running.",
        "sessionExpired" => "Your Codex session expired. Sign in again with the Codex CLI; never paste credentials into EchoDesk.",
        "networkUnavailable" => "Usage could not be refreshed. Check this PC's network connection; the last successful data is preserved.",
        "rateLimited" => "The provider is temporarily limiting requests. EchoDesk will retry automatically.",
        "dependencyMissing" => "The bundled usage provider is missing. Repair or reinstall EchoDesk Desktop.",
        "stale" => "The last successful usage data is out of date. EchoDesk will keep retrying in the background.",
        "unavailable" when component.Error?.Contains("network", StringComparison.OrdinalIgnoreCase) == true
            => "Usage could not be refreshed. Check this PC's network connection; the last successful data is preserved.",
        "unavailable" when component.Error?.Contains("rate", StringComparison.OrdinalIgnoreCase) == true
            => "The provider is temporarily limiting requests. EchoDesk will retry automatically.",
        "unavailable" => "Codex usage is temporarily unavailable. EchoDesk will retry automatically.",
        "error" => "Usage could not be refreshed safely. EchoDesk will retry; sign in again if the problem continues.",
        _ => "AI usage is managed by the bundled EchoDesk service.",
    };

    private static string Friendly(Exception error) => error switch
    {
        BrokerException brokerError => brokerError.Message,
        TimeoutException => "The device did not confirm the command in time.",
        IOException => "The companion disconnected. Reconnecting automatically…",
        _ => $"Command failed: {error.Message}",
    };

    private void SavePreferences()
    {
        try { preferencesService.Save(preferences); }
        catch (IOException error) { Feedback = $"Could not save desktop preferences: {error.Message}"; }
    }

    public async ValueTask DisposeAsync()
    {
        broker.StateChanged -= OnStateChanged;
        brightnessDebounce?.Cancel();
        brightnessDebounce?.Dispose();
        await broker.DisposeAsync();
    }
}

public sealed record AppTile(string Id, string Label, string Glyph);
public sealed record Choice(string Value, string Label);
