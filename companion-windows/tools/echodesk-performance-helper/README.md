# EchoDesk performance helper

This Windows-only helper samples local hardware once per second and writes one
compact JSON object per line to standard output. It never opens a serial port;
the Rust companion remains the sole owner of the display COM port.

The project uses the official stable
[`LibreHardwareMonitorLib` 0.9.6 NuGet package](https://www.nuget.org/packages/LibreHardwareMonitorLib/0.9.6),
whose source is the official
[`LibreHardwareMonitor/LibreHardwareMonitor`](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor)
repository. LibreHardwareMonitor is MPL-2.0; review its upstream
`THIRD-PARTY-NOTICES.txt` before distributing a build. The helper source in
this directory does not copy or modify LibreHardwareMonitor source.

Build a self-contained single-file helper:

```powershell
dotnet publish .\echodesk-performance-helper.csproj -c Release -r win-x64 --self-contained true
```

Smoke-test one sample without touching the display:

```powershell
.\bin\Release\net8.0\win-x64\publish\echodesk-performance-helper.exe --once
```

Run deterministic simulated-sensor checks (also run by `build.ps1`):

```powershell
.\bin\Release\net8.0\win-x64\publish\echodesk-performance-helper.exe --self-test
```

## Sensor selection

- CPU load: `CPU Total`; otherwise the average of `CPU Core*` load sensors.
- GPU load: the highest active GPU's `GPU Core`, `D3D 3D`, or other 3D load.
- RAM: `Memory`, `Memory Used`, and `Memory Available`; LibreHardwareMonitor's
  memory data values are GiB and are converted to bytes.
- Network: sum the current upload/download throughput of all reporting
  adapters. Disconnected adapters report zero or no value; no cumulative
  byte-counter subtraction is used, so resets cannot create spikes.
- CPU temperature: `Core (Tctl/Tdie)`, `CPU Package`, `Core Average`, then the
  hottest CPU temperature sensor.
- GPU temperature: exact ordinary/core names only (`GPU Core`,
  `GPU Temperature`, `Core`). Hotspot, junction and memory temperatures are
  never relabelled; if no ordinary GPU temperature exists the value is `null`.
- Motherboard temperature: an exact `Motherboard`, `Mainboard`, `System`, or
  `System #1` sensor. Ambiguous VRM/chipset readings are not relabelled.

Missing or inaccessible readings are emitted as JSON `null`. Some temperature
and motherboard sensors require administrator rights because the upstream
library must access hardware registers. The helper deliberately does not
request elevation: it degrades to the sensors available to the current user,
and the companion remains usable without an elevation prompt.
