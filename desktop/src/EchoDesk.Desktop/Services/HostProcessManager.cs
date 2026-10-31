using System.Diagnostics;
using System.IO;

namespace EchoDesk.Desktop.Services;

/// <summary>
/// Owns the bundled background host process. Serial access remains entirely in that process;
/// the desktop application communicates only through the local named-pipe broker.
/// </summary>
public sealed class HostProcessManager : IAsyncDisposable
{
    public const string HostFileName = "EchoDesk Host.exe";

    private readonly string hostPath;
    private readonly CancellationTokenSource lifetime = new();
    private readonly object gate = new();
    private Process? process;
    private bool processOwned;
    private Task? monitor;

    public HostProcessManager(string? applicationDirectory = null)
    {
        var directory = applicationDirectory ?? AppContext.BaseDirectory;
        hostPath = Path.GetFullPath(Path.Combine(directory, HostFileName));
    }

    public string HostPath => hostPath;

    public void Start()
    {
        if (monitor is not null) return;
        EnsureRunning();
        monitor = Task.Run(() => MonitorAsync(lifetime.Token));
    }

    public void PrepareForShutdown() => lifetime.Cancel();

    private async Task MonitorAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                await Task.Delay(TimeSpan.FromSeconds(2), cancellationToken).ConfigureAwait(false);
                EnsureRunning();
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                break;
            }
            catch
            {
                // A transient launch failure is retried. Broker state supplies the user-facing status.
            }
        }
    }

    private void EnsureRunning()
    {
        lock (gate)
        {
            if (lifetime.IsCancellationRequested || IsAlive(process)) return;

            process?.Dispose();
            process = FindExistingHost();
            processOwned = false;
            if (process is not null) return;

            if (!File.Exists(hostPath))
            {
                throw new FileNotFoundException(
                    $"The bundled background service is missing: {HostFileName}", hostPath);
            }

            var componentsDirectory = Path.Combine(Path.GetDirectoryName(hostPath)!, "components");
            var cliPath = Path.Combine(componentsDirectory, "codexbar-cli.exe");
            var startInfo = new ProcessStartInfo
            {
                FileName = hostPath,
                Arguments = "--echodesk-host",
                WorkingDirectory = Path.GetDirectoryName(hostPath)!,
                UseShellExecute = false,
                CreateNoWindow = true,
                WindowStyle = ProcessWindowStyle.Hidden,
            };
            startInfo.Environment["ECHODESK_HOST"] = "1";
            startInfo.Environment["AIMONITOR_HEADLESS"] = "1";
            if (File.Exists(cliPath)) startInfo.Environment["ECHODESK_CODEXBAR_CLI"] = cliPath;

            process = Process.Start(startInfo)
                      ?? throw new InvalidOperationException("Windows did not start the bundled EchoDesk Host.");
            processOwned = true;
        }
    }

    private Process? FindExistingHost()
    {
        var processName = Path.GetFileNameWithoutExtension(hostPath);
        foreach (var candidate in Process.GetProcessesByName(processName))
        {
            try
            {
                if (!candidate.HasExited
                    && string.Equals(candidate.MainModule?.FileName, hostPath, StringComparison.OrdinalIgnoreCase))
                {
                    return candidate;
                }
            }
            catch (InvalidOperationException) { }
            catch (System.ComponentModel.Win32Exception) { }
            candidate.Dispose();
        }
        return null;
    }

    private static bool IsAlive(Process? candidate)
    {
        if (candidate is null) return false;
        try { return !candidate.HasExited; }
        catch (InvalidOperationException) { return false; }
    }

    public async ValueTask DisposeAsync()
    {
        PrepareForShutdown();
        if (monitor is not null)
        {
            try { await monitor.ConfigureAwait(false); }
            catch (OperationCanceledException) { }
        }

        Process? current;
        bool owned;
        lock (gate)
        {
            current = process;
            owned = processOwned;
            process = null;
            processOwned = false;
        }

        if (current is not null)
        {
            try
            {
                if (owned && !current.HasExited)
                {
                    using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(3));
                    try { await current.WaitForExitAsync(timeout.Token).ConfigureAwait(false); }
                    catch (OperationCanceledException)
                    {
                        current.Kill(entireProcessTree: true);
                        await current.WaitForExitAsync().ConfigureAwait(false);
                    }
                }
            }
            catch (InvalidOperationException) { }
            finally { current.Dispose(); }
        }
        lifetime.Dispose();
    }
}
