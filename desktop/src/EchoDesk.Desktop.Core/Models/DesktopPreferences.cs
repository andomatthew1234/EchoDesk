namespace EchoDesk.Desktop.Core.Models;

public sealed record DesktopPreferences
{
    public bool MinimizeToTray { get; init; } = true;
    public bool LaunchWithWindows { get; init; }
    public string Appearance { get; init; } = "system";
    public bool FirstRunCompleted { get; init; }
}
