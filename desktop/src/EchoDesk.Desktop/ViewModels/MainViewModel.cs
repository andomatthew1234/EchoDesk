using System.Collections.ObjectModel;
using System.Diagnostics;
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
    private readonly GuidedSetupFlow guidedSetup;
    private readonly FirstRunFlow firstRun;
    private DesktopPreferences preferences;
    private BrokerState state = BrokerState.Offline();
    private string selectedPage = "Overview";
    private string feedback = "Starting EchoDesk services…";
    private int brightness = 50;
    private string deviceTheme = "light";
    private string nowPlayingLayout = "albumCover";
    private string nowPlayingBackground = "blurredAlbumCover";
    private string defaultApp = "home";
    private string clockFormat = "24h";
    private string timezone = "Australia/Melbourne";
    private string codexPercentageMode = "used";
    private string codexPreferredDisplay = "both";
    private string performanceDefaultView = "overview";
    private bool applyingState;
    private bool isCompact;
    private string? selectedFirmwarePort;
    private string selectedFirmwareHardwareModel = BrokerProtocol.EchoDeskMiniHardwareModel;
    private bool firmwareBoardConfirmed;
    private CancellationTokenSource? brightnessDebounce;
    private CancellationTokenSource? guidedDiscovery;
    private bool guidedSetupFinalizing;
    private string? guidedSetupError;
    private string? guidedWifiDeviceId;
    private string guidedWifiSetupCode = "";
    private string guidedWifiSsid = "";
    private string guidedWifiPassword = "";
    private bool showUnknownSetupDevices;
    private bool guidedUpdateRequested;

    public MainViewModel(BrokerClient broker, PreferencesService preferencesService)
    {
        this.broker = broker;
        this.preferencesService = preferencesService;
        uiContext = SynchronizationContext.Current ?? new SynchronizationContext();
        guidedSetup = new GuidedSetupFlow();
        preferences = preferencesService.Load();
        firstRun = new FirstRunFlow(preferences.FirstRunCompleted);
        selectedPage = preferences.FirstRunCompleted ? "Overview" : "First run";
        broker.StateChanged += OnStateChanged;
        NavigateCommand = new RelayCommand(page => SelectedPage = page as string ?? "Overview");
        RefreshCommand = new AsyncRelayCommand(_ => ExecuteAsync(() => broker.RefreshAsync(), "Device information refreshed."));
        ReconnectCommand = new AsyncRelayCommand(_ => ExecuteAsync(() => broker.ReconnectDeviceAsync(), "Reconnect requested."));
        OpenAppCommand = new AsyncRelayCommand(OpenAppAsync, app => IsConnected && app is string value && SupportsApp(value));
        SetThemeCommand = new AsyncRelayCommand(SetThemeAsync, _ => IsConnected && SupportsSettings);
        SetNowPlayingCommand = new AsyncRelayCommand(_ => SetNowPlayingAsync(), _ => IsConnected && SupportsSettings);
        SetAdvancedSettingCommand = new AsyncRelayCommand(
            SetAdvancedSettingAsync,
            setting => setting is string key && SupportsDeviceSetting(key));
        ToggleAiUsageCommand = new AsyncRelayCommand(
            _ => SetAiUsageEnabledAsync(!AiUsageEnabled),
            _ => IsBrokerAvailable && AiUsageAvailable);
        ScanFirmwareDevicesCommand = new AsyncRelayCommand(
            _ => ExecuteFirmwareAsync(() => broker.ScanFirmwareDevicesAsync(), "Device scan complete."),
            _ => IsBrokerAvailable && !FirmwareOperationActive);
        RevealFirmwareUnknownCommand = new AsyncRelayCommand(
            _ => ExecuteFirmwareAsync(() => broker.ScanFirmwareDevicesAsync(true), "Unidentified USB devices are now visible."),
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
        FirmwareRecoveryActionCommand = new AsyncRelayCommand(
            RunFirmwareRecoveryActionAsync,
            action => CanRunFirmwareRecoveryAction(action as string));
        CancelFirmwareOperationCommand = new AsyncRelayCommand(
            _ => ExecuteFirmwareAsync(() => broker.CancelFirmwareOperationAsync(), "Firmware installation cancelled safely."),
            _ => FirmwareOperationActive && FirmwareManager.Stage is not "writing" and not "verifying_installation");
        ToggleUpdateNotificationsCommand = new AsyncRelayCommand(
            _ => ExecuteFirmwareAsync(
                () => broker.SetFirmwareUpdateNotificationsAsync(!FirmwareManager.UpdateNotificationsEnabled),
                FirmwareManager.UpdateNotificationsEnabled ? "Update notifications disabled." : "Update notifications enabled."),
            _ => IsBrokerAvailable && FirmwareManager.Available && !FirmwareOperationActive);
        GuidedSetupContinueCommand = new AsyncRelayCommand(
            _ => ContinueGuidedSetupAsync(), _ => CanContinueGuidedSetup);
        GuidedSetupBackCommand = new RelayCommand(
            _ => MoveGuidedSetupBack(), _ => CanGoBackInGuidedSetup);
        GuidedSetupRefreshCommand = new AsyncRelayCommand(
            _ => RefreshGuidedDevicesAsync(), _ => IsBrokerAvailable && !FirmwareOperationActive);
        GuidedSetupRetryCommand = new AsyncRelayCommand(
            _ => RetryGuidedSetupAsync(), _ => CanRetryGuidedSetup);
        GuidedSetupCancelCommand = new AsyncRelayCommand(
            _ => CancelGuidedSetupAsync(), _ => CanCancelGuidedSetup);
        GuidedSetupChooseTransportCommand = new RelayCommand(
            ChooseGuidedSetupTransport,
            _ => guidedSetup.Step == GuidedSetupStep.Connect && !guidedSetupFinalizing);
        GuidedSetupChoiceCommand = new AsyncRelayCommand(
            ChooseGuidedSetupPathAsync,
            option => CanChooseGuidedSetupPath(option as string));
        GuidedSetupRevealUnknownCommand = new AsyncRelayCommand(
            _ => RevealUnknownSetupDevicesAsync(),
            _ => IsGuidedUsbSelectStep && !showUnknownSetupDevices && !FirmwareOperationActive);
        GuidedWifiNextCommand = new AsyncRelayCommand(
            _ => AdvanceGuidedWifiAsync(),
            _ => CanAdvanceGuidedWifi);
        GuidedWifiProvisionCommand = new AsyncRelayCommand(
            _ => ProvisionGuidedWifiAsync(), _ => CanProvisionGuidedWifi);
        OpenWindowsWifiSettingsCommand = new RelayCommand(_ =>
            Process.Start(new ProcessStartInfo("ms-settings:network-wifi") { UseShellExecute = true }));
        CompleteGuidedQuickSettingsCommand = new AsyncRelayCommand(
            _ => CompleteGuidedQuickSettingsAsync(),
            _ => IsGuidedQuickSettingsStep && !guidedSetupFinalizing);
        FinishGuidedSetupCommand = new RelayCommand(_ => FinishGuidedSetup());
        StartSetupAfterRecoveryCommand = new RelayCommand(_ => StartSetupAfterRecovery());
        FirstRunContinueCommand = new RelayCommand(_ => ContinueFirstRun(), _ => IsFirstRunWelcome);
        FirstRunChoiceCommand = new RelayCommand(CompleteFirstRun, _ => IsFirstRunSetupPrompt);
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
        DefaultAppOptions =
        [
            new Choice("home", "Home"), new Choice("codex", "Codex Usage"),
            new Choice("clock", "Clock"), new Choice("performance", "Performance"),
            new Choice("nowPlaying", "Now Playing"), new Choice("settings", "Settings"),
        ];
        ClockFormatOptions = [new Choice("12h", "12-hour"), new Choice("24h", "24-hour")];
        TimezoneOptions =
        [
            new Choice("Australia/Melbourne", "Melbourne"),
            new Choice("Australia/Sydney", "Sydney"),
            new Choice("UTC", "UTC"),
        ];
        CodexPercentageModeOptions =
        [new Choice("used", "Percentage used"), new Choice("remaining", "Percentage remaining")];
        CodexPreferredDisplayOptions =
        [new Choice("both", "Session and weekly"), new Choice("session", "5-hour session"), new Choice("weekly", "Weekly")];
        PerformanceDefaultViewOptions =
        [new Choice("overview", "All Performance"), new Choice("cpu", "CPU Usage"), new Choice("gpu", "GPU Usage"), new Choice("memory", "RAM Usage")];
        FirmwareHardwareOptions =
        [
            new Choice(BrokerProtocol.EchoDeskMiniHardwareModel, "EchoDesk Mini · Freenove FNK0114A_2P4"),
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
    public AsyncRelayCommand SetAdvancedSettingCommand { get; }
    public AsyncRelayCommand ToggleAiUsageCommand { get; }
    public AsyncRelayCommand ScanFirmwareDevicesCommand { get; }
    public AsyncRelayCommand RevealFirmwareUnknownCommand { get; }
    public AsyncRelayCommand SelectFirmwareDeviceCommand { get; }
    public AsyncRelayCommand CheckFirmwareUpdatesCommand { get; }
    public AsyncRelayCommand InstallFirmwareCommand { get; }
    public AsyncRelayCommand FirmwareRecoveryActionCommand { get; }
    public AsyncRelayCommand CancelFirmwareOperationCommand { get; }
    public AsyncRelayCommand ToggleUpdateNotificationsCommand { get; }
    public AsyncRelayCommand GuidedSetupContinueCommand { get; }
    public RelayCommand GuidedSetupBackCommand { get; }
    public AsyncRelayCommand GuidedSetupRefreshCommand { get; }
    public AsyncRelayCommand GuidedSetupRetryCommand { get; }
    public AsyncRelayCommand GuidedSetupCancelCommand { get; }
    public RelayCommand GuidedSetupChooseTransportCommand { get; }
    public AsyncRelayCommand GuidedSetupChoiceCommand { get; }
    public AsyncRelayCommand GuidedSetupRevealUnknownCommand { get; }
    public AsyncRelayCommand GuidedWifiNextCommand { get; }
    public AsyncRelayCommand GuidedWifiProvisionCommand { get; }
    public RelayCommand OpenWindowsWifiSettingsCommand { get; }
    public AsyncRelayCommand CompleteGuidedQuickSettingsCommand { get; }
    public RelayCommand FinishGuidedSetupCommand { get; }
    public RelayCommand StartSetupAfterRecoveryCommand { get; }
    public RelayCommand FirstRunContinueCommand { get; }
    public RelayCommand FirstRunChoiceCommand { get; }
    public ObservableCollection<AppTile> AppTiles { get; }
    public IReadOnlyList<string> AppearanceOptions { get; }
    public IReadOnlyList<Choice> LayoutOptions { get; }
    public IReadOnlyList<Choice> BackgroundOptions { get; }
    public IReadOnlyList<Choice> DefaultAppOptions { get; }
    public IReadOnlyList<Choice> ClockFormatOptions { get; }
    public IReadOnlyList<Choice> TimezoneOptions { get; }
    public IReadOnlyList<Choice> CodexPercentageModeOptions { get; }
    public IReadOnlyList<Choice> CodexPreferredDisplayOptions { get; }
    public IReadOnlyList<Choice> PerformanceDefaultViewOptions { get; }
    public IReadOnlyList<Choice> FirmwareHardwareOptions { get; }

    public string SelectedPage
    {
        get => selectedPage;
        set
        {
            if (!Set(ref selectedPage, value)) return;
            Raise(nameof(IsOverview)); Raise(nameof(IsDeviceControl)); Raise(nameof(IsCustomisation)); Raise(nameof(IsSetupUpdates));
            Raise(nameof(IsGuidedSetup)); Raise(nameof(IsFirstRun)); Raise(nameof(IsMainShellEnabled));
            if (IsSetupUpdates && IsBrokerAvailable && !FirmwareOperationActive)
            {
                ScanFirmwareDevicesCommand.Execute(null);
            }
            if (IsGuidedSetup && guidedSetup.Step == GuidedSetupStep.SelectDevice)
                StartGuidedDiscovery();
            else if (!IsGuidedSetup)
            {
                StopGuidedDiscovery();
                guidedWifiPassword = "";
                guidedWifiSetupCode = "";
            }
        }
    }

    public bool IsOverview => SelectedPage == "Overview";
    public bool IsFirstRun => SelectedPage == "First run";
    public bool IsMainShellEnabled => !IsFirstRun;
    public bool IsFirstRunWelcome => IsFirstRun && firstRun.Step == FirstRunStep.Welcome;
    public bool IsFirstRunSetupPrompt => IsFirstRun && firstRun.Step == FirstRunStep.SetupPrompt;
    public bool IsDeviceControl => SelectedPage == "Device Control";
    public bool IsCustomisation => SelectedPage == "Customisation";
    public bool IsSetupUpdates => SelectedPage == "Device recovery & updates";
    public bool IsGuidedSetup => SelectedPage == "Set up a new device";
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
    public bool SupportsDefaultApp => SupportsDeviceSetting("defaultApp");
    public bool SupportsClockFormat => SupportsDeviceSetting("clockFormat");
    public bool SupportsTimezone => SupportsDeviceSetting("timezone");
    public bool SupportsCodexPercentageMode => SupportsDeviceSetting("codexPercentageMode");
    public bool SupportsCodexPreferredDisplay => SupportsDeviceSetting("codexPreferredDisplay");
    public bool SupportsPerformanceDefaultView => SupportsDeviceSetting("performanceDefaultView");
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
    public bool IsFirmwareOverviewScreen => !IsFirmwareInstallingScreen && !IsFirmwareSuccessScreen
        && !IsFirmwarePartialScreen && !IsFirmwareFailureScreen;
    // Keep the current release details visible during the short manifest check;
    // A transient backend event must never leave recovery and updates blank.
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
    public bool CanStartManualRecovery => IsBrokerAvailable && FirmwareManager.Available
        && !FirmwareOperationActive && FirmwareManager.Status != "checking"
        && SelectedFirmwareDevice is not null && !string.IsNullOrWhiteSpace(SelectedFirmwarePort);
    public bool FirmwareTargetSelected => SelectedFirmwareDevice is not null
        && !string.IsNullOrWhiteSpace(FirmwareManager.SelectedPort)
        && string.Equals(FirmwareManager.SelectedPort, SelectedFirmwarePort, StringComparison.OrdinalIgnoreCase)
        && (!SelectedFirmwareDevice.RequiresBoardConfirmation || FirmwareManager.BoardConfirmed);
    public bool CanUseFirmwareActions => IsBrokerAvailable && FirmwareManager.Available
        && FirmwareTargetSelected && !FirmwareOperationActive && FirmwareManager.Status != "checking";
    public string FirmwareConnectionHeading => FirmwareManager.Status == "noDevice"
        ? "Connect EchoDesk"
        : FirmwareManager.Status is "ready" or "checking" or "updateAvailable" or "upToDate"
            ? "Selected EchoDesk"
            : "Choose your EchoDesk";
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
    public bool CanCancelFirmwareOperation => FirmwareManager.Stage is not "writing" and not "verifying_installation";

    public GuidedSetupStep CurrentGuidedSetupStep => guidedSetup.Step;
    public GuidedSetupTransport CurrentGuidedSetupTransport => guidedSetup.Transport;
    public bool GuidedSetupUsesUsb => guidedSetup.Transport == GuidedSetupTransport.Usb;
    public bool GuidedSetupUsesWifi => guidedSetup.Transport == GuidedSetupTransport.Wifi;
    public bool IsGuidedConnectStep => guidedSetup.Step == GuidedSetupStep.Connect;
    public bool IsGuidedSelectStep => guidedSetup.Step == GuidedSetupStep.SelectDevice;
    public bool IsGuidedChoosePathStep => guidedSetup.Step == GuidedSetupStep.ChoosePath;
    public bool IsGuidedInstallStep => guidedSetup.Step == GuidedSetupStep.Install;
    public bool IsGuidedQuickSettingsStep => guidedSetup.Step == GuidedSetupStep.QuickSettings;
    public bool GuidedSetupRequiresFirmware => IsGuidedChoosePathStep
        && string.Equals(FirmwareManager.SetupPhase, "firmwareRequired", StringComparison.OrdinalIgnoreCase);
    public bool IsGuidedUsbSelectStep => IsGuidedSelectStep && GuidedSetupUsesUsb;
    public bool IsGuidedWifiSelectStep => IsGuidedSelectStep && GuidedSetupUsesWifi;
    public bool IsGuidedUsbInstallStep => IsGuidedInstallStep && GuidedSetupUsesUsb;
    public bool IsGuidedWifiInstallStep => IsGuidedInstallStep && GuidedSetupUsesWifi;
    public bool IsGuidedCompleteStep => guidedSetup.Step == GuidedSetupStep.Complete;
    public bool IsGuidedWifiJoinStep => IsGuidedWifiSelectStep
        && guidedSetup.WifiStep == GuidedWifiStep.JoinTemporaryNetwork;
    public bool IsGuidedWifiFindStep => IsGuidedWifiSelectStep
        && guidedSetup.WifiStep == GuidedWifiStep.FindDevice;
    public bool IsGuidedWifiCredentialsStep => IsGuidedWifiSelectStep
        && guidedSetup.WifiStep == GuidedWifiStep.EnterCredentials;
    public bool ShowUnknownSetupDevices => showUnknownSetupDevices;
    public IReadOnlyList<FirmwareDeviceCandidate> GuidedSetupKnownDevices =>
        FirmwareDevices.Where(GuidedSetupFlow.IsKnown).ToArray();
    public IReadOnlyList<FirmwareDeviceCandidate> GuidedSetupUnknownDevices => showUnknownSetupDevices
        ? FirmwareDevices.Where(GuidedSetupFlow.IsUnknown).ToArray()
        : [];
    public IReadOnlyList<FirmwareDeviceCandidate> GuidedSetupDevices =>
        GuidedSetupKnownDevices.Concat(GuidedSetupUnknownDevices).ToArray();
    public bool GuidedSetupHasDevices => GuidedSetupDevices.Count > 0;
    public bool GuidedSetupHasKnownDevices => GuidedSetupKnownDevices.Count > 0;
    public bool GuidedSetupHasUnknownDevices => GuidedSetupUnknownDevices.Count > 0;
    public WifiSetupState WifiSetup => state.WifiSetup;
    public IReadOnlyList<WifiSetupDevice> GuidedWifiDevices =>
        WifiSetup.Devices.Where(GuidedSetupFlow.IsDiscoverable).ToArray();
    public bool GuidedWifiHasDevices => GuidedWifiDevices.Count > 0;
    public IReadOnlyList<WifiSetupDevice> GuidedPairedWifiDevices =>
        WifiSetup.Devices.Where(device => device.Paired).ToArray();
    public bool GuidedWifiHasPairedDevice => GuidedPairedWifiDevices.Any(device =>
        string.Equals(device.DeviceId, guidedSetup.PinnedWifiDeviceId, StringComparison.Ordinal));
    public string? GuidedWifiSelectedDeviceId
    {
        get => guidedWifiDeviceId;
        set
        {
            guidedSetup.SelectWifi(value, WifiSetup.Devices);
            if (!Set(ref guidedWifiDeviceId, guidedSetup.SelectedWifiDeviceId)) return;
            Raise(nameof(GuidedWifiSelectedDevice));
            Raise(nameof(CanProvisionGuidedWifi));
            GuidedWifiProvisionCommand.Refresh();
        }
    }
    public WifiSetupDevice? GuidedWifiSelectedDevice => GuidedWifiDevices.FirstOrDefault(device =>
        string.Equals(device.DeviceId, guidedWifiDeviceId, StringComparison.Ordinal));
    public string GuidedWifiSetupCode
    {
        get => guidedWifiSetupCode;
        set
        {
            var filtered = new string((value ?? "").Where(char.IsAsciiDigit).Take(8).ToArray());
            if (!Set(ref guidedWifiSetupCode, filtered)) return;
            Raise(nameof(CanProvisionGuidedWifi));
            GuidedWifiProvisionCommand.Refresh();
        }
    }
    public string GuidedWifiSsid
    {
        get => guidedWifiSsid;
        set
        {
            var bounded = (value ?? "").Length <= 32 ? value ?? "" : (value ?? "")[..32];
            if (!Set(ref guidedWifiSsid, bounded)) return;
            Raise(nameof(CanProvisionGuidedWifi));
            GuidedWifiProvisionCommand.Refresh();
        }
    }
    public void SetGuidedWifiPassword(string password)
    {
        guidedWifiPassword = password.Length <= 63 ? password : password[..63];
        Raise(nameof(CanProvisionGuidedWifi));
        GuidedWifiProvisionCommand.Refresh();
    }
    public bool CanProvisionGuidedWifi => IsBrokerAvailable && IsGuidedWifiSelectStep
        && GuidedWifiSelectedDevice is not null
        && GuidedWifiSetupCode.Length == 8
        && GuidedWifiSetupCode.All(char.IsAsciiDigit)
        && !string.IsNullOrWhiteSpace(GuidedWifiSsid)
        && (guidedWifiPassword.Length == 0 || guidedWifiPassword.Length is >= 8 and <= 63)
        && WifiSetup.Status is not "scanning" and not "provisioning";
    public bool WifiSetupBusy => WifiSetup.Status is "scanning" or "provisioning";
    public bool CanAdvanceGuidedWifi => IsGuidedWifiJoinStep
        || (IsGuidedWifiFindStep && GuidedWifiSelectedDevice is not null);
    public string GuidedWifiStatus => WifiSetup.Message ?? WifiSetup.Status switch
    {
        "scanning" => "Looking for EchoDesk on the temporary setup network…",
        "found" => "EchoDesk found. Enter the code shown on its screen and your home Wi-Fi details.",
        "provisioning" => "Sending Wi-Fi details securely. Keep this computer joined to the temporary EchoDesk network.",
        "provisioned" => "EchoDesk joined your Wi-Fi network.",
        "error" => "Wi-Fi setup did not complete.",
        _ => "Join the temporary EchoDesk network in Windows, then select Find Wi-Fi devices.",
    };
    public string? GuidedSetupSelectedPort
    {
        get => guidedSetup.SelectedPort;
        set
        {
            guidedSetup.Select(value, FirmwareDevices);
            Raise();
            Raise(nameof(GuidedSetupSelectedDevice));
            Raise(nameof(GuidedSetupRequiresBoardConfirmation));
            Raise(nameof(GuidedSetupBoardConfirmed));
            Raise(nameof(CanContinueGuidedSetup));
            GuidedSetupContinueCommand.Refresh();
        }
    }
    public FirmwareDeviceCandidate? GuidedSetupSelectedDevice => GuidedSetupDevices.FirstOrDefault(device =>
        string.Equals(device.Port, guidedSetup.SelectedPort, StringComparison.OrdinalIgnoreCase));
    public bool GuidedSetupRequiresBoardConfirmation => guidedSetup.SelectedDeviceRequiresConfirmation;
    public bool GuidedSetupBoardConfirmed
    {
        get => guidedSetup.BoardConfirmed;
        set
        {
            guidedSetup.SetBoardConfirmed(value);
            Raise();
            Raise(nameof(CanContinueGuidedSetup));
            GuidedSetupContinueCommand.Refresh();
        }
    }
    public bool CanContinueGuidedSetup => guidedSetup.CanContinue
        && !FirmwareOperationActive && !guidedSetupFinalizing;
    public bool CanGoBackInGuidedSetup => guidedSetup.Step is GuidedSetupStep.SelectDevice or GuidedSetupStep.ChoosePath
        && !FirmwareOperationActive && !guidedSetupFinalizing;
    public bool CanRetryGuidedSetup => guidedSetup.Step == GuidedSetupStep.Install
        && (GuidedSetupUsesUsb
            ? IsFirmwareFailureScreen && guidedSetup.PinnedPort is not null && !FirmwareOperationActive
            : guidedSetup.PinnedWifiDeviceId is not null && WifiSetup.Status == "error" && !WifiSetupBusy);
    public bool CanCancelGuidedSetup => guidedSetup.Step == GuidedSetupStep.Install
        && (GuidedSetupUsesUsb
            ? FirmwareOperationActive && CanCancelFirmwareOperation
            : WifiSetup.Status == "provisioning");
    public string? GuidedSetupError
    {
        get => guidedSetupError;
        private set
        {
            if (!Set(ref guidedSetupError, value)) return;
            Raise(nameof(GuidedSetupHasError));
        }
    }
    public bool GuidedSetupHasError => !string.IsNullOrWhiteSpace(GuidedSetupError);
    public string GuidedSetupDeviceHeading => GuidedSetupSelectedDevice?.DisplayName ?? "EchoDesk Mini";
    public string GuidedSetupDeviceDetails => GuidedSetupSelectedDevice is { } device
        ? string.Join(" · ", new[]
        {
            device.HardwareModel ?? "EchoDesk Mini",
            device.SetupState is "completed" ? "Already configured" : "Ready for setup",
            device.FirmwareVersion is { Length: > 0 } version ? $"EchoDesk OS {version}" : "Firmware not reported",
            device.Port,
        })
        : "Select a compatible EchoDesk to continue.";
    public string GuidedSetupProgressLabel => FirmwareManager.ProgressPercent is { } progress
        ? $"{FirmwareStageLabel} · {progress}%" : FirmwareStageLabel;
    public string GuidedSetupCompleteHeading => "Welcome to EchoDesk!";
    public string GuidedSetupCompleteMessage =>
        "Welcome to the community. Your new EchoDesk is ready to go.";
    public string GuidedWifiConfirmationStatus => GuidedWifiHasPairedDevice
        ? $"EchoDesk found on your network{(string.IsNullOrWhiteSpace(WifiSetup.Address) ? "." : $" at {WifiSetup.Address}.")}"
        : "After reconnecting Windows to your home network, select Find Wi-Fi devices to confirm EchoDesk is reachable.";

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

    public string DefaultApp
    {
        get => defaultApp;
        set { if (Set(ref defaultApp, value) && !applyingState) SetAdvancedSettingCommand.Execute("defaultApp"); }
    }

    public string ClockFormat
    {
        get => clockFormat;
        set { if (Set(ref clockFormat, value) && !applyingState) SetAdvancedSettingCommand.Execute("clockFormat"); }
    }

    public string Timezone
    {
        get => timezone;
        set { if (Set(ref timezone, value) && !applyingState) SetAdvancedSettingCommand.Execute("timezone"); }
    }

    public string CodexPercentageMode
    {
        get => codexPercentageMode;
        set { if (Set(ref codexPercentageMode, value) && !applyingState) SetAdvancedSettingCommand.Execute("codexPercentageMode"); }
    }

    public string CodexPreferredDisplay
    {
        get => codexPreferredDisplay;
        set { if (Set(ref codexPreferredDisplay, value) && !applyingState) SetAdvancedSettingCommand.Execute("codexPreferredDisplay"); }
    }

    public string PerformanceDefaultView
    {
        get => performanceDefaultView;
        set { if (Set(ref performanceDefaultView, value) && !applyingState) SetAdvancedSettingCommand.Execute("performanceDefaultView"); }
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

    public async Task FactoryResetAsync()
    {
        var targetPort = state.Port;
        if (string.IsNullOrWhiteSpace(targetPort)) return;
        try
        {
            var next = await broker.FactoryResetAsync(targetPort);
            ApplyState(next);
            var accepted = string.Equals(next.FirmwareManager.SetupTargetPort, targetPort, StringComparison.OrdinalIgnoreCase)
                && string.Equals(next.FirmwareManager.SetupPhase, "waitingForDesktop", StringComparison.OrdinalIgnoreCase)
                && !next.FirmwareManager.SetupCompleted;
            if (!accepted) throw new InvalidDataException("EchoDesk Host did not confirm the reset transition.");
            StartSetupAfterRecovery();
            Feedback = "Factory reset accepted. EchoDesk is restarting at the setup screen.";
        }
        catch (Exception error)
        {
            Feedback = Friendly(error);
        }
    }

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
                DefaultApp = settings.DefaultApp ?? DefaultApp;
                ClockFormat = settings.ClockFormat ?? ClockFormat;
                Timezone = settings.Timezone ?? Timezone;
                CodexPercentageMode = settings.CodexPercentageMode ?? CodexPercentageMode;
                CodexPreferredDisplay = settings.CodexPreferredDisplay ?? CodexPreferredDisplay;
                PerformanceDefaultView = settings.PerformanceDefaultView ?? PerformanceDefaultView;
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

        if (IsGuidedWifiSelectStep && guidedWifiDeviceId is null)
        {
            var availableWifiDevices = next.WifiSetup.Devices.Where(GuidedSetupFlow.IsDiscoverable).ToArray();
            if (availableWifiDevices.Length == 1)
            {
                guidedSetup.SelectWifi(availableWifiDevices[0].DeviceId, next.WifiSetup.Devices);
                guidedWifiDeviceId = guidedSetup.SelectedWifiDeviceId;
            }
        }

        if (IsGuidedWifiInstallStep)
        {
            if (next.WifiSetup.Status == "provisioned"
                && guidedSetup.CompleteWifi(next.WifiSetup.Provisioned, next.WifiSetup.SelectedDeviceId))
            {
                guidedWifiPassword = "";
                guidedWifiSetupCode = "";
                GuidedSetupError = null;
                Feedback = "EchoDesk joined the Wi-Fi network.";
            }
            else if (next.WifiSetup.Status == "error")
            {
                GuidedSetupError = next.WifiSetup.Message
                    ?? "Wi-Fi setup did not complete. Check the code and network password, then try again.";
            }
        }

        foreach (var name in new[]
                 {
                     nameof(State), nameof(Device), nameof(IsConnected), nameof(IsBrokerAvailable),
                     nameof(ConnectionLabel), nameof(ConnectionDetail), nameof(DeviceName), nameof(Port),
                     nameof(Firmware), nameof(Hardware), nameof(ActiveApp), nameof(Appearance), nameof(SupportsSettings),
                     nameof(SupportsDefaultApp), nameof(SupportsClockFormat), nameof(SupportsTimezone),
                     nameof(SupportsCodexPercentageMode), nameof(SupportsCodexPreferredDisplay),
                     nameof(SupportsPerformanceDefaultView),
                     nameof(AiUsageAvailable), nameof(AiUsageEnabled), nameof(AiUsageFetching), nameof(AiUsageStatus),
                     nameof(AiUsageProvider), nameof(AiUsageLastUpdated), nameof(AiUsageGuidance), nameof(AiUsageNeedsAttention),
                     nameof(FirmwareManager), nameof(FirmwareDevices), nameof(SelectedFirmwarePort), nameof(SelectedFirmwareDevice),
                     nameof(WifiSetup), nameof(GuidedWifiDevices), nameof(GuidedWifiHasDevices),
                     nameof(GuidedPairedWifiDevices), nameof(GuidedWifiHasPairedDevice),
                     nameof(GuidedWifiSelectedDeviceId), nameof(GuidedWifiSelectedDevice),
                     nameof(GuidedWifiStatus), nameof(GuidedWifiConfirmationStatus),
                     nameof(WifiSetupBusy), nameof(CanProvisionGuidedWifi),
                 }) Raise(name);
        RaiseFirmwareComputed();
        RaiseGuidedSetupComputed();
        RefreshCommands();
        if (IsConnected) Feedback = "Device state is synchronised.";

        if (IsGuidedSetup && IsGuidedUsbInstallStep && guidedUpdateRequested
            && IsFirmwareSuccessScreen && guidedSetup.FinishInstall(
                next.FirmwareManager.WriteCompleted, next.FirmwareManager.Reconnected))
        {
            guidedUpdateRequested = false;
            RaiseGuidedSetupComputed();
        }
    }

    private async Task ContinueGuidedSetupAsync()
    {
        GuidedSetupError = null;
        if (guidedSetup.Transport != GuidedSetupTransport.Usb
            || guidedSetup.Step != GuidedSetupStep.SelectDevice || guidedSetup.SelectedPort is not { } port)
            return;

        // Stop background scans before the existing updater takes exclusive
        // ownership of the selected serial port for its hardware probe.
        StopGuidedDiscovery();
        try
        {
            var selected = await broker.SelectFirmwareDeviceAsync(
                port, "EchoDesk Mini", guidedSetup.BoardConfirmed);
            ApplyState(selected);
            if (!string.Equals(selected.FirmwareManager.SelectedPort, port, StringComparison.OrdinalIgnoreCase)
                || !selected.FirmwareManager.BoardConfirmed)
            {
                throw new InvalidDataException("EchoDesk Host did not verify and retain the selected device identity.");
            }
            var begun = await broker.BeginGuidedSetupAsync(port);
            ApplyState(begun);
            if (!string.Equals(begun.FirmwareManager.SelectedPort, port, StringComparison.OrdinalIgnoreCase)
                || !string.Equals(begun.FirmwareManager.SetupTargetPort, port, StringComparison.OrdinalIgnoreCase)
                || begun.FirmwareManager.SetupPhase is not ("ready" or "firmwareRequired"))
                throw new InvalidDataException("EchoDesk Host did not retain the selected device identity.");
            if (!guidedSetup.ConfirmDevice()) return;
            StopGuidedDiscovery();
            RaiseGuidedSetupComputed();
            Feedback = "EchoDesk verified. Choose how you want to continue.";
        }
        catch (Exception error)
        {
            GuidedSetupError = FriendlyFirmwareError(error);
            Feedback = GuidedSetupError;
            if (guidedSetup.Step == GuidedSetupStep.SelectDevice) StartGuidedDiscovery();
            RaiseGuidedSetupComputed();
        }
    }

    private void MoveGuidedSetupBack()
    {
        if (!guidedSetup.MoveBack()) return;
        if (guidedSetup.Step == GuidedSetupStep.SelectDevice
            && (GuidedSetupUsesUsb || guidedSetup.WifiStep == GuidedWifiStep.FindDevice))
            StartGuidedDiscovery();
        else
            StopGuidedDiscovery();
        if (guidedSetup.Step == GuidedSetupStep.Connect)
        {
            guidedWifiPassword = "";
            GuidedWifiSetupCode = "";
            guidedWifiDeviceId = null;
        }
        RaiseGuidedSetupComputed();
    }

    private void ChooseGuidedSetupTransport(object? parameter)
    {
        if (guidedSetup.Step != GuidedSetupStep.Connect) return;
        var transport = string.Equals(parameter as string, "wifi", StringComparison.OrdinalIgnoreCase)
            ? GuidedSetupTransport.Wifi
            : GuidedSetupTransport.Usb;
        guidedSetup.SelectTransport(transport);
        guidedWifiDeviceId = null;
        GuidedSetupError = null;
        if (transport == GuidedSetupTransport.Usb) StartGuidedDiscovery();
        RaiseGuidedSetupComputed();
    }

    private async Task ChooseGuidedSetupPathAsync(object? parameter)
    {
        if (guidedSetup.Step != GuidedSetupStep.ChoosePath) return;
        GuidedSetupError = null;
        if (string.Equals(parameter as string, "updates", StringComparison.OrdinalIgnoreCase))
        {
            if (!guidedSetup.StartUpdateCheck()) return;
            guidedUpdateRequested = true;
            RaiseGuidedSetupComputed();
            try
            {
                var checkedState = await broker.CheckFirmwareUpdatesAsync();
                ApplyState(checkedState);
                if (checkedState.FirmwareManager.Status == "updateAvailable")
                {
                    ApplyState(await broker.StartFirmwareOperationAsync("update"));
                }
                else if (checkedState.FirmwareManager.Status is "upToDate" or "ready")
                {
                    guidedUpdateRequested = false;
                    guidedSetup.SkipInstallWhenCurrent();
                    Feedback = "EchoDesk OS is up to date.";
                    RaiseGuidedSetupComputed();
                }
            }
            catch (Exception error)
            {
                GuidedSetupError = FriendlyFirmwareError(error);
            }
            return;
        }

        if (guidedSetup.GetStartedNow())
        {
            Feedback = "EchoDesk is ready for a few quick settings.";
            RaiseGuidedSetupComputed();
        }
    }

    private bool CanChooseGuidedSetupPath(string? option)
    {
        if (guidedSetup.Step != GuidedSetupStep.ChoosePath || guidedSetupFinalizing) return false;
        return string.Equals(option, "updates", StringComparison.OrdinalIgnoreCase)
            || (string.Equals(option, "now", StringComparison.OrdinalIgnoreCase)
                && string.Equals(FirmwareManager.SetupPhase, "ready", StringComparison.OrdinalIgnoreCase));
    }

    private async Task RefreshGuidedDevicesAsync()
    {
        try
        {
            var next = guidedSetup.Transport == GuidedSetupTransport.Wifi
                ? await broker.DiscoverWifiSetupDevicesAsync()
                : await broker.ScanFirmwareDevicesAsync(showUnknownSetupDevices);
            ApplyState(next);
            GuidedSetupError = null;
        }
        catch (Exception error)
        {
            GuidedSetupError = guidedSetup.Transport == GuidedSetupTransport.Wifi
                ? Friendly(error)
                : FriendlyFirmwareError(error);
        }
        RaiseGuidedSetupComputed();
    }

    private async Task RevealUnknownSetupDevicesAsync()
    {
        showUnknownSetupDevices = true;
        RaiseGuidedSetupComputed();
        await RefreshGuidedDevicesAsync();
    }

    private async Task AdvanceGuidedWifiAsync()
    {
        if (!CanAdvanceGuidedWifi || !guidedSetup.AdvanceWifi()) return;
        if (IsGuidedWifiFindStep)
        {
            StartGuidedDiscovery();
            await RefreshGuidedDevicesAsync();
        }
        else
        {
            StopGuidedDiscovery();
        }
        RaiseGuidedSetupComputed();
    }

    private void StartGuidedDiscovery()
    {
        if (guidedDiscovery is not null) return;
        guidedDiscovery = new CancellationTokenSource();
        _ = GuidedDiscoveryLoopAsync(guidedDiscovery.Token);
    }

    private void StopGuidedDiscovery()
    {
        guidedDiscovery?.Cancel();
        guidedDiscovery?.Dispose();
        guidedDiscovery = null;
    }

    private async Task GuidedDiscoveryLoopAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested
               && IsGuidedSetup && guidedSetup.Step == GuidedSetupStep.SelectDevice
               && (GuidedSetupUsesUsb || guidedSetup.WifiStep == GuidedWifiStep.FindDevice))
        {
            await RefreshGuidedDevicesAsync();
            try
            {
                var delay = guidedSetup.Transport == GuidedSetupTransport.Wifi ? 4 : 2;
                await Task.Delay(TimeSpan.FromSeconds(delay), cancellationToken);
            }
            catch (OperationCanceledException) { break; }
        }
    }

    private async Task ProvisionGuidedWifiAsync()
    {
        if (!CanProvisionGuidedWifi || GuidedWifiSelectedDeviceId is not { } deviceId) return;
        var setupCode = GuidedWifiSetupCode;
        var ssid = GuidedWifiSsid.Trim();
        var password = guidedWifiPassword;
        GuidedSetupError = null;
        StopGuidedDiscovery();
        if (!guidedSetup.BeginWifiProvisioning()) return;
        RaiseGuidedSetupComputed();
        try
        {
            var next = await broker.ProvisionWifiSetupDeviceAsync(deviceId, setupCode, ssid, password);
            ApplyState(next);
            Feedback = "Wi-Fi details sent. Waiting for EchoDesk to join the network…";
        }
        catch (Exception error)
        {
            GuidedSetupError = Friendly(error);
            Feedback = GuidedSetupError;
            RaiseGuidedSetupComputed();
        }
    }

    private async Task RetryGuidedSetupAsync()
    {
        if (!CanRetryGuidedSetup) return;
        GuidedSetupError = null;
        try
        {
            var next = GuidedSetupUsesWifi
                ? await broker.ProvisionWifiSetupDeviceAsync(
                    guidedSetup.PinnedWifiDeviceId!, GuidedWifiSetupCode, GuidedWifiSsid.Trim(), guidedWifiPassword)
                : await broker.StartFirmwareOperationAsync("update");
            ApplyState(next);
        }
        catch (Exception error)
        {
            GuidedSetupError = FriendlyFirmwareError(error);
        }
    }

    private async Task CancelGuidedSetupAsync()
    {
        if (!CanCancelGuidedSetup) return;
        try
        {
            var next = GuidedSetupUsesWifi
                ? await broker.CancelWifiSetupAsync()
                : await broker.CancelFirmwareOperationAsync();
            ApplyState(next);
            GuidedSetupError = "Setup was cancelled safely. Nothing is reported as complete.";
        }
        catch (Exception error)
        {
            GuidedSetupError = FriendlyFirmwareError(error);
        }
    }

    private async Task CompleteGuidedQuickSettingsAsync()
    {
        var port = guidedSetup.PinnedPort;
        if (guidedSetup.Step != GuidedSetupStep.QuickSettings || guidedSetupFinalizing) return;
        guidedSetupFinalizing = true;
        RaiseGuidedSetupComputed();
        try
        {
            if (GuidedSetupUsesUsb)
            {
                if (port is null || !string.Equals(state.Port, port, StringComparison.OrdinalIgnoreCase) || !state.IsConnected)
                    throw new IOException("The selected EchoDesk has not reconnected yet.");
                var settings = await broker.SetSettingsAsync(
                    Device?.SettingsRevision ?? 0,
                    new SettingsPatch { Theme = DeviceTheme, ClockFormat = ClockFormat },
                    true);
                ApplyState(settings);
                var completed = await broker.CompleteGuidedSetupAsync(port);
                ApplyState(completed);
                var acknowledged = completed.FirmwareManager.SetupCompleted
                    || string.Equals(completed.Device?.SetupState, "completed", StringComparison.OrdinalIgnoreCase);
                if (!acknowledged) throw new InvalidDataException("EchoDesk did not confirm setup completion.");
            }
            if (!guidedSetup.Complete()) throw new InvalidDataException("Setup could not be completed from this step.");
            GuidedSetupError = null;
            Feedback = "Your EchoDesk is ready.";
        }
        catch (Exception error)
        {
            GuidedSetupError = FriendlyFirmwareError(error);
        }
        finally
        {
            guidedSetupFinalizing = false;
            RaiseGuidedSetupComputed();
        }
    }

    private void FinishGuidedSetup()
    {
        SelectedPage = "Overview";
        guidedSetup.Reset();
        guidedWifiDeviceId = null;
        guidedWifiSetupCode = "";
        guidedWifiSsid = "";
        guidedWifiPassword = "";
        showUnknownSetupDevices = false;
        guidedUpdateRequested = false;
        GuidedSetupError = null;
        RaiseGuidedSetupComputed();
    }

    private void StartSetupAfterRecovery()
    {
        guidedSetup.Reset();
        showUnknownSetupDevices = false;
        GuidedSetupError = null;
        SelectedPage = "Set up a new device";
        RaiseGuidedSetupComputed();
    }

    private void ContinueFirstRun()
    {
        if (!firstRun.Continue()) return;
        Raise(nameof(IsFirstRunWelcome));
        Raise(nameof(IsFirstRunSetupPrompt));
        FirstRunContinueCommand.Refresh();
        FirstRunChoiceCommand.Refresh();
    }

    private void CompleteFirstRun(object? choice)
    {
        if (!firstRun.Finish()) return;
        preferences = preferences with { FirstRunCompleted = true };
        SavePreferences();
        if (string.Equals(choice as string, "yes", StringComparison.OrdinalIgnoreCase))
            StartSetupAfterRecovery();
        else
            SelectedPage = "Overview";
        Raise(nameof(IsFirstRunWelcome));
        Raise(nameof(IsFirstRunSetupPrompt));
        FirstRunContinueCommand.Refresh();
        FirstRunChoiceCommand.Refresh();
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
            () => mode == "manualRecovery"
                ? broker.StartManualRecoveryAsync(SelectedFirmwarePort!)
                : broker.StartFirmwareOperationAsync(mode),
            mode == "manualRecovery"
                ? "Manual recovery started. Keep the selected ESP32 connected."
                : "Firmware installation started. Keep EchoDesk connected.");
    }

    private bool CanRunFirmwareRecoveryAction(string? action) => action switch
    {
        "recovery" => CanStartManualRecovery,
        "update" or "resetInstall" or "repair" => CanUseFirmwareActions,
        _ => false,
    };

    private async Task RunFirmwareRecoveryActionAsync(object? parameter)
    {
        var action = parameter as string;
        if (!CanRunFirmwareRecoveryAction(action)) return;
        try
        {
            BrokerState next;
            if (action == "update")
            {
                next = await broker.CheckFirmwareUpdatesAsync();
                ApplyState(next);
                if (next.FirmwareManager.Status != "updateAvailable")
                {
                    Feedback = "EchoDesk OS is already up to date.";
                    return;
                }
                next = await broker.StartFirmwareOperationAsync("update");
            }
            else if (action == "recovery")
            {
                next = await broker.StartManualRecoveryAsync(SelectedFirmwarePort!);
            }
            else
            {
                var mode = action == "resetInstall" ? "reinstall" : "repair";
                next = await broker.StartFirmwareOperationAsync(mode);
            }
            ApplyState(next);
            Feedback = "EchoDesk OS operation started. Keep the USB cable connected.";
        }
        catch (Exception error)
        {
            Feedback = FriendlyFirmwareError(error);
        }
    }

    private bool CanStartFirmwareOperation(string? mode)
    {
        if (!IsBrokerAvailable || !FirmwareManager.Available || FirmwareOperationActive
            || FirmwareManager.Status == "checking") return false;
        if (mode == "manualRecovery") return CanStartManualRecovery;
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

    private Task SetAdvancedSettingAsync(object? parameter)
    {
        if (parameter is not string setting || !SupportsDeviceSetting(setting)) return Task.CompletedTask;
        var patch = setting switch
        {
            "defaultApp" => new SettingsPatch { DefaultApp = DefaultApp },
            "clockFormat" => new SettingsPatch { ClockFormat = ClockFormat },
            "timezone" => new SettingsPatch { Timezone = Timezone },
            "codexPercentageMode" => new SettingsPatch { CodexPercentageMode = CodexPercentageMode },
            "codexPreferredDisplay" => new SettingsPatch { CodexPreferredDisplay = CodexPreferredDisplay },
            "performanceDefaultView" => new SettingsPatch { PerformanceDefaultView = PerformanceDefaultView },
            _ => null,
        };
        return patch is null
            ? Task.CompletedTask
            : SetSettingsAsync(patch, true, "Device customisation updated.");
    }

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

    private bool SupportsDeviceSetting(string setting) => IsConnected && SupportsSettings
        && Device?.UnsupportedSettings.Contains(setting, StringComparer.OrdinalIgnoreCase) != true;

    private void RefreshCommands()
    {
        RefreshCommand.Refresh(); ReconnectCommand.Refresh(); OpenAppCommand.Refresh();
        SetThemeCommand.Refresh(); SetNowPlayingCommand.Refresh(); SetAdvancedSettingCommand.Refresh();
        ToggleAiUsageCommand.Refresh();
        ScanFirmwareDevicesCommand.Refresh(); RevealFirmwareUnknownCommand.Refresh(); SelectFirmwareDeviceCommand.Refresh();
        CheckFirmwareUpdatesCommand.Refresh(); InstallFirmwareCommand.Refresh();
        FirmwareRecoveryActionCommand.Refresh();
        CancelFirmwareOperationCommand.Refresh(); ToggleUpdateNotificationsCommand.Refresh();
        GuidedSetupContinueCommand.Refresh(); GuidedSetupBackCommand.Refresh();
        GuidedSetupRefreshCommand.Refresh(); GuidedSetupRetryCommand.Refresh();
        GuidedSetupCancelCommand.Refresh(); GuidedSetupChooseTransportCommand.Refresh();
        GuidedSetupChoiceCommand.Refresh(); GuidedSetupRevealUnknownCommand.Refresh();
        GuidedWifiNextCommand.Refresh(); GuidedWifiProvisionCommand.Refresh();
        CompleteGuidedQuickSettingsCommand.Refresh();
    }

    private void RaiseFirmwareComputed()
    {
        foreach (var name in new[]
                 {
                     nameof(SelectedFirmwareDevice), nameof(FirmwareOperationActive), nameof(IsFirmwareConnectScreen), nameof(IsFirmwareOverviewScreen),
                     nameof(IsFirmwareReadyScreen), nameof(IsFirmwareInstallingScreen), nameof(IsFirmwareSuccessScreen),
                     nameof(IsFirmwarePartialScreen), nameof(IsFirmwareFailureScreen), nameof(ShowBoardConfirmation),
                     nameof(CanSelectFirmwareDevice), nameof(CanStartManualRecovery), nameof(FirmwareTargetSelected),
                     nameof(CanUseFirmwareActions), nameof(FirmwareConnectionHeading), nameof(FirmwareConnectionMessage),
                     nameof(FirmwareDeviceDetails), nameof(FirmwareCurrentVersion), nameof(FirmwareAvailableVersion),
                     nameof(FirmwareReleaseNotes), nameof(FirmwareStageLabel), nameof(FirmwareProgressLabel),
                     nameof(FirmwareOperationMessage), nameof(FirmwareErrorMessage), nameof(FirmwareHasMeasuredProgress),
                     nameof(FirmwareHasNoMeasuredProgress), nameof(FirmwareUpdateAvailable), nameof(FirmwareIsUpToDate),
                     nameof(UpdateNotificationsEnabled),
                 }) Raise(name);
        RefreshCommands();
    }

    private void RaiseGuidedSetupComputed()
    {
        foreach (var name in new[]
                 {
                     nameof(CurrentGuidedSetupStep), nameof(IsGuidedConnectStep),
                     nameof(CurrentGuidedSetupTransport), nameof(GuidedSetupUsesUsb), nameof(GuidedSetupUsesWifi),
                     nameof(IsGuidedSelectStep), nameof(IsGuidedChoosePathStep), nameof(IsGuidedInstallStep),
                     nameof(IsGuidedQuickSettingsStep), nameof(GuidedSetupRequiresFirmware), nameof(IsGuidedCompleteStep),
                     nameof(IsGuidedUsbSelectStep), nameof(IsGuidedWifiSelectStep),
                     nameof(IsGuidedUsbInstallStep), nameof(IsGuidedWifiInstallStep),
                     nameof(IsGuidedWifiJoinStep), nameof(IsGuidedWifiFindStep), nameof(IsGuidedWifiCredentialsStep),
                     nameof(ShowUnknownSetupDevices), nameof(GuidedSetupKnownDevices), nameof(GuidedSetupUnknownDevices),
                     nameof(GuidedSetupDevices), nameof(GuidedSetupHasDevices), nameof(GuidedSetupHasKnownDevices),
                     nameof(GuidedSetupHasUnknownDevices),
                     nameof(GuidedSetupSelectedPort), nameof(GuidedSetupSelectedDevice),
                     nameof(WifiSetup), nameof(GuidedWifiDevices), nameof(GuidedWifiHasDevices),
                     nameof(GuidedPairedWifiDevices), nameof(GuidedWifiHasPairedDevice),
                     nameof(GuidedWifiSelectedDeviceId), nameof(GuidedWifiSelectedDevice),
                     nameof(GuidedWifiSetupCode), nameof(GuidedWifiSsid), nameof(CanProvisionGuidedWifi), nameof(CanAdvanceGuidedWifi),
                     nameof(GuidedWifiStatus), nameof(GuidedWifiConfirmationStatus), nameof(WifiSetupBusy),
                     nameof(GuidedSetupRequiresBoardConfirmation), nameof(GuidedSetupBoardConfirmed),
                     nameof(CanContinueGuidedSetup), nameof(CanGoBackInGuidedSetup), nameof(CanRetryGuidedSetup),
                     nameof(CanCancelGuidedSetup),
                     nameof(GuidedSetupDeviceHeading), nameof(GuidedSetupDeviceDetails),
                     nameof(GuidedSetupProgressLabel), nameof(GuidedSetupCompleteHeading),
                     nameof(GuidedSetupCompleteMessage), nameof(GuidedSetupHasError),
                 }) Raise(name);
        RefreshCommands();
    }

    private static string FriendlyFirmwareStage(string? stage) => stage switch
    {
        "preparing" => "Preparing device",
        "checking_latest" => "Checking for latest EchoDesk OS",
        "downloading" => "Downloading firmware",
        "verifying_firmware" => "Verifying firmware",
        "entering_bootloader" => "Entering bootloader",
        "writing" => "Writing firmware",
        "verifying_installation" => "Verifying installation",
        "restarting" => "Restarting",
        "reconnecting" => "Connecting to EchoDesk",
        "finishing_setup" => "Finishing setup",
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
        StopGuidedDiscovery();
        await broker.DisposeAsync();
    }
}

public sealed record AppTile(string Id, string Label, string Glyph);
public sealed record Choice(string Value, string Label);
