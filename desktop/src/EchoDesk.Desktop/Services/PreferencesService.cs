using System.Text.Json;
using System.IO;
using EchoDesk.Desktop.Core.Models;

namespace EchoDesk.Desktop.Services;

public sealed class PreferencesService
{
    private static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web) { WriteIndented = true };
    private readonly string path;

    public PreferencesService(string? settingsPath = null)
    {
        path = settingsPath ?? Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "EchoDesk Desktop", "settings.json");
    }

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
