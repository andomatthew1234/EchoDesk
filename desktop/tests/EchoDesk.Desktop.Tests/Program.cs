using System.IO.Pipes;
using System.Text.Json;
using EchoDesk.Desktop.Core.Models;
using EchoDesk.Desktop.Core.Services;

namespace EchoDesk.Desktop.Tests;

internal static class Program
{
    public static async Task<int> Main()
    {
        try
        {
            MapperUsesOnlyReportedValues();
            MapperPreservesVersionedClockState();
            MapperPreservesNotificationStackPrivacyState();
            MapperPreservesPcDashboardConfiguration();
            AppCatalogIncludesMyDevice();
            FirmwareManagerMappingPreservesSafetyStates();
            MapperAcceptsNullOptionalNumericFields();
            FirstRunOnboardingIsDismissibleAndOneTime();
            GuidedSetupStartsAtConnectAndPinsSelection();
            WifiSetupMappingAndGuidedPolicyAreAuthoritative();
            await BrokerRoundTripIsBoundedAndRevisionAware();
            Console.WriteLine("EchoDesk.Desktop.Tests: PASS");
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"EchoDesk.Desktop.Tests: FAIL\n{error}");
            return 1;
        }
    }

    private static void AppCatalogIncludesMyDevice()
    {
        var app = EchoDeskApps.All.SingleOrDefault(candidate => candidate.Id == "myDevice");
        Assert(app.Id == "myDevice" && app.Label == "My device",
            "My device is missing from the authoritative Desktop app catalogue");
    }

    private static void MapperPreservesVersionedClockState()
    {
        using var document = JsonDocument.Parse("""
        {
          "brokerAvailable":true,"connection":"connected",
          "clock":{"available":true,"protocol":1,"configRequestMode":"focus","configRequestSequence":3,"state":{
            "revision":9,"activeMode":"countdown",
            "stopwatch":{"status":"paused","elapsedMs":12500},
            "countdown":{"status":"running","durationSeconds":600,"remainingSeconds":321,"progressPermille":465},
            "focus":{"status":"paused","phase":"break","focusMinutes":45,"breakMinutes":10,"rounds":3,"currentRound":2,"remainingSeconds":240,"progressPermille":600}
          }}
        }
        """);
        var state = BrokerStateMapper.FromPayload(document.RootElement);
        Assert(state.Clock.Available && state.Clock.Protocol == 1, "Clock capability was not mapped");
        Assert(state.Clock.ConfigRequestMode == "focus", "Clock configuration request was lost");
        Assert(state.Clock.ConfigRequestSequence == 3, "repeated Clock request identity was lost");
        Assert(state.Clock.Revision == 9 && state.Clock.ActiveMode == "countdown",
            "Clock state identity was lost");
        Assert(state.Clock.Stopwatch.ElapsedMs == 12500 && state.Clock.Countdown.RemainingSeconds == 321,
            "Clock elapsed/remaining values were lost");
        Assert(state.Clock.Focus.Phase == "break" && state.Clock.Focus.CurrentRound == 2,
            "Focus plan state was lost");
    }

    private static void MapperPreservesNotificationStackPrivacyState()
    {
        using var document = JsonDocument.Parse("""
        {"components":{"notificationStack":{"enabled":true,"status":"connected","permission":"allowed",
          "message":"Private USB relay active","forwardedCount":7,
          "availableSources":["Mail","Calendar"],"excludedSources":["Calendar"]}}}
        """);
        var state = BrokerStateMapper.FromPayload(document.RootElement);
        Assert(state.NotificationStack.Available && state.NotificationStack.Enabled,
            "Notification Stack setup state was not mapped");
        Assert(state.NotificationStack.Permission == "allowed" && state.NotificationStack.ForwardedCount == 7,
            "Notification permission/forward count was lost");
        Assert(state.NotificationStack.AvailableSources.SequenceEqual(["Mail", "Calendar"])
               && state.NotificationStack.ExcludedSources.SequenceEqual(["Calendar"]),
            "Notification source filters were lost");
        Assert(EchoDeskApps.All.Any(app => app.Id == "notificationStack"),
            "Notification Stack is missing from the Desktop app catalogue");
    }

    private static void MapperPreservesPcDashboardConfiguration()
    {
        using var document = JsonDocument.Parse("""
        {"connection":"connected","pcDashboard":{"available":true,"protocol":1,
          "state":{"revision":5,"metrics":["cpu","ram","battery","storage"]}}}
        """);
        var state = BrokerStateMapper.FromPayload(document.RootElement);
        Assert(state.PcDashboard.Available && state.PcDashboard.Protocol == 1,
            "PC Dashboard capability was not mapped");
        Assert(state.PcDashboard.Revision == 5
               && state.PcDashboard.Metrics.SequenceEqual(["cpu", "ram", "battery", "storage"]),
            "PC Dashboard metric selection was lost");
        Assert(EchoDeskApps.All.Any(app => app.Id == "pcDashboard"),
            "PC Dashboard is missing from the Desktop app catalogue");
    }

    private static void FirstRunOnboardingIsDismissibleAndOneTime()
    {
        var json = new JsonSerializerOptions(JsonSerializerDefaults.Web);
        var upgradedPreferences = JsonSerializer.Deserialize<DesktopPreferences>("{}", json) ?? new();
        Assert(!upgradedPreferences.FirstRunCompleted,
            "an existing settings file without the first-run flag was treated as completed");
        var persisted = JsonSerializer.Deserialize<DesktopPreferences>(
            JsonSerializer.Serialize(upgradedPreferences with { FirstRunCompleted = true }, json), json);
        Assert(persisted?.FirstRunCompleted == true,
            "first-run completion did not survive local preference serialization");

        var firstLaunch = new FirstRunFlow();
        Assert(firstLaunch.Step == FirstRunStep.Welcome,
            "a fresh Desktop install did not begin at the welcome screen");
        Assert(firstLaunch.Continue() && firstLaunch.Step == FirstRunStep.SetupPrompt,
            "Continue did not advance to the setup question");
        Assert(firstLaunch.Finish() && firstLaunch.Step == FirstRunStep.Complete,
            "Not now/Yes could not dismiss first-run onboarding");
        Assert(!firstLaunch.Continue() && !firstLaunch.Finish(),
            "completed first-run onboarding was re-entered");

        var returningLaunch = new FirstRunFlow(alreadyCompleted: true);
        Assert(returningLaunch.Step == FirstRunStep.Complete,
            "persisted first-run completion was ignored");
    }

    private static void WifiSetupMappingAndGuidedPolicyAreAuthoritative()
    {
        using var document = JsonDocument.Parse("""
        {
          "wifiSetup":{
            "available":true,"status":"found","message":"EchoDesk found",
            "selectedDeviceId":"mini-a","provisioned":false,
            "devices":[
              {"deviceId":"mini-a","displayName":"Studio EchoDesk","model":"Freenove FNK0114A_2P4",
               "setupSsid":"EchoDesk-A1B2","address":"192.168.4.1","port":39321,
               "setupMode":true,"paired":false,"firmwareVersion":"2.31.0","setupState":"wifiSetup"},
              {"deviceId":"bad","displayName":"Bad record","model":"Freenove FNK0114A_2P4",
               "address":"192.168.4.2","port":70000,"setupMode":true,"paired":false}
            ]
          }
        }
        """);
        var state = BrokerStateMapper.FromPayload(document.RootElement);
        Assert(state.WifiSetup.Available && state.WifiSetup.Status == "found", "Wi-Fi setup status was not mapped");
        Assert(state.WifiSetup.Devices.Count == 1, "invalid Wi-Fi setup candidates were not bounded");
        var device = state.WifiSetup.Devices[0];
        Assert(device.SetupSsid == "EchoDesk-A1B2" && device.Port == 39321,
            "temporary setup network identity was lost");

        var flow = new GuidedSetupFlow();
        Assert(flow.Step == GuidedSetupStep.Connect, "Wi-Fi setup did not start at Connect");
        flow.SelectTransport(GuidedSetupTransport.Wifi);
        Assert(flow.Step == GuidedSetupStep.SelectDevice
               && flow.WifiStep == GuidedWifiStep.JoinTemporaryNetwork,
            "Wi-Fi setup did not start with the single network-join step");
        Assert(flow.AdvanceWifi() && flow.WifiStep == GuidedWifiStep.FindDevice,
            "Wi-Fi discovery step did not advance");
        flow.SelectWifi(device.DeviceId, state.WifiSetup.Devices);
        Assert(flow.AdvanceWifi() && flow.WifiStep == GuidedWifiStep.EnterCredentials,
            "Wi-Fi credentials step did not advance");
        Assert(flow.CanContinue && flow.BeginWifiProvisioning() && flow.PinnedWifiDeviceId == "mini-a",
            "Wi-Fi setup target was not pinned");
        flow.SelectWifi("another", [device with { DeviceId = "another" }]);
        Assert(flow.PinnedWifiDeviceId == "mini-a", "pinned Wi-Fi target changed during provisioning");
        Assert(!flow.CompleteWifi(true, "another"), "a different Wi-Fi device completed the flow");
        Assert(flow.CompleteWifi(true, "mini-a") && flow.Step == GuidedSetupStep.Complete,
            "authoritative Wi-Fi provisioning did not reach Welcome");
    }

    private static void FirmwareManagerMappingPreservesSafetyStates()
    {
        Assert(BrokerProtocol.EchoDeskMiniHardwareModel == "EchoDesk Mini", "firmware model identity drifted");
        using var document = JsonDocument.Parse("""
        {
          "brokerAvailable":true,"connection":"disconnected",
          "firmwareManager":{
            "available":true,"status":"selectionRequired","selectedPort":"COM7",
            "showUnknownDevices":true,"setupPhase":"awaitingSettings","setupTargetPort":"COM7","setupCompleted":false,
            "selectedHardwareModel":"EchoDesk Mini","boardConfirmed":false,
            "writeCompleted":false,"reconnected":false,"currentVersion":"2.28.0",
            "availableVersion":"2.29.0","progressPercent":101,
            "devices":[{
              "port":"COM7","displayName":"USB-SERIAL CH340","vid":"1A86","pid":"7523",
              "adapter":"CH340","chipFamily":"esp32","flashSizeBytes":4194304,
              "echoDeskHandshake":false,"compatible":true,"requiresBoardConfirmation":true,
              "setupState":"notStarted","setupVersion":2,"deviceMode":"application",
              "identityStatus":"unknown","usbSerialNumber":"USB-ABC-123"
            }]
          }
        }
        """);
        var state = BrokerStateMapper.FromPayload(document.RootElement);
        Assert(state.FirmwareManager.Available, "firmware manager was not mapped");
        Assert(state.FirmwareManager.Status == "selectionRequired", "firmware status lost");
        Assert(state.FirmwareManager.ProgressPercent is null, "out-of-range progress must not be displayed");
        Assert(state.FirmwareManager.Devices.Count == 1, "firmware device list lost");
        Assert(state.FirmwareManager.Devices[0].RequiresBoardConfirmation, "generic CH340 confirmation requirement lost");
        Assert(state.FirmwareManager.Devices[0].SetupState == "notStarted", "guided setup state lost");
        Assert(state.FirmwareManager.Devices[0].SetupVersion == 2, "guided setup version lost");
        Assert(state.FirmwareManager.ShowUnknownDevices, "unknown-device reveal state lost");
        Assert(state.FirmwareManager.SetupPhase == "awaitingSettings"
               && state.FirmwareManager.SetupTargetPort == "COM7" && !state.FirmwareManager.SetupCompleted,
            "authoritative setup transition fields were not mapped");
        Assert(state.FirmwareManager.Devices[0].IdentityStatus == "unknown"
               && state.FirmwareManager.Devices[0].UsbSerialNumber == "USB-ABC-123",
            "device identity fields were not mapped");
        Assert(!state.FirmwareManager.WriteCompleted && !state.FirmwareManager.Reconnected,
            "incomplete installation was incorrectly promoted to success");
    }

    private static void GuidedSetupStartsAtConnectAndPinsSelection()
    {
        var flow = new GuidedSetupFlow();
        Assert(flow.Step == GuidedSetupStep.Connect, "guided setup must begin at Connect without a hidden legal step");
        flow.SelectTransport(GuidedSetupTransport.Usb);
        Assert(flow.Step == GuidedSetupStep.SelectDevice, "USB choice did not reach device selection");

            var genericAdapter = new FirmwareDeviceCandidate
            {
                Port = "COM3", DisplayName = "USB-SERIAL CH340", Compatible = true,
                ChipFamily = "esp32", EchoDeskHandshake = false,
            };
            var firstMini = new FirmwareDeviceCandidate
            {
                Port = "COM7", DisplayName = "EchoDesk Mini A", Compatible = true,
                ChipFamily = "esp32", EchoDeskHandshake = true,
                HardwareModel = BrokerProtocol.EchoDeskMiniHardwareModel, IdentityStatus = "verified",
            };
            var secondMini = new FirmwareDeviceCandidate
            {
                Port = "COM9", DisplayName = "EchoDesk Mini B", Compatible = true,
                ChipFamily = "esp32", EchoDeskHandshake = true,
                HardwareModel = BrokerProtocol.EchoDeskMiniHardwareModel, IdentityStatus = "verified",
            };
            var bootloaderCandidate = new FirmwareDeviceCandidate
            {
                Port = "COM4", DisplayName = "USB-SERIAL CH340", Compatible = true,
                RequiresBoardConfirmation = true,
            };
            var devices = new[] { genericAdapter, bootloaderCandidate, firstMini, secondMini };
            Assert(GuidedSetupFlow.IsKnown(firstMini) && !GuidedSetupFlow.IsUnknown(firstMini),
                "verified EchoDesk was not classified as known");
            Assert(GuidedSetupFlow.IsUnknown(bootloaderCandidate),
                "unidentified recovery candidate was not classified as unknown");
            flow.Select("COM3", devices);
            Assert(flow.SelectedPort is null, "generic serial adapter was accepted as an EchoDesk Mini");
            flow.Select("COM4", devices);
            Assert(flow.SelectedPort == "COM4" && flow.SelectedDeviceRequiresConfirmation,
                "recoverable bootloader candidate was not offered with an explicit confirmation gate");
            Assert(!flow.CanContinue, "unidentified bootloader candidate bypassed physical board confirmation");
            flow.SetBoardConfirmed(true);
            Assert(flow.CanContinue, "confirmed bootloader candidate remained blocked");
            flow.Select("COM7", devices);
            Assert(flow.SelectedPort == "COM7", "suitable device selection was not retained");
            Assert(flow.ConfirmDevice() && flow.PinnedPort == "COM7"
                   && flow.Step == GuidedSetupStep.ChoosePath, "selection was not pinned before options");
            flow.Select("COM9", devices);
            Assert(flow.PinnedPort == "COM7", "pinned setup target changed after selection");
            Assert(flow.StartUpdateCheck() && flow.Step == GuidedSetupStep.Install,
                "check-for-updates choice did not reach update stage");
            Assert(!flow.FinishInstall(writeVerified: true, reconnected: false),
                "incomplete update was promoted to quick settings");
            Assert(flow.FinishInstall(writeVerified: true, reconnected: true)
                   && flow.Step == GuidedSetupStep.QuickSettings, "verified update did not reach quick settings");
            Assert(flow.Complete() && flow.Step == GuidedSetupStep.Complete,
                "quick settings did not reach Welcome");
    }

    private static void MapperAcceptsNullOptionalNumericFields()
    {
        using var document = JsonDocument.Parse("""
        {
          "brokerAvailable":true,"brokerRevision":3,"connection":"disconnected",
          "deviceState":null,
          "firmwareManager":{
            "availableVersion":null,"boardConfirmed":false,"currentVersion":null,
            "devices":[{
              "port":"COM5","displayName":"USB-SERIAL CH340","flashSizeBytes":null,
              "compatible":true,"requiresBoardConfirmation":true
            }],
            "progressPercent":null,"selectedPort":null,"status":"idle"
          }
        }
        """);

        var state = BrokerStateMapper.FromPayload(document.RootElement);
        Assert(state.BrokerAvailable, "idle host payload was not mapped");
        Assert(state.FirmwareManager.ProgressPercent is null, "null progress should remain absent");
        Assert(state.FirmwareManager.Devices.Count == 1, "firmware candidate was lost");
        Assert(state.FirmwareManager.Devices[0].FlashSizeBytes is null, "null flash size should remain absent");
    }

    private static void MapperUsesOnlyReportedValues()
    {
        using var document = JsonDocument.Parse("""
        {
          "brokerAvailable":true,"brokerRevision":8,"connection":"connected","port":"COM5","firmware":"2.28.0",
          "components":{
            "aiUsage":{"enabled":true,"status":"live","lastSuccessfulUpdate":"2026-10-30T12:34:56Z","provider":"Codex","fetching":false,"source":"codexbar-cli","cliVersion":"0.70.0","diagnosticCode":"ok","exitCode":0,"diagnosticDetail":"Provider returned valid usage data."},
            "performance":{"status":"live","lastSampleAt":"2026-10-30T12:35:01Z","capabilityNegotiated":true},
            "nowPlaying":{"status":"unavailable","error":"Windows media sessions are unavailable.","capabilityNegotiated":true}
          },
          "deviceState":{
            "deviceName":"EchoDesk","hardwareModel":"Freenove FNK0114A_2P4","firmwareVersion":"2.28.0",
            "revision":12,"activeApp":"nowPlaying",
            "supportedApps":["home","codex","clock","performance","nowPlaying","myDevice","settings"],
            "settings":{"theme":"dark","brightness":73,"nowPlayingLayout":"carPlay","nowPlayingBackground":"audioBars","nowPlayingBackgroundCustomized":true,
              "defaultApp":"codex","clockFormat":"24h","timezone":"Australia/Melbourne","codexPercentageMode":"remaining","codexPreferredDisplay":"weekly","performanceDefaultView":"gpu"},
            "unsupportedSettings":["timezone","performanceDefaultView"]
          }
        }
        """);
        var state = BrokerStateMapper.FromPayload(document.RootElement);
        Assert(state.IsConnected, "state should be connected");
        Assert(state.BrokerRevision == 8, "broker revision lost");
        Assert(state.Device?.SettingsRevision == 12, "device revision lost");
        Assert(state.Device?.Settings.Brightness == 73, "brightness lost");
        Assert(state.Device?.Settings.NowPlayingLayout == "carPlay", "layout lost");
        Assert(state.Device?.Settings.DefaultApp == "codex", "default app lost");
        Assert(state.Device?.Settings.ClockFormat == "24h", "clock format lost");
        Assert(state.Device?.Settings.Timezone == "Australia/Melbourne", "timezone lost");
        Assert(state.Device?.Settings.CodexPercentageMode == "remaining", "Codex percentage mode lost");
        Assert(state.Device?.Settings.CodexPreferredDisplay == "weekly", "Codex preferred display lost");
        Assert(state.Device?.Settings.PerformanceDefaultView == "gpu", "performance default view lost");
        Assert(state.Device?.SupportedApps.Contains("myDevice") == true, "My device support was lost");
        Assert(state.Device?.UnsupportedSettings.Contains("timezone") == true, "unsupported settings lost");
        Assert(state.AiUsage.Available && state.AiUsage.Enabled, "AI usage component lost");
        Assert(state.AiUsage.Status == "live", "AI usage status lost");
        Assert(state.AiUsage.LastSuccessfulUpdate?.UtcDateTime == new DateTime(2026, 10, 30, 12, 34, 56, DateTimeKind.Utc), "AI usage timestamp lost");
        Assert(state.AiUsage.DiagnosticCode == "ok" && state.AiUsage.ExitCode == 0,
            "bounded Codex diagnostic identity was lost");
        Assert(state.AiUsage.DiagnosticDetail == "Provider returned valid usage data.",
            "safe Codex diagnostic detail was lost");
        Assert(state.Performance.Status == "live" && state.Performance.CapabilityNegotiated,
            "Performance producer health was lost");
        Assert(state.NowPlaying.Status == "unavailable"
               && state.NowPlaying.Error == "Windows media sessions are unavailable.",
            "Now Playing producer failure was lost");
    }

    private static async Task BrokerRoundTripIsBoundedAndRevisionAware()
    {
        using var serverReady = new CancellationTokenSource(TimeSpan.FromSeconds(10));
        var pipeName = $"EchoDesk.Desktop.Tests.{Guid.NewGuid():N}";
        var observedSet = new TaskCompletionSource<JsonElement>(TaskCreationOptions.RunContinuationsAsynchronously);
        var observedAiToggle = new TaskCompletionSource<JsonElement>(TaskCreationOptions.RunContinuationsAsynchronously);
        var observedFirmwareInstall = new TaskCompletionSource<JsonElement>(TaskCreationOptions.RunContinuationsAsynchronously);
        var observedFirmwareScan = new TaskCompletionSource<JsonElement>(TaskCreationOptions.RunContinuationsAsynchronously);
        var observedManualRecovery = new TaskCompletionSource<JsonElement>(TaskCreationOptions.RunContinuationsAsynchronously);
        var observedSetupBegin = new TaskCompletionSource<JsonElement>(TaskCreationOptions.RunContinuationsAsynchronously);
        var observedSetupComplete = new TaskCompletionSource<JsonElement>(TaskCreationOptions.RunContinuationsAsynchronously);
        var observedFactoryReset = new TaskCompletionSource<JsonElement>(TaskCreationOptions.RunContinuationsAsynchronously);
        var observedWifiDiscover = new TaskCompletionSource<JsonElement>(TaskCreationOptions.RunContinuationsAsynchronously);
        var observedWifiProvision = new TaskCompletionSource<JsonElement>(TaskCreationOptions.RunContinuationsAsynchronously);
        var observedWifiCancel = new TaskCompletionSource<JsonElement>(TaskCreationOptions.RunContinuationsAsynchronously);
        var observedClockControl = new TaskCompletionSource<JsonElement>(TaskCreationOptions.RunContinuationsAsynchronously);
        var server = Task.Run(async () =>
        {
            await using var pipe = new NamedPipeServerStream(
                pipeName, PipeDirection.InOut, 1,
                PipeTransmissionMode.Byte, PipeOptions.Asynchronous);
            await pipe.WaitForConnectionAsync(serverReady.Token);
            using var reader = new StreamReader(pipe, leaveOpen: true);
            await using var writer = new StreamWriter(pipe, leaveOpen: true) { AutoFlush = true };
            while (!serverReady.IsCancellationRequested)
            {
                var line = await reader.ReadLineAsync(serverReady.Token);
                if (line is null) break;
                Assert(System.Text.Encoding.UTF8.GetByteCount(line) <= BrokerProtocol.MaximumLineBytes, "request exceeded bound");
                using var requestDocument = JsonDocument.Parse(line);
                var request = requestDocument.RootElement.Clone();
                var id = request.GetProperty("id").GetString();
                var method = request.GetProperty("method").GetString();
                if (method == "set_settings") observedSet.TrySetResult(request);
                if (method == "set_ai_usage_enabled") observedAiToggle.TrySetResult(request);
                if (method == "firmware_install") observedFirmwareInstall.TrySetResult(request);
                if (method == "firmware_scan") observedFirmwareScan.TrySetResult(request);
                if (method == "firmware_manual_recovery") observedManualRecovery.TrySetResult(request);
                if (method == "setup_begin") observedSetupBegin.TrySetResult(request);
                if (method == "setup_complete") observedSetupComplete.TrySetResult(request);
                if (method == "factory_reset") observedFactoryReset.TrySetResult(request);
                if (method == "wifi_setup_discover") observedWifiDiscover.TrySetResult(request);
                if (method == "wifi_setup_provision") observedWifiProvision.TrySetResult(request);
                if (method == "wifi_setup_cancel") observedWifiCancel.TrySetResult(request);
                if (method == "clock_control") observedClockControl.TrySetResult(request);
                var response = new
                {
                    version = 1,
                    id,
                    ok = true,
                    result = new
                    {
                        brokerAvailable = true,
                        brokerRevision = method == "set_settings" ? 2 : 1,
                        connection = "connected",
                        port = "COM5",
                        firmware = "2.28.0",
                        wifiSetup = new
                        {
                            available = true,
                            status = method == "wifi_setup_provision" ? "provisioning" : "found",
                            message = "EchoDesk found",
                            selectedDeviceId = "mini-a",
                            provisioned = false,
                            devices = new[]
                            {
                                new
                                {
                                    deviceId = "mini-a", displayName = "EchoDesk Mini", model = "Freenove FNK0114A_2P4",
                                    setupSsid = "EchoDesk-A1B2", address = "192.168.4.1", port = 39321,
                                    setupMode = true, paired = false, firmwareVersion = "2.31.0",
                                },
                            },
                        },
                        components = new
                        {
                            aiUsage = new
                            {
                                enabled = method == "set_ai_usage_enabled"
                                    ? request.GetProperty("params").GetProperty("enabled").GetBoolean()
                                    : true,
                                status = "live",
                                lastSuccessfulUpdate = "2026-10-30T12:34:56Z",
                                provider = "Codex",
                                fetching = false,
                            },
                        },
                        firmwareManager = new
                        {
                            available = true,
                            status = method == "firmware_install" ? "preparing" : "ready",
                            selectedPort = "COM7",
                            selectedHardwareModel = BrokerProtocol.EchoDeskMiniHardwareModel,
                            boardConfirmed = true,
                            writeCompleted = false,
                            reconnected = false,
                            devices = new[]
                            {
                                new
                                {
                                    port = "COM7", displayName = "USB-SERIAL CH340", adapter = "CH340",
                                    chipFamily = "esp32", compatible = true, requiresBoardConfirmation = true,
                                },
                            },
                        },
                        deviceState = new
                        {
                            deviceName = "EchoDesk",
                            hardwareModel = "Freenove FNK0114A_2P4",
                            firmwareVersion = "2.28.0",
                            revision = method == "set_settings" ? 6 : 5,
                            setupState = method switch
                            {
                                "setup_complete" => "completed",
                                "factory_reset" => "notConfigured",
                                _ => "inProgress",
                            },
                            setupVersion = 2,
                            activeApp = "home",
                            supportedApps = new[] { "home", "nowPlaying" },
                            settings = new
                            {
                                theme = "light", brightness = 55,
                                nowPlayingLayout = "albumCover",
                                nowPlayingBackground = "blurredAlbumCover",
                                nowPlayingBackgroundCustomized = false,
                            },
                            unsupportedSettings = Array.Empty<string>(),
                        },
                    },
                };
                await writer.WriteLineAsync(JsonSerializer.Serialize(response, BrokerProtocol.Json));
            }
        }, serverReady.Token);

        await Task.Delay(50, serverReady.Token);
        await using var client = new BrokerClient(pipeName);
        var hello = new TaskCompletionSource<BrokerState>(TaskCreationOptions.RunContinuationsAsynchronously);
        client.StateChanged += (_, state) => { if (state.IsConnected) hello.TrySetResult(state); };
        client.Start();
        var initial = await hello.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Assert(initial.Device?.SettingsRevision == 5, "hello state revision incorrect");
        var changed = await client.SetSettingsAsync(5, new SettingsPatch { Brightness = 55 }, false, serverReady.Token);
        Assert(changed.Device?.SettingsRevision == 6, "set response revision incorrect");
        var request = await observedSet.Task.WaitAsync(TimeSpan.FromSeconds(2));
        Assert(request.GetProperty("params").GetProperty("baseRevision").GetUInt64() == 5, "baseRevision missing");
        Assert(!request.GetProperty("params").GetProperty("persist").GetBoolean(), "preview unexpectedly persisted");
        var toggled = await client.SetAiUsageEnabledAsync(false, serverReady.Token);
        Assert(!toggled.AiUsage.Enabled, "AI usage toggle response was not authoritative");
        var toggleRequest = await observedAiToggle.Task.WaitAsync(TimeSpan.FromSeconds(2));
        var toggleParams = toggleRequest.GetProperty("params");
        Assert(toggleParams.EnumerateObject().Count() == 1, "AI usage toggle sent unexpected parameters");
        Assert(!toggleParams.GetProperty("enabled").GetBoolean(), "AI usage toggle value missing");
        await client.ScanFirmwareDevicesAsync(true, serverReady.Token);
        var scanRequest = await observedFirmwareScan.Task.WaitAsync(TimeSpan.FromSeconds(2));
        var scanParams = scanRequest.GetProperty("params");
        Assert(scanParams.EnumerateObject().Count() == 1 && scanParams.GetProperty("revealUnknown").GetBoolean(),
            "firmware scan did not explicitly request unidentified devices");
        var prepared = await client.StartFirmwareOperationAsync("repair", serverReady.Token);
        Assert(prepared.FirmwareManager.Status == "preparing", "firmware operation response was not mapped");
        var firmwareRequest = await observedFirmwareInstall.Task.WaitAsync(TimeSpan.FromSeconds(2));
        var firmwareParams = firmwareRequest.GetProperty("params");
        Assert(firmwareParams.EnumerateObject().Count() == 1, "firmware request exposed arbitrary flash parameters");
        Assert(firmwareParams.GetProperty("mode").GetString() == "repair", "firmware operation mode missing");
        var recovering = await client.StartManualRecoveryAsync("COM7", serverReady.Token);
        Assert(recovering.FirmwareManager.Available, "manual recovery response was not mapped");
        var recoveryRequest = await observedManualRecovery.Task.WaitAsync(TimeSpan.FromSeconds(2));
        var recoveryParams = recoveryRequest.GetProperty("params");
        Assert(recoveryParams.EnumerateObject().Count() == 1, "manual recovery exposed arbitrary flash parameters");
        Assert(recoveryParams.GetProperty("port").GetString() == "COM7", "manual recovery selected port missing");
        Assert(!recoveryParams.TryGetProperty("mode", out _), "manual recovery must not send an install mode");
        Assert(!recoveryParams.TryGetProperty("image", out _), "manual recovery must not accept an image path");
        Assert(!recoveryParams.TryGetProperty("address", out _), "manual recovery must not accept a flash address");
        var setupStarted = await client.BeginGuidedSetupAsync("COM7", serverReady.Token);
        Assert(setupStarted.Device?.SetupState == "inProgress", "setup begin response was not mapped");
        var beginRequest = await observedSetupBegin.Task.WaitAsync(TimeSpan.FromSeconds(2));
        var beginParams = beginRequest.GetProperty("params");
        Assert(beginParams.EnumerateObject().Count() == 1 && beginParams.GetProperty("port").GetString() == "COM7",
            "setup begin did not carry only the pinned port");
        var setupCompleted = await client.CompleteGuidedSetupAsync("COM7", serverReady.Token);
        Assert(setupCompleted.Device?.SetupState == "completed", "setup completion response was not mapped");
        var completeRequest = await observedSetupComplete.Task.WaitAsync(TimeSpan.FromSeconds(2));
        var completeParams = completeRequest.GetProperty("params");
        Assert(completeParams.EnumerateObject().Count() == 1 && completeParams.GetProperty("port").GetString() == "COM7",
            "setup complete did not carry only the pinned port");
        var reset = await client.FactoryResetAsync("COM7", serverReady.Token);
        Assert(reset.Device?.SetupState == "notConfigured", "factory reset response was not mapped");
        var resetRequest = await observedFactoryReset.Task.WaitAsync(TimeSpan.FromSeconds(2));
        var resetParams = resetRequest.GetProperty("params");
        Assert(resetParams.EnumerateObject().Count() == 1 && resetParams.GetProperty("port").GetString() == "COM7",
            "factory reset did not carry only the connected port");
        var discovered = await client.DiscoverWifiSetupDevicesAsync(serverReady.Token);
        Assert(discovered.WifiSetup.Devices.Count == 1, "Wi-Fi discovery response was not mapped");
        var discoverRequest = await observedWifiDiscover.Task.WaitAsync(TimeSpan.FromSeconds(2));
        Assert(!discoverRequest.GetProperty("params").EnumerateObject().Any(),
            "Wi-Fi discovery sent unexpected parameters");
        var provisioning = await client.ProvisionWifiSetupDeviceAsync(
            "mini-a", "12345678", "Home Wi-Fi", "correct horse battery", serverReady.Token);
        Assert(provisioning.WifiSetup.Status == "provisioning", "Wi-Fi provisioning response was not authoritative");
        var provisionRequest = await observedWifiProvision.Task.WaitAsync(TimeSpan.FromSeconds(2));
        var provisionParams = provisionRequest.GetProperty("params");
        Assert(provisionParams.EnumerateObject().Count() == 4, "Wi-Fi provisioning sent unexpected parameters");
        Assert(provisionParams.GetProperty("deviceId").GetString() == "mini-a", "Wi-Fi device identity missing");
        Assert(provisionParams.GetProperty("setupCode").GetString() == "12345678", "Wi-Fi setup code missing");
        Assert(provisionParams.GetProperty("ssid").GetString() == "Home Wi-Fi", "Wi-Fi SSID missing");
        Assert(provisionParams.GetProperty("password").GetString() == "correct horse battery", "Wi-Fi password missing");
        await client.CancelWifiSetupAsync(serverReady.Token);
        var cancelRequest = await observedWifiCancel.Task.WaitAsync(TimeSpan.FromSeconds(2));
        Assert(!cancelRequest.GetProperty("params").EnumerateObject().Any(),
            "Wi-Fi cancellation sent unexpected parameters");
        await client.ControlClockAsync(
            "configure", "focus", focusMinutes: 25, breakMinutes: 5, rounds: 4,
            cancellationToken: serverReady.Token);
        var clockRequest = await observedClockControl.Task.WaitAsync(TimeSpan.FromSeconds(2));
        var clockParams = clockRequest.GetProperty("params");
        Assert(clockParams.EnumerateObject().Count() == 5, "Clock control sent unexpected parameters");
        Assert(clockParams.GetProperty("action").GetString() == "configure"
               && clockParams.GetProperty("mode").GetString() == "focus",
            "Clock control action/mode was not versioned by the broker seam");
        Assert(clockParams.GetProperty("focusMinutes").GetInt32() == 25
               && clockParams.GetProperty("breakMinutes").GetInt32() == 5
               && clockParams.GetProperty("rounds").GetInt32() == 4,
            "Clock focus configuration was not preserved");
        serverReady.Cancel();
        try { await server; } catch (OperationCanceledException) { }
    }

    private static void Assert(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
