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
            FirmwareManagerMappingPreservesSafetyStates();
            MapperAcceptsNullOptionalNumericFields();
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

    private static void FirmwareManagerMappingPreservesSafetyStates()
    {
        using var document = JsonDocument.Parse("""
        {
          "brokerAvailable":true,"connection":"disconnected",
          "firmwareManager":{
            "available":true,"status":"selectionRequired","selectedPort":"COM7",
            "selectedHardwareModel":"echodesk-mini-fnk0114a-2p4","boardConfirmed":false,
            "writeCompleted":false,"reconnected":false,"currentVersion":"2.28.0",
            "availableVersion":"2.29.0","progressPercent":101,
            "devices":[{
              "port":"COM7","displayName":"USB-SERIAL CH340","vid":"1A86","pid":"7523",
              "adapter":"CH340","chipFamily":"esp32","flashSizeBytes":4194304,
              "echoDeskHandshake":false,"compatible":true,"requiresBoardConfirmation":true
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
        Assert(!state.FirmwareManager.WriteCompleted && !state.FirmwareManager.Reconnected,
            "incomplete installation was incorrectly promoted to success");
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
          "components":{"aiUsage":{"enabled":true,"status":"live","lastSuccessfulUpdate":"2026-10-30T12:34:56Z","provider":"Codex","fetching":false,"source":"codexbar-cli","cliVersion":"0.70.0"}},
          "deviceState":{
            "deviceName":"EchoDesk","hardwareModel":"Freenove FNK0114A_2P4","firmwareVersion":"2.28.0",
            "revision":12,"activeApp":"nowPlaying",
            "supportedApps":["home","codex","clock","performance","nowPlaying","settings"],
            "settings":{"theme":"dark","brightness":73,"nowPlayingLayout":"carPlay","nowPlayingBackground":"audioBars","nowPlayingBackgroundCustomized":true},
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
        Assert(state.Device?.UnsupportedSettings.Contains("timezone") == true, "unsupported settings lost");
        Assert(state.AiUsage.Available && state.AiUsage.Enabled, "AI usage component lost");
        Assert(state.AiUsage.Status == "live", "AI usage status lost");
        Assert(state.AiUsage.LastSuccessfulUpdate?.UtcDateTime == new DateTime(2026, 10, 30, 12, 34, 56, DateTimeKind.Utc), "AI usage timestamp lost");
    }

    private static async Task BrokerRoundTripIsBoundedAndRevisionAware()
    {
        using var serverReady = new CancellationTokenSource(TimeSpan.FromSeconds(10));
        var pipeName = $"EchoDesk.Desktop.Tests.{Guid.NewGuid():N}";
        var observedSet = new TaskCompletionSource<JsonElement>(TaskCreationOptions.RunContinuationsAsynchronously);
        var observedAiToggle = new TaskCompletionSource<JsonElement>(TaskCreationOptions.RunContinuationsAsynchronously);
        var observedFirmwareInstall = new TaskCompletionSource<JsonElement>(TaskCreationOptions.RunContinuationsAsynchronously);
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
                            selectedHardwareModel = "echodesk-mini-fnk0114a-2p4",
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
        var prepared = await client.StartFirmwareOperationAsync("repair", serverReady.Token);
        Assert(prepared.FirmwareManager.Status == "preparing", "firmware operation response was not mapped");
        var firmwareRequest = await observedFirmwareInstall.Task.WaitAsync(TimeSpan.FromSeconds(2));
        var firmwareParams = firmwareRequest.GetProperty("params");
        Assert(firmwareParams.EnumerateObject().Count() == 1, "firmware request exposed arbitrary flash parameters");
        Assert(firmwareParams.GetProperty("mode").GetString() == "repair", "firmware operation mode missing");
        serverReady.Cancel();
        try { await server; } catch (OperationCanceledException) { }
    }

    private static void Assert(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
