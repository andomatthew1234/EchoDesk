using System.Text.Json;
using EchoDesk.Desktop.Core.Models;

namespace EchoDesk.Desktop.Core.Services;

public static class BrokerStateMapper
{
    public static BrokerState FromPayload(JsonElement payload)
    {
        if (payload.ValueKind == JsonValueKind.Object && payload.TryGetProperty("state", out var nested))
        {
            payload = nested;
        }

        var connection = Text(payload, "connection") ?? "disconnected";
        var port = Text(payload, "port");
        var lastError = Text(payload, "lastError");
        var firmware = Text(payload, "firmware");
        if (payload.TryGetProperty("firmware", out var firmwareNode) && firmwareNode.ValueKind == JsonValueKind.Object)
        {
            firmware = Text(firmwareNode, "version");
        }

        if (!payload.TryGetProperty("deviceState", out var state) || state.ValueKind != JsonValueKind.Object)
        {
            state = payload;
        }

        var settings = state.TryGetProperty("settings", out var settingsNode)
            ? ParseSettings(settingsNode)
            : new DeviceSettings();
        var revision = ULong(state, "revision") ?? ULong(state, "settingsRevision") ?? 0;
        var activeApp = Text(state, "activeApp");
        var supportedApps = Strings(state, "supportedApps");
        var unsupportedSettings = Strings(state, "unsupportedSettings");
        var info = payload.TryGetProperty("device", out var deviceInfo) && deviceInfo.ValueKind == JsonValueKind.Object
            ? deviceInfo
            : default;
        var hardware = Text(state, "hardwareModel")
                       ?? Text(info, "hardwareModel")
                       ?? Text(info, "display");
        var name = Text(state, "deviceName") ?? Text(state, "name") ?? Text(info, "name") ?? "EchoDesk";
        var controlProtocol = Int(state, "controlProtocol")
                              ?? Int(info, "echoDeskControlProtocol")
                              ?? (supportedApps.Count > 0 ? 1 : 0);

        EchoDeskDevice? device = null;
        if (string.Equals(connection, "connected", StringComparison.OrdinalIgnoreCase)
            || activeApp is not null || supportedApps.Count > 0)
        {
            device = new EchoDeskDevice
            {
                Name = name,
                FirmwareVersion = firmware ?? Text(state, "firmwareVersion"),
                HardwareModel = hardware,
                ActiveApp = activeApp,
                SupportedApps = supportedApps,
                Brightness = settings.Brightness ?? Int(state, "brightness"),
                Theme = settings.Theme ?? Text(state, "theme"),
                SettingsRevision = revision,
                Settings = settings,
                UnsupportedSettings = unsupportedSettings,
                SetupState = Text(state, "setupState"),
                SetupVersion = Int(state, "setupVersion"),
                Capabilities = new DeviceCapabilities
                {
                    ControlProtocol = controlProtocol,
                    Theme = controlProtocol > 0,
                    Brightness = controlProtocol > 0,
                    NowPlaying = controlProtocol > 0,
                    Restart = controlProtocol > 0,
                },
            };
        }

        return new BrokerState
        {
            BrokerRevision = ULong(payload, "brokerRevision") ?? 0,
            BrokerAvailable = true,
            Connection = connection,
            Port = port,
            Device = device,
            LastError = lastError,
            AiUsage = ParseAiUsage(payload),
            Performance = ParseTelemetryComponent(payload, "performance"),
            NowPlaying = ParseTelemetryComponent(payload, "nowPlaying"),
            Clock = ParseClock(payload),
            NotificationStack = ParseNotificationStack(payload),
            PcDashboard = ParsePcDashboard(payload),
            FirmwareManager = ParseFirmwareManager(payload),
            WifiSetup = ParseWifiSetup(payload),
        };
    }

    private static PcDashboardState ParsePcDashboard(JsonElement payload)
    {
        if (!payload.TryGetProperty("pcDashboard", out var dashboard) || dashboard.ValueKind != JsonValueKind.Object)
            return PcDashboardState.Unavailable;
        var metrics = dashboard.TryGetProperty("state", out var state) && state.ValueKind == JsonValueKind.Object
            ? Strings(state, "metrics") : [];
        string[] allowed = ["battery", "microphone", "storage", "ram", "cpu", "gpu", "networkDown", "networkUp", "cpuTemperature", "gpuTemperature", "motherboardTemperature"];
        var metricsValid = metrics.Count is >= 1 and <= 4
                           && metrics.Distinct(StringComparer.Ordinal).Count() == metrics.Count
                           && metrics.All(allowed.Contains);
        return new PcDashboardState
        {
            Available = Bool(dashboard, "available") ?? false,
            Protocol = Int(dashboard, "protocol") ?? 0,
            Revision = dashboard.TryGetProperty("state", out state) && state.ValueKind == JsonValueKind.Object ? ULong(state, "revision") ?? 0 : 0,
            Metrics = metricsValid ? metrics : ["cpu", "ram", "battery", "storage"],
        };
    }

    private static NotificationStackState ParseNotificationStack(JsonElement payload)
    {
        if (!payload.TryGetProperty("components", out var components)
            || components.ValueKind != JsonValueKind.Object
            || !components.TryGetProperty("notificationStack", out var component)
            || component.ValueKind != JsonValueKind.Object)
            return NotificationStackState.Unavailable;
        return new NotificationStackState
        {
            Available = true,
            Enabled = Bool(component, "enabled") ?? false,
            Status = BoundedText(component, "status", 40) ?? "unavailable",
            Permission = BoundedText(component, "permission", 40) ?? "notRequested",
            Message = BoundedText(component, "message", 256),
            ForwardedCount = ULong(component, "forwardedCount") ?? 0,
            AvailableSources = Strings(component, "availableSources").Take(32).ToArray(),
            ExcludedSources = Strings(component, "excludedSources").Take(32).ToArray(),
        };
    }

    private static ClockUtilityState ParseClock(JsonElement payload)
    {
        if (!payload.TryGetProperty("clock", out var clock) || clock.ValueKind != JsonValueKind.Object)
            return ClockUtilityState.Unavailable;
        var available = Bool(clock, "available") ?? false;
        var protocol = Int(clock, "protocol") ?? 0;
        if (!clock.TryGetProperty("state", out var state) || state.ValueKind != JsonValueKind.Object)
            return new ClockUtilityState
            {
                Available = available,
                Protocol = protocol,
                ConfigRequestMode = ClockMode(Text(clock, "configRequestMode"), false),
                ConfigRequestSequence = ULong(clock, "configRequestSequence") ?? 0,
            };
        var stopwatch = state.TryGetProperty("stopwatch", out var sw) ? sw : default;
        var countdown = state.TryGetProperty("countdown", out var cd) ? cd : default;
        var focus = state.TryGetProperty("focus", out var fs) ? fs : default;
        return new ClockUtilityState
        {
            Available = available,
            Protocol = protocol,
            ConfigRequestMode = ClockMode(Text(clock, "configRequestMode"), false),
            ConfigRequestSequence = ULong(clock, "configRequestSequence") ?? 0,
            Revision = ULong(state, "revision") ?? 0,
            ActiveMode = ClockMode(Text(state, "activeMode"), true) ?? "time",
            Stopwatch = new ClockStopwatchState
            {
                Status = ClockStatus(Text(stopwatch, "status")),
                ElapsedMs = Math.Min(ULong(stopwatch, "elapsedMs") ?? 0, 604_800_000),
            },
            Countdown = new ClockCountdownState
            {
                Status = ClockStatus(Text(countdown, "status")),
                DurationSeconds = Math.Clamp(Int(countdown, "durationSeconds") ?? 300, 1, 86400),
                RemainingSeconds = Math.Clamp(Int(countdown, "remainingSeconds") ?? 300, 0, 86400),
                ProgressPermille = Math.Clamp(Int(countdown, "progressPermille") ?? 0, 0, 1000),
            },
            Focus = new ClockFocusState
            {
                Status = ClockStatus(Text(focus, "status")),
                Phase = Text(focus, "phase") is "break" or "complete" ? Text(focus, "phase")! : "focus",
                FocusMinutes = Math.Clamp(Int(focus, "focusMinutes") ?? 25, 1, 180),
                BreakMinutes = Math.Clamp(Int(focus, "breakMinutes") ?? 5, 1, 60),
                Rounds = Math.Clamp(Int(focus, "rounds") ?? 4, 1, 12),
                CurrentRound = Math.Clamp(Int(focus, "currentRound") ?? 1, 0, 12),
                RemainingSeconds = Math.Clamp(Int(focus, "remainingSeconds") ?? 1500, 0, 10800),
                ProgressPermille = Math.Clamp(Int(focus, "progressPermille") ?? 0, 0, 1000),
            },
        };
    }

    private static string ClockStatus(string? value) =>
        value is "running" or "paused" or "complete" ? value : "idle";

    private static string? ClockMode(string? value, bool includeTime) => value switch
    {
        "stopwatch" or "countdown" or "focus" => value,
        "time" when includeTime => value,
        _ => null,
    };

    private static WifiSetupState ParseWifiSetup(JsonElement payload)
    {
        if (!payload.TryGetProperty("wifiSetup", out var setup)
            || setup.ValueKind != JsonValueKind.Object)
        {
            return WifiSetupState.Unavailable;
        }

        var devices = new List<WifiSetupDevice>();
        if (setup.TryGetProperty("devices", out var deviceNodes)
            && deviceNodes.ValueKind == JsonValueKind.Array)
        {
            foreach (var device in deviceNodes.EnumerateArray())
            {
                if (device.ValueKind != JsonValueKind.Object) continue;
                var deviceId = BoundedText(device, "deviceId", 128);
                var address = BoundedText(device, "address", 45);
                var port = Int(device, "port") ?? 0;
                if (string.IsNullOrWhiteSpace(deviceId) || string.IsNullOrWhiteSpace(address)
                    || port is < 1 or > 65535)
                {
                    continue;
                }
                devices.Add(new WifiSetupDevice
                {
                    DeviceId = deviceId,
                    DisplayName = BoundedText(device, "displayName", 80) ?? "EchoDesk Mini",
                    Model = BoundedText(device, "model", 80) ?? "",
                    SetupSsid = BoundedText(device, "setupSsid", 32),
                    Address = address,
                    Port = port,
                    SetupMode = Bool(device, "setupMode") ?? false,
                    Paired = Bool(device, "paired") ?? false,
                    FirmwareVersion = BoundedText(device, "firmwareVersion", 40),
                    SetupState = BoundedText(device, "setupState", 40),
                });
            }
        }

        return new WifiSetupState
        {
            Available = Bool(setup, "available") ?? true,
            Status = BoundedText(setup, "status", 32) ?? "idle",
            Message = SafeText(setup, "message"),
            ErrorCode = BoundedText(setup, "errorCode", 80),
            Devices = devices,
            SelectedDeviceId = BoundedText(setup, "selectedDeviceId", 128),
            Provisioned = Bool(setup, "provisioned") ?? false,
            ConnectedSsid = BoundedText(setup, "connectedSsid", 32),
            Address = BoundedText(setup, "address", 45),
        };
    }

    private static FirmwareManagerState ParseFirmwareManager(JsonElement payload)
    {
        if (!payload.TryGetProperty("firmwareManager", out var manager)
            || manager.ValueKind != JsonValueKind.Object)
        {
            return FirmwareManagerState.Unavailable;
        }

        var devices = new List<FirmwareDeviceCandidate>();
        if (manager.TryGetProperty("devices", out var deviceNodes)
            && deviceNodes.ValueKind == JsonValueKind.Array)
        {
            foreach (var device in deviceNodes.EnumerateArray())
            {
                if (device.ValueKind != JsonValueKind.Object || string.IsNullOrWhiteSpace(Text(device, "port"))) continue;
                devices.Add(new FirmwareDeviceCandidate
                {
                    Port = Text(device, "port")!,
                    DisplayName = Text(device, "displayName") ?? "Serial device",
                    Vid = UsbIdentifier(device, "vid"),
                    Pid = UsbIdentifier(device, "pid"),
                    Adapter = Text(device, "adapter"),
                    UsbSerialNumber = BoundedText(device, "usbSerialNumber", 128),
                    IdentityStatus = BoundedText(device, "identityStatus", 24) ?? "unknown",
                    DeviceMode = BoundedText(device, "deviceMode", 24) ?? "candidate",
                    ChipFamily = Text(device, "chipFamily"),
                    FlashSizeBytes = ULong(device, "flashSizeBytes"),
                    EchoDeskHandshake = Bool(device, "echoDeskHandshake") ?? false,
                    FirmwareVersion = Text(device, "firmwareVersion"),
                    HardwareModel = Text(device, "hardwareModel"),
                    Compatible = Bool(device, "compatible") ?? false,
                    RequiresBoardConfirmation = Bool(device, "requiresBoardConfirmation") ?? false,
                    SetupState = Text(device, "setupState"),
                    SetupVersion = Int(device, "setupVersion"),
                    InBootloader = Bool(device, "inBootloader") ?? false,
                });
            }
        }

        var progress = Int(manager, "progressPercent");
        if (progress is < 0 or > 100) progress = null;
        return new FirmwareManagerState
        {
            Available = Bool(manager, "available") ?? true,
            Status = Text(manager, "status") ?? "idle",
            Operation = Text(manager, "operation"),
            Stage = Text(manager, "stage"),
            ProgressPercent = progress,
            Message = SafeText(manager, "message"),
            ErrorCode = Text(manager, "errorCode"),
            ErrorMessage = SafeText(manager, "errorMessage"),
            SelectedPort = Text(manager, "selectedPort"),
            SelectedHardwareModel = Text(manager, "selectedHardwareModel"),
            BoardConfirmed = Bool(manager, "boardConfirmed") ?? false,
            WriteCompleted = Bool(manager, "writeCompleted") ?? false,
            Reconnected = Bool(manager, "reconnected") ?? false,
            SettingsBackupAvailable = Bool(manager, "settingsBackupAvailable") ?? false,
            UpdateNotificationsEnabled = Bool(manager, "updateNotificationsEnabled") ?? false,
            CurrentVersion = Text(manager, "currentVersion"),
            AvailableVersion = Text(manager, "availableVersion"),
            ReleaseNotes = SafeLongText(manager, "releaseNotes", 4000),
            ShowUnknownDevices = Bool(manager, "showUnknownDevices") ?? false,
            SetupPhase = BoundedText(manager, "setupPhase", 40),
            SetupTargetPort = BoundedText(manager, "setupTargetPort", 128),
            SetupCompleted = Bool(manager, "setupCompleted") ?? false,
            Devices = devices,
        };
    }

    private static AiUsageComponentState ParseAiUsage(JsonElement payload)
    {
        if (!payload.TryGetProperty("components", out var components) || components.ValueKind != JsonValueKind.Object)
        {
            return AiUsageComponentState.Unavailable;
        }

        if (!components.TryGetProperty("aiUsage", out var component)
            && !components.TryGetProperty("aiUsageMonitor", out component))
        {
            return AiUsageComponentState.Unavailable;
        }
        if (component.ValueKind != JsonValueKind.Object) return AiUsageComponentState.Unavailable;

        return new AiUsageComponentState
        {
            Available = true,
            Enabled = Bool(component, "enabled") ?? false,
            Status = Text(component, "status") ?? "waiting",
            LastSuccessfulUpdate = DateTime(component, "lastSuccessfulUpdate"),
            Error = SafeText(component, "error"),
            Provider = Text(component, "provider") ?? "Codex",
            Fetching = Bool(component, "fetching") ?? false,
            Source = Text(component, "source"),
            CliVersion = Text(component, "cliVersion"),
            DiagnosticCode = BoundedText(component, "diagnosticCode", 64),
            ExitCode = Int(component, "exitCode"),
            DiagnosticDetail = BoundedText(component, "diagnosticDetail", 256),
            StderrSummary = BoundedText(component, "stderrSummary", 256),
        };
    }

    private static TelemetryComponentState ParseTelemetryComponent(JsonElement payload, string name)
    {
        if (!payload.TryGetProperty("components", out var components)
            || components.ValueKind != JsonValueKind.Object
            || !components.TryGetProperty(name, out var component)
            || component.ValueKind != JsonValueKind.Object)
        {
            return TelemetryComponentState.Unavailable;
        }

        return new TelemetryComponentState
        {
            Available = true,
            Status = BoundedText(component, "status", 40) ?? "unavailable",
            LastSampleAt = DateTime(component, "lastSampleAt"),
            Error = BoundedText(component, "error", 256),
            CapabilityNegotiated = Bool(component, "capabilityNegotiated") ?? false,
        };
    }

    private static DeviceSettings ParseSettings(JsonElement node) => new()
    {
        Theme = Text(node, "theme"),
        Brightness = Int(node, "brightness"),
        NowPlayingLayout = Text(node, "nowPlayingLayout"),
        NowPlayingBackground = Text(node, "nowPlayingBackground"),
        NowPlayingBackgroundCustomized = Bool(node, "nowPlayingBackgroundCustomized"),
        DefaultApp = Text(node, "defaultApp"),
        ClockFormat = Text(node, "clockFormat"),
        Timezone = Text(node, "timezone"),
        CodexPercentageMode = Text(node, "codexPercentageMode"),
        CodexPreferredDisplay = Text(node, "codexPreferredDisplay"),
        PerformanceDefaultView = Text(node, "performanceDefaultView"),
    };

    private static string? Text(JsonElement node, string name) =>
        node.ValueKind == JsonValueKind.Object && node.TryGetProperty(name, out var value)
            && value.ValueKind == JsonValueKind.String ? value.GetString() : null;

    private static int? Int(JsonElement node, string name) =>
        node.ValueKind == JsonValueKind.Object && node.TryGetProperty(name, out var value)
            && value.ValueKind == JsonValueKind.Number && value.TryGetInt32(out var number) ? number : null;

    private static string? UsbIdentifier(JsonElement node, string name)
    {
        if (node.ValueKind != JsonValueKind.Object || !node.TryGetProperty(name, out var value))
            return null;
        if (value.ValueKind == JsonValueKind.String) return value.GetString();
        return value.ValueKind == JsonValueKind.Number && value.TryGetUInt16(out var number)
            ? $"0x{number:X4}"
            : null;
    }

    private static ulong? ULong(JsonElement node, string name) =>
        node.ValueKind == JsonValueKind.Object && node.TryGetProperty(name, out var value)
            && value.ValueKind == JsonValueKind.Number && value.TryGetUInt64(out var number) ? number : null;

    private static bool? Bool(JsonElement node, string name) =>
        node.ValueKind == JsonValueKind.Object && node.TryGetProperty(name, out var value)
            && value.ValueKind is JsonValueKind.True or JsonValueKind.False ? value.GetBoolean() : null;

    private static DateTimeOffset? DateTime(JsonElement node, string name)
    {
        var value = Text(node, name);
        return DateTimeOffset.TryParse(value, System.Globalization.CultureInfo.InvariantCulture,
            System.Globalization.DateTimeStyles.AssumeUniversal, out var parsed) ? parsed : null;
    }

    private static string? SafeText(JsonElement node, string name)
    {
        var value = Text(node, name)?.Trim();
        if (string.IsNullOrWhiteSpace(value)) return null;
        value = string.Join(' ', value.Split(['\r', '\n', '\t'], StringSplitOptions.RemoveEmptyEntries));
        return value.Length <= 240 ? value : value[..237] + "…";
    }

    private static string? SafeLongText(JsonElement node, string name, int maximumLength)
    {
        var value = Text(node, name)?.Trim();
        if (string.IsNullOrWhiteSpace(value)) return null;
        value = value.Replace("\0", string.Empty, StringComparison.Ordinal);
        return value.Length <= maximumLength ? value : value[..(maximumLength - 1)] + "…";
    }

    private static string? BoundedText(JsonElement node, string name, int maximumLength)
    {
        var value = Text(node, name)?.Trim();
        if (string.IsNullOrWhiteSpace(value) || value.Length > maximumLength
            || value.IndexOfAny(['\r', '\n', '\0']) >= 0)
        {
            return null;
        }
        return value;
    }

    private static IReadOnlyList<string> Strings(JsonElement node, string name)
    {
        if (node.ValueKind != JsonValueKind.Object || !node.TryGetProperty(name, out var values)
            || values.ValueKind != JsonValueKind.Array)
        {
            return [];
        }

        return values.EnumerateArray()
            .Where(value => value.ValueKind == JsonValueKind.String)
            .Select(value => value.GetString())
            .Where(value => !string.IsNullOrWhiteSpace(value))
            .Cast<string>()
            .ToArray();
    }
}
