using System.Text.Json;
using System.IO;

namespace EchoDesk.Desktop.Services;

public sealed record DesktopPreferences
{
    public bool MinimizeToTray { get; init; } = true;
    public bool LaunchWithWindows { get; init; }
    public string Appearance { get; init; } = "system";
}

public sealed class PreferencesService
{
    private static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web) { WriteIndented = true };
    private readonly string path = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "EchoDesk Desktop", "settings.json");

    public DesktopPreferences Load()
    {
        try
        {
            return File.Exists(path)
                ? JsonSerializer.Deserialize<DesktopPreferences>(File.ReadAllText(path), Json) ?? new()
                : new();
        }
        catch (JsonException) { return new(); }
        catch (IOException) { return new(); }
    }

    public void Save(DesktopPreferences value)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var temporary = path + ".tmp";
        File.WriteAllText(temporary, JsonSerializer.Serialize(value, Json));
        File.Move(temporary, path, true);
    }
}
