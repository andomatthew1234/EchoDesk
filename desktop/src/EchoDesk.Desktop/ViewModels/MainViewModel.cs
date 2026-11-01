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
    private readonly GuidedSetupLegalDocuments guidedSetupLegalDocuments;
    private readonly GuidedSetupFlow guidedSetup;
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

    public MainViewModel(
        BrokerClient broker,
        PreferencesService preferencesService,
        GuidedSetupLegalDocuments? setupLegalDocuments = null)
    {
        this.broker = broker;
        this.preferencesService = preferencesService;
        uiContext = SynchronizationContext.Current ?? new SynchronizationContext();
        guidedSetupLegalDocuments = setupLegalDocuments
            ?? GuidedSetupLegalDocuments.Discover(AppContext.BaseDirectory);
        guidedSetup = new GuidedSetupFlow(guidedSetupLegalDocuments);
        preferences = preferencesService.Load();
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
        GuidedWifiProvisionCommand = new AsyncRelayCommand(
            _ => ProvisionGuidedWifiAsync(), _ => CanProvisionGuidedWifi);
        OpenWindowsWifiSettingsCommand = new RelayCommand(_ =>
            Process.Start(new ProcessStartInfo("ms-settings:network-wifi") { UseShellExecute = true }));
        FinishGuidedSetupCommand = new RelayCommand(_ => FinishGuidedSetup());
        OpenSetupDocumentCommand = new RelayCommand(OpenSetupDocument, CanOpenSetupDocument);
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
    public AsyncRelayCommand SelectFirmwareDeviceCommand { get; }
    public AsyncRelayCommand CheckFirmwareUpdatesCommand { get; }
    public AsyncRelayCommand InstallFirmwareCommand { get; }
    public AsyncRelayCommand CancelFirmwareOperationCommand { get; }
    public AsyncRelayCommand ToggleUpdateNotificationsCommand { get; }
    public AsyncRelayCommand GuidedSetupContinueCommand { get; }
    public RelayCommand GuidedSetupBackCommand { get; }
    public AsyncRelayCommand GuidedSetupRefreshCommand { get; }
    public AsyncRelayCommand GuidedSetupRetryCommand { get; }
    public AsyncRelayCommand GuidedSetupCancelCommand { get; }
    public RelayCommand GuidedSetupChooseTransportCommand { get; }
    public AsyncRelayCommand GuidedWifiProvisionCommand { get; }
    public RelayCommand OpenWindowsWifiSettingsCommand { get; }
    public RelayCommand FinishGuidedSetupCommand { get; }
    public RelayCommand OpenSetupDocumentCommand { get; }
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
            Raise(nameof(IsGuidedSetup));
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
    public bool IsDeviceControl => SelectedPage == "Device Control";
    public bool IsCustomisation => SelectedPage == "Customisation";
    public bool IsSetupUpdates => SelectedPage == "Setup & Updates";
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
    public bool CanStartManualRecovery => IsBrokerAvailable && FirmwareManager.Available
        && !FirmwareOperationActive && FirmwareManager.Status != "checking"
        && SelectedFirmwareDevice is not null && !string.IsNullOrWhiteSpace(SelectedFirmwarePort);
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
    public bool CanCancelFirmwareOperation => FirmwareManager.Stage is not "writing" and not "verifying_installation";

    public GuidedSetupStep CurrentGuidedSetupStep => guidedSetup.Step;
    public GuidedSetupTransport CurrentGuidedSetupTransport => guidedSetup.Transport;
    public bool GuidedSetupUsesUsb => guidedSetup.Transport == GuidedSetupTransport.Usb;
    public bool GuidedSetupUsesWifi => guidedSetup.Transport == GuidedSetupTransport.Wifi;
    public bool IsGuidedTermsStep => guidedSetup.Step == GuidedSetupStep.Terms;
    public bool IsGuidedConnectStep => guidedSetup.Step == GuidedSetupStep.Connect;
    public bool IsGuidedSelectStep => guidedSetup.Step == GuidedSetupStep.SelectDevice;
    public bool IsGuidedInstallStep => guidedSetup.Step == GuidedSetupStep.Install;
    public bool IsGuidedUsbSelectStep => IsGuidedSelectStep && GuidedSetupUsesUsb;
    public bool IsGuidedWifiSelectStep => IsGuidedSelectStep && GuidedSetupUsesWifi;
    public bool IsGuidedUsbInstallStep => IsGuidedInstallStep && GuidedSetupUsesUsb;
    public bool IsGuidedWifiInstallStep => IsGuidedInstallStep && GuidedSetupUsesWifi;
    public bool IsGuidedCompleteStep => guidedSetup.Step == GuidedSetupStep.Complete;
    public bool GuidedSetupHasLegalDocuments => guidedSetupLegalDocuments.HasRequiredDocuments;
    public bool GuidedSetupTermsAvailable => guidedSetupLegalDocuments.HasTerms;
    public bool GuidedSetupPrivacyAvailable => guidedSetupLegalDocuments.HasPrivacy;
    public bool GuidedSetupNoticesAvailable => !string.IsNullOrWhiteSpace(guidedSetupLegalDocuments.ThirdPartyNoticesPath)
                                               && File.Exists(guidedSetupLegalDocuments.ThirdPartyNoticesPath);
    public string GuidedSetupLegalStatus => GuidedSetupHasLegalDocuments
        ? "Review both documents before accepting."
        : "Setup is unavailable in this build because official EchoDesk Terms and Privacy documents have not been supplied.";
    public bool GuidedSetupTermsAccepted
    {
        get => guidedSetup.TermsAccepted;
        set
        {
            guidedSetup.SetTermsAccepted(value);
            Raise();
            Raise(nameof(CanContinueGuidedSetup));
            GuidedSetupContinueCommand.Refresh();
        }
    }
    public IReadOnlyList<FirmwareDeviceCandidate> GuidedSetupDevices =>
        FirmwareDevices.Where(GuidedSetupFlow.IsDiscoverable).ToArray();
    public bool GuidedSetupHasDevices => GuidedSetupDevices.Count > 0;
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
    public bool CanGoBackInGuidedSetup => guidedSetup.Step is GuidedSetupStep.Connect or GuidedSetupStep.SelectDevice
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
    public string GuidedSetupCompleteHeading => GuidedSetupUsesWifi
        ? "EchoDesk is on Wi-Fi"
        : "Your EchoDesk is ready";
    public string GuidedSetupCompleteMessage => GuidedSetupUsesWifi
        ? $"EchoDesk accepted the details for {WifiSetup.ConnectedSsid ?? "your Wi-Fi network"}. Reconnect this computer to that network, then find EchoDesk again to confirm it is reachable. USB remains available."
        : "EchoDesk OS is installed, verified and connected. You can now customise the display and use the desktop companion.";
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

    public Task FactoryResetAsync() => string.IsNullOrWhiteSpace(Port)
        ? Task.CompletedTask
        : ExecuteAsync(
            () => broker.FactoryResetAsync(Port),
            "Factory reset accepted. EchoDesk is returning to the Hello screen.");

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

        if (IsGuidedSetup && guidedSetup.Step == GuidedSetupStep.Install
            && IsFirmwareSuccessScreen && !guidedSetupFinalizing)
        {
            _ = FinalizeGuidedSetupAsync();
        }
    }

    private async Task ContinueGuidedSetupAsync()
    {
        GuidedSetupError = null;
        if (guidedSetup.Step is GuidedSetupStep.Terms or GuidedSetupStep.Connect)
        {
            if (!guidedSetup.MoveNext()) return;
            if (guidedSetup.Step == GuidedSetupStep.SelectDevice) StartGuidedDiscovery();
            RaiseGuidedSetupComputed();
            return;
        }
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
            if (!string.Equals(begun.FirmwareManager.SelectedPort, port, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("EchoDesk Host did not retain the selected device identity.");
            if (!guidedSetup.MoveNext()) return;
            StopGuidedDiscovery();
            RaiseGuidedSetupComputed();
            var installing = await broker.StartFirmwareOperationAsync("install");
            ApplyState(installing);
            Feedback = "Guided setup started. Keep this EchoDesk connected.";
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
        if (guidedSetup.Step != GuidedSetupStep.SelectDevice) StopGuidedDiscovery();
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
        if (!guidedSetup.MoveNext()) return;
        StartGuidedDiscovery();
        RaiseGuidedSetupComputed();
    }

    private async Task RefreshGuidedDevicesAsync()
    {
        try
        {
            var next = guidedSetup.Transport == GuidedSetupTransport.Wifi
                ? await broker.DiscoverWifiSetupDevicesAsync()
                : await broker.ScanFirmwareDevicesAsync();
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
               && IsGuidedSetup && guidedSetup.Step == GuidedSetupStep.SelectDevice)
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
        if (!guidedSetup.MoveNext()) return;
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
                : await broker.StartFirmwareOperationAsync("install");
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

    private async Task FinalizeGuidedSetupAsync()
    {
        var port = guidedSetup.PinnedPort;
        if (port is null || guidedSetupFinalizing) return;
        guidedSetupFinalizing = true;
        RaiseGuidedSetupComputed();
        try
        {
            if (!string.Equals(state.Port, port, StringComparison.OrdinalIgnoreCase) || !state.IsConnected)
                throw new IOException("The selected EchoDesk has not reconnected yet.");
            var completed = await broker.CompleteGuidedSetupAsync(port);
            ApplyState(completed);
            var acknowledged = string.Equals(
                completed.Device?.SetupState, "completed", StringComparison.OrdinalIgnoreCase);
            if (!guidedSetup.Complete(
                    completed.FirmwareManager.WriteCompleted,
                    completed.FirmwareManager.Reconnected && completed.IsConnected,
                    acknowledged))
            {
                throw new InvalidDataException("EchoDesk did not confirm setup completion.");
            }
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
        GuidedSetupError = null;
        RaiseGuidedSetupComputed();
    }

    private bool CanOpenSetupDocument(object? parameter) => SetupDocumentPath(parameter as string) is { } path
        && File.Exists(path);

    private void OpenSetupDocument(object? parameter)
    {
        var path = SetupDocumentPath(parameter as string);
        if (path is null || !File.Exists(path)) return;
        Process.Start(new ProcessStartInfo(path) { UseShellExecute = true });
    }

    private string? SetupDocumentPath(string? name) => name switch
    {
        "terms" => guidedSetupLegalDocuments.TermsPath,
        "privacy" => guidedSetupLegalDocuments.PrivacyPath,
        "notices" => guidedSetupLegalDocuments.ThirdPartyNoticesPath,
        _ => null,
    };

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
        ScanFirmwareDevicesCommand.Refresh(); SelectFirmwareDeviceCommand.Refresh();
        CheckFirmwareUpdatesCommand.Refresh(); InstallFirmwareCommand.Refresh();
        CancelFirmwareOperationCommand.Refresh(); ToggleUpdateNotificationsCommand.Refresh();
        GuidedSetupContinueCommand.Refresh(); GuidedSetupBackCommand.Refresh();
        GuidedSetupRefreshCommand.Refresh(); GuidedSetupRetryCommand.Refresh();
        GuidedSetupCancelCommand.Refresh(); GuidedSetupChooseTransportCommand.Refresh();
        GuidedWifiProvisionCommand.Refresh(); OpenSetupDocumentCommand.Refresh();
    }

    private void RaiseFirmwareComputed()
    {
        foreach (var name in new[]
                 {
                     nameof(SelectedFirmwareDevice), nameof(FirmwareOperationActive), nameof(IsFirmwareConnectScreen),
                     nameof(IsFirmwareReadyScreen), nameof(IsFirmwareInstallingScreen), nameof(IsFirmwareSuccessScreen),
                     nameof(IsFirmwarePartialScreen), nameof(IsFirmwareFailureScreen), nameof(ShowBoardConfirmation),
                     nameof(CanSelectFirmwareDevice), nameof(CanStartManualRecovery), nameof(FirmwareConnectionHeading), nameof(FirmwareConnectionMessage),
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
                     nameof(CurrentGuidedSetupStep), nameof(IsGuidedTermsStep), nameof(IsGuidedConnectStep),
                     nameof(CurrentGuidedSetupTransport), nameof(GuidedSetupUsesUsb), nameof(GuidedSetupUsesWifi),
                     nameof(IsGuidedSelectStep), nameof(IsGuidedInstallStep), nameof(IsGuidedCompleteStep),
                     nameof(IsGuidedUsbSelectStep), nameof(IsGuidedWifiSelectStep),
                     nameof(IsGuidedUsbInstallStep), nameof(IsGuidedWifiInstallStep),
                     nameof(GuidedSetupTermsAccepted), nameof(GuidedSetupDevices), nameof(GuidedSetupHasDevices),
                     nameof(GuidedSetupSelectedPort), nameof(GuidedSetupSelectedDevice),
                     nameof(WifiSetup), nameof(GuidedWifiDevices), nameof(GuidedWifiHasDevices),
                     nameof(GuidedPairedWifiDevices), nameof(GuidedWifiHasPairedDevice),
                     nameof(GuidedWifiSelectedDeviceId), nameof(GuidedWifiSelectedDevice),
                     nameof(GuidedWifiSetupCode), nameof(GuidedWifiSsid), nameof(CanProvisionGuidedWifi),
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
