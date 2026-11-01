using System.Text.Json.Serialization;

namespace EchoDesk.Desktop.Core.Models;

public sealed record BrokerState
{
    [JsonPropertyName("brokerRevision")]
    public ulong BrokerRevision { get; init; }

    [JsonPropertyName("brokerAvailable")]
    public bool BrokerAvailable { get; init; }

    [JsonPropertyName("connection")]
    public string Connection { get; init; } = "disconnected";

    [JsonPropertyName("port")]
    public string? Port { get; init; }

    [JsonPropertyName("device")]
    public EchoDeskDevice? Device { get; init; }

    [JsonPropertyName("lastError")]
    public string? LastError { get; init; }

    [JsonPropertyName("aiUsage")]
    public AiUsageComponentState AiUsage { get; init; } = AiUsageComponentState.Unavailable;

    [JsonPropertyName("firmwareManager")]
    public FirmwareManagerState FirmwareManager { get; init; } = FirmwareManagerState.Unavailable;

    [JsonPropertyName("wifiSetup")]
    public WifiSetupState WifiSetup { get; init; } = WifiSetupState.Unavailable;

    [JsonIgnore]
    public bool IsConnected => string.Equals(Connection, "connected", StringComparison.OrdinalIgnoreCase)
                               && Device is not null;

    public static BrokerState Offline(string? error = null) => new()
    {
        BrokerAvailable = false,
        Connection = "disconnected",
        LastError = error,
    };
}

public sealed record WifiSetupState
{
    public static WifiSetupState Unavailable { get; } = new();

    public bool Available { get; init; }
    public string Status { get; init; } = "unavailable";
    public string? Message { get; init; }
    public string? ErrorCode { get; init; }
    public IReadOnlyList<WifiSetupDevice> Devices { get; init; } = [];
    public string? SelectedDeviceId { get; init; }
    public bool Provisioned { get; init; }
    public string? ConnectedSsid { get; init; }
    public string? Address { get; init; }
}

public sealed record WifiSetupDevice
{
    public string DeviceId { get; init; } = "";
    public string DisplayName { get; init; } = "EchoDesk Mini";
    public string Model { get; init; } = "";
    public string? SetupSsid { get; init; }
    public string Address { get; init; } = "";
    public int Port { get; init; }
    public bool SetupMode { get; init; }
    public bool Paired { get; init; }
    public string? FirmwareVersion { get; init; }
    public string? SetupState { get; init; }

    [JsonIgnore]
    public string Summary => string.IsNullOrWhiteSpace(SetupSsid)
        ? $"{DisplayName} · {Address}"
        : $"{DisplayName} · {SetupSsid}";
}

public sealed record FirmwareManagerState
{
    public static FirmwareManagerState Unavailable { get; } = new();

    public bool Available { get; init; }
    public string Status { get; init; } = "unavailable";
    public string? Operation { get; init; }
    public string? Stage { get; init; }
    public int? ProgressPercent { get; init; }
    public string? Message { get; init; }
    public string? ErrorCode { get; init; }
    public string? ErrorMessage { get; init; }
    public string? SelectedPort { get; init; }
    public string? SelectedHardwareModel { get; init; }
    public bool BoardConfirmed { get; init; }
    public bool WriteCompleted { get; init; }
    public bool Reconnected { get; init; }
    public bool SettingsBackupAvailable { get; init; }
    public bool UpdateNotificationsEnabled { get; init; }
    public string? CurrentVersion { get; init; }
    public string? AvailableVersion { get; init; }
    public string? ReleaseNotes { get; init; }
    public IReadOnlyList<FirmwareDeviceCandidate> Devices { get; init; } = [];
}

public sealed record FirmwareDeviceCandidate
{
    public string Port { get; init; } = "";
    public string DisplayName { get; init; } = "Serial device";
    public string? Vid { get; init; }
    public string? Pid { get; init; }
    public string? Adapter { get; init; }
    public string? ChipFamily { get; init; }
    public ulong? FlashSizeBytes { get; init; }
    public bool EchoDeskHandshake { get; init; }
    public string? FirmwareVersion { get; init; }
    public string? HardwareModel { get; init; }
    public bool Compatible { get; init; }
    public bool RequiresBoardConfirmation { get; init; }
    public string? SetupState { get; init; }
    public int? SetupVersion { get; init; }
    public bool InBootloader { get; init; }

    [JsonIgnore]
    public string Summary => $"{Port} · {DisplayName}";
}

public sealed record AiUsageComponentState
{
    public static AiUsageComponentState Unavailable { get; } = new();

    public bool Available { get; init; }
    public bool Enabled { get; init; }
    public string Status { get; init; } = "unavailable";
    public DateTimeOffset? LastSuccessfulUpdate { get; init; }
    public string? Error { get; init; }
    public string Provider { get; init; } = "Codex";
    public bool Fetching { get; init; }
    public string? Source { get; init; }
    public string? CliVersion { get; init; }
}

public sealed record EchoDeskDevice
{
    [JsonPropertyName("name")]
    public string Name { get; init; } = "EchoDesk";

    [JsonPropertyName("firmwareVersion")]
    public string? FirmwareVersion { get; init; }

    [JsonPropertyName("hardwareModel")]
    public string? HardwareModel { get; init; }

    [JsonPropertyName("activeApp")]
    public string? ActiveApp { get; init; }

    [JsonPropertyName("supportedApps")]
    public IReadOnlyList<string> SupportedApps { get; init; } = [];

    [JsonPropertyName("brightness")]
    public int? Brightness { get; init; }

    [JsonPropertyName("theme")]
    public string? Theme { get; init; }

    [JsonPropertyName("settingsRevision")]
    public ulong SettingsRevision { get; init; }

    [JsonPropertyName("settings")]
    public DeviceSettings Settings { get; init; } = new();

    [JsonPropertyName("capabilities")]
    public DeviceCapabilities Capabilities { get; init; } = new();

    [JsonPropertyName("unsupportedSettings")]
    public IReadOnlyList<string> UnsupportedSettings { get; init; } = [];

    [JsonPropertyName("setupState")]
    public string? SetupState { get; init; }

    [JsonPropertyName("setupVersion")]
    public int? SetupVersion { get; init; }
}

public sealed record DeviceSettings
{
    [JsonPropertyName("theme")]
    public string? Theme { get; init; }

    [JsonPropertyName("brightness")]
    public int? Brightness { get; init; }

    [JsonPropertyName("nowPlayingLayout")]
    public string? NowPlayingLayout { get; init; }

    [JsonPropertyName("nowPlayingBackground")]
    public string? NowPlayingBackground { get; init; }

    [JsonPropertyName("nowPlayingBackgroundCustomized")]
    public bool? NowPlayingBackgroundCustomized { get; init; }

    [JsonPropertyName("defaultApp")]
    public string? DefaultApp { get; init; }

    [JsonPropertyName("clockFormat")]
    public string? ClockFormat { get; init; }

    [JsonPropertyName("timezone")]
    public string? Timezone { get; init; }

    [JsonPropertyName("codexPercentageMode")]
    public string? CodexPercentageMode { get; init; }

    [JsonPropertyName("codexPreferredDisplay")]
    public string? CodexPreferredDisplay { get; init; }

    [JsonPropertyName("performanceDefaultView")]
    public string? PerformanceDefaultView { get; init; }
}

public sealed record DeviceCapabilities
{
    [JsonPropertyName("controlProtocol")]
    public int ControlProtocol { get; init; }

    [JsonPropertyName("theme")]
    public bool Theme { get; init; }

    [JsonPropertyName("brightness")]
    public bool Brightness { get; init; }

    [JsonPropertyName("nowPlaying")]
    public bool NowPlaying { get; init; }

    [JsonPropertyName("restart")]
    public bool Restart { get; init; }
}

public sealed record SettingsPatch
{
    [JsonPropertyName("theme")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? Theme { get; init; }

    [JsonPropertyName("brightness")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public int? Brightness { get; init; }

    [JsonPropertyName("nowPlayingLayout")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? NowPlayingLayout { get; init; }

    [JsonPropertyName("nowPlayingBackground")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? NowPlayingBackground { get; init; }

    [JsonPropertyName("nowPlayingBackgroundCustomized")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public bool? NowPlayingBackgroundCustomized { get; init; }

    [JsonPropertyName("defaultApp")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? DefaultApp { get; init; }

    [JsonPropertyName("clockFormat")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? ClockFormat { get; init; }

    [JsonPropertyName("timezone")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? Timezone { get; init; }

    [JsonPropertyName("codexPercentageMode")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? CodexPercentageMode { get; init; }

    [JsonPropertyName("codexPreferredDisplay")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? CodexPreferredDisplay { get; init; }

    [JsonPropertyName("performanceDefaultView")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? PerformanceDefaultView { get; init; }
}

public static class EchoDeskApps
{
    public static readonly (string Id, string Label, string Glyph)[] All =
    [
        ("home", "Home", "\uE80F"),
        ("codex", "Codex Usage", "\uE943"),
        ("clock", "Clock", "\uE823"),
        ("performance", "Performance", "\uE9D9"),
        ("nowPlaying", "Now Playing", "\uE8D6"),
        ("settings", "Settings", "\uE713"),
    ];
}
