using System.Diagnostics;
using System.Text.Json;
using System.Text.Json.Serialization;
using LibreHardwareMonitor.Hardware;

namespace EchoDesk.PerformanceHelper;

internal static class Program
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        DefaultIgnoreCondition = JsonIgnoreCondition.Never,
        WriteIndented = false,
    };

    public static async Task<int> Main(string[] args)
    {
        if (args.Contains("--self-test", StringComparer.Ordinal))
            return SensorSnapshot.RunSelfTest();

        if (!OperatingSystem.IsWindows())
        {
            Console.Error.WriteLine("echodesk-performance-helper is Windows-only");
            return 2;
        }

        int? parentPid = ReadIntOption(args, "--parent-pid");
        int intervalMs = Math.Clamp(ReadIntOption(args, "--interval-ms") ?? 1000, 250, 60_000);
        bool once = args.Contains("--once", StringComparer.Ordinal);

        var computer = new Computer
        {
            IsCpuEnabled = true,
            IsGpuEnabled = true,
            IsMemoryEnabled = true,
            IsMotherboardEnabled = true,
            IsNetworkEnabled = true,
        };

        try
        {
            try
            {
                computer.Open();
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"LibreHardwareMonitor startup failed: {ex.Message}");
                return 3;
            }

            var visitor = new UpdateVisitor();
            do
            {
                if (parentPid is int pid && !ParentIsAlive(pid))
                    return 0;

                try
                {
                    computer.Accept(visitor);
                    PerformanceSample sample = SensorSnapshot.Read(computer.Hardware);
                    Console.WriteLine(JsonSerializer.Serialize(sample, JsonOptions));
                    Console.Out.Flush();
                }
                catch (Exception ex)
                {
                    // stderr is diagnostic only. stdout remains strict NDJSON so the
                    // Rust companion can reject malformed lines independently.
                    Console.Error.WriteLine($"Sensor read failed: {ex.Message}");
                }

                if (!once)
                    await Task.Delay(intervalMs).ConfigureAwait(false);
            } while (!once);

            return 0;
        }
        finally
        {
            try
            {
                computer.Close();
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"LibreHardwareMonitor shutdown failed: {ex.Message}");
            }
        }
    }

    private static int? ReadIntOption(string[] args, string name)
    {
        int index = Array.IndexOf(args, name);
        if (index < 0 || index + 1 >= args.Length)
            return null;
        return int.TryParse(args[index + 1], out int value) ? value : null;
    }

    private static bool ParentIsAlive(int pid)
    {
        try
        {
            using Process parent = Process.GetProcessById(pid);
            return !parent.HasExited;
        }
        catch (Exception ex) when (ex is ArgumentException or InvalidOperationException or System.ComponentModel.Win32Exception)
        {
            return false;
        }
    }
}

internal sealed class UpdateVisitor : IVisitor
{
    public void VisitComputer(IComputer computer) => computer.Traverse(this);

    public void VisitHardware(IHardware hardware)
    {
        hardware.Update();
        foreach (IHardware child in hardware.SubHardware)
            child.Accept(this);
    }

    public void VisitSensor(ISensor sensor) { }
    public void VisitParameter(IParameter parameter) { }
}

internal static class SensorSnapshot
{
    private const double Gibibyte = 1024d * 1024d * 1024d;

    public static PerformanceSample Read(IEnumerable<IHardware> roots)
    {
        List<SensorReading> readings = Flatten(roots).ToList();

        double? cpuPct = CpuLoad(readings);
        double? gpuPct = GpuLoad(readings);
        double? ramPct = Exact(readings, HardwareType.Memory, SensorType.Load, "Memory");
        double? ramUsedGiB = Exact(readings, HardwareType.Memory, SensorType.Data, "Memory Used");
        double? ramAvailableGiB = Exact(readings, HardwareType.Memory, SensorType.Data, "Memory Available");
        ulong? ramUsedBytes = ramUsedGiB is double && ramAvailableGiB is double
            ? ToBytes(ramUsedGiB)
            : null;
        ulong? ramTotalBytes = ramUsedGiB is double used && ramAvailableGiB is double available
            ? ToBytes(used + available)
            : null;

        (ulong? down, ulong? up) = NetworkRate(readings);
        return new PerformanceSample(
            "performance",
            1,
            DateTimeOffset.UtcNow.ToUnixTimeSeconds(),
            Percent(cpuPct),
            Percent(gpuPct),
            Percent(ramPct),
            ramUsedBytes,
            ramTotalBytes,
            new NetworkSample(down, up),
            new TemperatureSample(
                Temperature(CpuTemperature(readings)),
                Temperature(GpuTemperature(readings)),
                Temperature(MotherboardTemperature(readings))));
    }

    public static int RunSelfTest()
    {
        try
        {
            var readings = new List<SensorReading>
            {
                new(HardwareType.Cpu, "cpu/0", "CPU Total", SensorType.Load, 42),
                new(HardwareType.Cpu, "cpu/0", "CPU Core #1", SensorType.Load, 99),
                new(HardwareType.Cpu, "cpu/0", "CPU Package", SensorType.Temperature, 63),
                new(HardwareType.GpuNvidia, "gpu/0", "GPU Core", SensorType.Load, 57),
                new(HardwareType.GpuNvidia, "gpu/0", "GPU Core", SensorType.Temperature, 71),
                new(HardwareType.GpuNvidia, "gpu/0", "GPU Hot Spot", SensorType.Temperature, 98),
                new(HardwareType.Memory, "memory", "Memory", SensorType.Load, 50),
                new(HardwareType.Network, "net/0", "Download Speed", SensorType.Throughput, 1000),
                new(HardwareType.Network, "net/0", "Upload Speed", SensorType.Throughput, 200),
                new(HardwareType.Network, "net/1", "Download Speed", SensorType.Throughput, 3000),
            };
            Require(CpuLoad(readings) == 42, "CPU Total must win over individual cores");
            Require(GpuLoad(readings) == 57, "GPU Core load selection");
            Require(GpuTemperature(readings) == 71, "GPU hotspot must not replace GPU Core");
            Require(MotherboardTemperature(readings) is null, "missing motherboard sensor must stay null");
            (ulong? down, ulong? up) = NetworkRate(readings);
            Require(down == 4000 && up == 200, "network adapters must aggregate safely");

            var hotspotOnly = new List<SensorReading>
            {
                new(HardwareType.GpuAmd, "gpu/1", "GPU Hot Spot", SensorType.Temperature, 95),
                new(HardwareType.GpuAmd, "gpu/1", "GPU Memory Junction", SensorType.Temperature, 88),
            };
            Require(GpuTemperature(hotspotOnly) is null,
                    "hotspot/junction-only GPU temperature must stay null");
            Console.WriteLine("echodesk-performance-helper self-test: PASS");
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"echodesk-performance-helper self-test: FAIL: {ex.Message}");
            return 1;
        }
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }

    private static IEnumerable<SensorReading> Flatten(IEnumerable<IHardware> hardware)
    {
        foreach (IHardware item in hardware)
        {
            foreach (ISensor sensor in item.Sensors)
            {
                if (sensor.Value is float value && float.IsFinite(value))
                    yield return new SensorReading(item.HardwareType, item.Identifier.ToString(), sensor.Name, sensor.SensorType, value);
            }

            foreach (SensorReading child in Flatten(item.SubHardware))
                yield return child;
        }
    }

    private static double? CpuLoad(List<SensorReading> all)
    {
        double? total = Exact(all, HardwareType.Cpu, SensorType.Load, "CPU Total");
        if (total is not null)
            return total;

        double[] cores = all.Where(r => r.Hardware == HardwareType.Cpu && r.Type == SensorType.Load &&
                                        r.Name.StartsWith("CPU Core", StringComparison.OrdinalIgnoreCase))
                            .Select(r => r.Value).ToArray();
        return cores.Length == 0 ? null : cores.Average();
    }

    private static double? GpuLoad(List<SensorReading> all)
    {
        IEnumerable<IGrouping<string, SensorReading>> devices = all.Where(r => IsGpu(r.Hardware)).GroupBy(r => r.HardwareId);
        List<double> loads = new();
        foreach (IGrouping<string, SensorReading> device in devices)
        {
            SensorReading[] sensors = device.Where(r => r.Type == SensorType.Load).ToArray();
            double? value = Named(sensors, "GPU Core") ?? Named(sensors, "D3D 3D") ??
                            sensors.Where(r => r.Name.Contains("3D", StringComparison.OrdinalIgnoreCase))
                                   .Select(r => (double?)r.Value).Max();
            if (value is not null)
                loads.Add(value.Value);
        }
        return loads.Count == 0 ? null : loads.Max();
    }

    private static double? CpuTemperature(List<SensorReading> all)
    {
        SensorReading[] sensors = all.Where(r => r.Hardware == HardwareType.Cpu && r.Type == SensorType.Temperature).ToArray();
        return Named(sensors, "Core (Tctl/Tdie)") ?? Named(sensors, "CPU Package") ??
               Named(sensors, "Core Average") ?? sensors.Select(r => (double?)r.Value).Max();
    }

    private static double? GpuTemperature(List<SensorReading> all)
    {
        SensorReading[] sensors = all.Where(r => IsGpu(r.Hardware) && r.Type == SensorType.Temperature).ToArray();
        // Never silently relabel hotspot, junction or memory temperature as
        // the ordinary GPU temperature shown by EchoDesk.
        foreach (string name in new[] { "GPU Core", "GPU Temperature", "Core" })
        {
            double[] ordinary = sensors.Where(r => r.Name.Equals(name, StringComparison.OrdinalIgnoreCase))
                                       .Select(r => r.Value).ToArray();
            if (ordinary.Length > 0)
                return ordinary.Max();
        }
        return null;
    }

    private static double? MotherboardTemperature(List<SensorReading> all)
    {
        SensorReading[] sensors = all.Where(r =>
            r.Type == SensorType.Temperature &&
            r.Hardware is HardwareType.Motherboard or HardwareType.SuperIO or HardwareType.EmbeddedController).ToArray();
        foreach (string name in new[] { "Motherboard", "Mainboard", "System", "System #1" })
        {
            double? value = Named(sensors, name);
            if (value is not null)
                return value;
        }
        return null;
    }

    private static (ulong? Down, ulong? Up) NetworkRate(List<SensorReading> all)
    {
        var adapters = all.Where(r => r.Hardware == HardwareType.Network && r.Type == SensorType.Throughput)
                          .GroupBy(r => r.HardwareId)
                          .Select(group => new
                          {
                              Down = Named(group, "Download Speed"),
                              Up = Named(group, "Upload Speed"),
                          })
                          .Where(rate => rate.Down is not null || rate.Up is not null)
                          .ToList();
        if (adapters.Count == 0)
            return (null, null);

        // Sum all reporting adapters so simultaneous Ethernet/VPN/Wi-Fi
        // traffic is not silently discarded. A disconnected adapter normally
        // reports zero or no value and therefore cannot create a counter spike.
        double? down = adapters.Any(rate => rate.Down is not null)
            ? adapters.Sum(rate => rate.Down ?? 0)
            : null;
        double? up = adapters.Any(rate => rate.Up is not null)
            ? adapters.Sum(rate => rate.Up ?? 0)
            : null;
        return (ToRate(down), ToRate(up));
    }

    private static double? Exact(IEnumerable<SensorReading> all, HardwareType hardware, SensorType type, string name) =>
        Named(all.Where(r => r.Hardware == hardware && r.Type == type), name);

    private static double? Named(IEnumerable<SensorReading> sensors, string name) =>
        sensors.FirstOrDefault(r => r.Name.Equals(name, StringComparison.OrdinalIgnoreCase))?.Value;

    private static bool IsGpu(HardwareType type) =>
        type is HardwareType.GpuNvidia or HardwareType.GpuAmd or HardwareType.GpuIntel;

    private static double? Percent(double? value) => value is double n && double.IsFinite(n)
        ? Math.Round(Math.Clamp(n, 0, 100), 1)
        : null;

    private static double? Temperature(double? value) => value is double n && double.IsFinite(n) && n is >= -100 and <= 300
        ? Math.Round(n, 1)
        : null;

    private static ulong? ToBytes(double? gibibytes) => gibibytes is double n && double.IsFinite(n) && n >= 0 && n <= ulong.MaxValue / Gibibyte
        ? (ulong)Math.Round(n * Gibibyte)
        : null;

    private static ulong? ToRate(double? bytesPerSecond) => bytesPerSecond is double n && double.IsFinite(n) && n >= 0 && n <= ulong.MaxValue
        ? (ulong)Math.Round(n)
        : null;

    private sealed record SensorReading(HardwareType Hardware, string HardwareId, string Name, SensorType Type, double Value);
}

internal sealed record PerformanceSample(
    string Type,
    int Version,
    long Timestamp,
    double? CpuPct,
    double? GpuPct,
    double? RamPct,
    ulong? RamUsedBytes,
    ulong? RamTotalBytes,
    NetworkSample Network,
    TemperatureSample Temperature);

internal sealed record NetworkSample(ulong? DownBps, ulong? UpBps);
internal sealed record TemperatureSample(double? CpuC, double? GpuC, double? MotherboardC);
