using System.Collections.Concurrent;
using System.IO.Pipes;
using System.Text.Json;
using EchoDesk.Desktop.Core.Models;

namespace EchoDesk.Desktop.Core.Services;

public sealed class BrokerClient : IAsyncDisposable
{
    private readonly ConcurrentDictionary<string, TaskCompletionSource<BrokerEnvelope>> pending = new();
    private readonly SemaphoreSlim writeGate = new(1, 1);
    private readonly CancellationTokenSource lifetime = new();
    private readonly string pipeName;
    private NamedPipeClientStream? pipe;
    private Task? worker;
    private long nextId;

    public event EventHandler<BrokerState>? StateChanged;

    public BrokerState State { get; private set; } = BrokerState.Offline();

    public BrokerClient(string? pipeName = null)
    {
        this.pipeName = string.IsNullOrWhiteSpace(pipeName) ? BrokerProtocol.PipeName : pipeName;
    }

    public void Start() => worker ??= Task.Run(() => RunAsync(lifetime.Token));

    public Task<BrokerState> RefreshAsync(CancellationToken cancellationToken = default) =>
        StateRequestAsync("get_state", null, cancellationToken);

    public Task<BrokerState> ReconnectDeviceAsync(CancellationToken cancellationToken = default) =>
        StateRequestAsync("reconnect", null, cancellationToken);

    public Task<BrokerState> OpenAppAsync(string app, CancellationToken cancellationToken = default) =>
        StateRequestAsync("open_app", new { app }, cancellationToken);

    public Task<BrokerState> SetSettingsAsync(
        ulong baseRevision,
        SettingsPatch settings,
        bool persist,
        CancellationToken cancellationToken = default) =>
        StateRequestAsync("set_settings", new { baseRevision, settings, persist }, cancellationToken);

    public Task<BrokerState> RestartAsync(CancellationToken cancellationToken = default) =>
        StateRequestAsync("restart", null, cancellationToken);

    public Task<BrokerState> SetAiUsageEnabledAsync(bool enabled, CancellationToken cancellationToken = default) =>
        StateRequestAsync("set_ai_usage_enabled", new { enabled }, cancellationToken);

    public Task<BrokerState> ScanFirmwareDevicesAsync(
        bool revealUnknown = false,
        CancellationToken cancellationToken = default) =>
        StateRequestAsync("firmware_scan", new { revealUnknown }, cancellationToken);

    public Task<BrokerState> CheckFirmwareUpdatesAsync(CancellationToken cancellationToken = default) =>
        StateRequestAsync("firmware_check_updates", null, cancellationToken);

    public Task<BrokerState> SelectFirmwareDeviceAsync(
        string port,
        string hardwareModel,
        bool boardConfirmed,
        CancellationToken cancellationToken = default) =>
        StateRequestAsync("firmware_select_device", new { port, hardwareModel, boardConfirmed }, cancellationToken);

    public Task<BrokerState> StartFirmwareOperationAsync(
        string mode,
        CancellationToken cancellationToken = default) =>
        StateRequestAsync("firmware_install", new { mode }, cancellationToken);

    public Task<BrokerState> StartManualRecoveryAsync(
        string port,
        CancellationToken cancellationToken = default) =>
        StateRequestAsync("firmware_manual_recovery", new { port }, cancellationToken);

    public Task<BrokerState> BeginGuidedSetupAsync(
        string port,
        CancellationToken cancellationToken = default) =>
        StateRequestAsync("setup_begin", new { port }, cancellationToken);

    public Task<BrokerState> CompleteGuidedSetupAsync(
        string port,
        CancellationToken cancellationToken = default) =>
        StateRequestAsync("setup_complete", new { port }, cancellationToken);

    public Task<BrokerState> FactoryResetAsync(
        string port,
        CancellationToken cancellationToken = default) =>
        StateRequestAsync("factory_reset", new { port }, cancellationToken);

    public Task<BrokerState> CancelFirmwareOperationAsync(CancellationToken cancellationToken = default) =>
        StateRequestAsync("firmware_cancel", null, cancellationToken);

    public Task<BrokerState> DiscoverWifiSetupDevicesAsync(CancellationToken cancellationToken = default) =>
        StateRequestAsync("wifi_setup_discover", null, cancellationToken);

    public Task<BrokerState> ProvisionWifiSetupDeviceAsync(
        string deviceId,
        string setupCode,
        string ssid,
        string password,
        CancellationToken cancellationToken = default) =>
        StateRequestAsync(
            "wifi_setup_provision",
            new { deviceId, setupCode, ssid, password },
            cancellationToken);

    public Task<BrokerState> CancelWifiSetupAsync(CancellationToken cancellationToken = default) =>
        StateRequestAsync("wifi_setup_cancel", null, cancellationToken);

    public Task<BrokerState> SetFirmwareUpdateNotificationsAsync(
        bool enabled,
        CancellationToken cancellationToken = default) =>
        StateRequestAsync("set_update_notifications", new { enabled }, cancellationToken);

    public async Task ShutdownHostAsync(CancellationToken cancellationToken = default)
    {
        await RequestAsync("shutdown", null, cancellationToken).ConfigureAwait(false);
    }

    private async Task RunAsync(CancellationToken cancellationToken)
    {
        var delay = TimeSpan.FromSeconds(1);
        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                var connected = new NamedPipeClientStream(
                    ".", pipeName, PipeDirection.InOut,
                    PipeOptions.Asynchronous | PipeOptions.WriteThrough);
                await connected.ConnectAsync(2000, cancellationToken).ConfigureAwait(false);
                pipe = connected;
                delay = TimeSpan.FromSeconds(1);
                var readerTask = ReadLoopAsync(connected, cancellationToken);
                await StateRequestAsync("hello", new { client = "EchoDesk Desktop", clientVersion = "0.2.7" }, cancellationToken)
                    .ConfigureAwait(false);
                await readerTask.ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                break;
            }
            catch (Exception error)
            {
                Publish(BrokerState.Offline(FriendlyConnectionError(error)));
            }
            finally
            {
                pipe?.Dispose();
                pipe = null;
                FailPending(new IOException("The EchoDesk companion connection closed."));
            }

            try
            {
                await Task.Delay(delay, cancellationToken).ConfigureAwait(false);
                delay = TimeSpan.FromSeconds(Math.Min(delay.TotalSeconds * 1.5, 5));
            }
            catch (OperationCanceledException)
            {
                break;
            }
        }
    }

    private async Task ReadLoopAsync(Stream stream, CancellationToken cancellationToken)
    {
        var reader = new BoundedLineReader(stream);
        while (!cancellationToken.IsCancellationRequested)
        {
            var line = await reader.ReadLineAsync(cancellationToken).ConfigureAwait(false);
            if (line is null)
            {
                throw new EndOfStreamException("EchoDesk companion closed the pipe.");
            }
            if (line.Length == 0)
            {
                continue;
            }

            var envelope = JsonSerializer.Deserialize<BrokerEnvelope>(line, BrokerProtocol.Json)
                           ?? throw new InvalidDataException("Empty broker message.");
            if (envelope.Version != BrokerProtocol.Version)
            {
                throw new InvalidDataException($"Unsupported broker protocol {envelope.Version}.");
            }

            if (string.Equals(envelope.Event, "state", StringComparison.OrdinalIgnoreCase)
                && envelope.Data is { } eventData)
            {
                Publish(BrokerStateMapper.FromPayload(eventData));
            }
            else if (envelope.Id is { } id && pending.TryRemove(id, out var completion))
            {
                completion.TrySetResult(envelope);
            }
        }
    }

    private async Task<BrokerState> StateRequestAsync(
        string method,
        object? parameters,
        CancellationToken cancellationToken)
    {
        var result = await RequestAsync(method, parameters, cancellationToken).ConfigureAwait(false);
        var state = BrokerStateMapper.FromPayload(result);
        Publish(state);
        return state;
    }

    private async Task<JsonElement> RequestAsync(
        string method,
        object? parameters,
        CancellationToken cancellationToken)
    {
        var target = pipe;
        if (target is null || !target.IsConnected)
        {
            throw new BrokerException("broker_unavailable", "EchoDesk Host is still starting or unavailable.");
        }

        var id = Interlocked.Increment(ref nextId).ToString(System.Globalization.CultureInfo.InvariantCulture);
        var completion = new TaskCompletionSource<BrokerEnvelope>(TaskCreationOptions.RunContinuationsAsynchronously);
        if (!pending.TryAdd(id, completion))
        {
            throw new InvalidOperationException("Duplicate broker request ID.");
        }

        try
        {
            using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, lifetime.Token);
            timeout.CancelAfter(TimeSpan.FromSeconds(method switch
            {
                "restart" => 8,
                "firmware_scan" or "firmware_check_updates" or "firmware_select_device"
                    or "wifi_setup_discover" => 15,
                "setup_begin" or "setup_complete" or "factory_reset" => 20,
                "firmware_install" or "firmware_manual_recovery" or "firmware_cancel"
                    or "wifi_setup_provision" or "wifi_setup_cancel" => 30,
                _ => 4,
            }));
            var request = new Dictionary<string, object?>
            {
                ["version"] = BrokerProtocol.Version,
                ["id"] = id,
                ["method"] = method,
                ["params"] = parameters ?? new { },
            };
            var bytes = JsonSerializer.SerializeToUtf8Bytes(request, BrokerProtocol.Json);
            if (bytes.Length + 1 > BrokerProtocol.MaximumLineBytes)
            {
                throw new InvalidDataException("Broker request exceeded 64 KiB.");
            }

            await writeGate.WaitAsync(timeout.Token).ConfigureAwait(false);
            try
            {
                await target.WriteAsync(bytes, timeout.Token).ConfigureAwait(false);
                await target.WriteAsync("\n"u8.ToArray(), timeout.Token).ConfigureAwait(false);
                await target.FlushAsync(timeout.Token).ConfigureAwait(false);
            }
            finally
            {
                writeGate.Release();
            }

            var response = await completion.Task.WaitAsync(timeout.Token).ConfigureAwait(false);
            if (response.Ok != true)
            {
                throw new BrokerException(
                    response.Error?.Code ?? "broker_error",
                    response.Error?.Message ?? "The companion rejected the request.");
            }
            return response.Result ?? throw new InvalidDataException("Broker response did not contain a result.");
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested && !lifetime.IsCancellationRequested)
        {
            throw new TimeoutException($"EchoDesk companion timed out while handling {method}.");
        }
        finally
        {
            pending.TryRemove(id, out _);
        }
    }

    private void Publish(BrokerState state)
    {
        State = state;
        StateChanged?.Invoke(this, state);
    }

    private void FailPending(Exception error)
    {
        foreach (var pair in pending.ToArray())
        {
            if (pending.TryRemove(pair.Key, out var completion))
            {
                completion.TrySetException(error);
            }
        }
    }

    private static string FriendlyConnectionError(Exception error) => error switch
    {
        TimeoutException => "EchoDesk Host did not respond. It may still be starting.",
        IOException => "EchoDesk Host disconnected. Reconnecting automatically…",
        _ => "EchoDesk Host is unavailable. EchoDesk Desktop will restart it automatically.",
    };

    public async ValueTask DisposeAsync()
    {
        lifetime.Cancel();
        pipe?.Dispose();
        if (worker is not null)
        {
            try
            {
                await worker.ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                // Expected during shutdown.
            }
        }
        writeGate.Dispose();
        lifetime.Dispose();
    }
}
