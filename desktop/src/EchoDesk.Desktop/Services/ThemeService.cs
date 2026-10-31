using Microsoft.Win32;
using System.Windows;
using System.Windows.Media;

namespace EchoDesk.Desktop.Services;

public static class ThemeService
{
    public static void Apply(string preference)
    {
        var dark = preference.Equals("dark", StringComparison.OrdinalIgnoreCase)
                   || (preference.Equals("system", StringComparison.OrdinalIgnoreCase) && SystemUsesDark());
        Set("WindowBrush", dark ? "#FF0D0D0F" : "#FFF4F4F5");
        Set("SurfaceBrush", dark ? "#FF19191C" : "#FFFFFFFF");
        Set("SurfaceAltBrush", dark ? "#FF252529" : "#FFF0F0F1");
        Set("TextBrush", dark ? "#FFF7F7F8" : "#FF111113");
        Set("MutedTextBrush", dark ? "#FFA7A7AD" : "#FF66666C");
        Set("BorderBrush", dark ? "#26FFFFFF" : "#1A000000");
        Set("AccentBrush", dark ? "#FFF7F7F8" : "#FF111113");
        Set("AccentTextBrush", dark ? "#FF111113" : "#FFFFFFFF");
        var text = (SolidColorBrush)System.Windows.Application.Current.Resources["TextBrush"];
        foreach (System.Windows.Window window in System.Windows.Application.Current.Windows)
        {
            // Native WPF controls can establish their own foreground
            // inheritance boundary. A local window value guarantees that a
            // live theme switch updates ordinary text immediately.
            window.Foreground = text;
        }
    }

    public static bool IsDark(string preference) =>
        preference.Equals("dark", StringComparison.OrdinalIgnoreCase)
        || (preference.Equals("system", StringComparison.OrdinalIgnoreCase) && SystemUsesDark());

    private static bool SystemUsesDark()
    {
        try
        {
            using var key = Registry.CurrentUser.OpenSubKey(
                @"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize");
            return key?.GetValue("AppsUseLightTheme") is int value && value == 0;
        }
        catch { return false; }
    }

    private static void Set(string key, string color) =>
        System.Windows.Application.Current.Resources[key] = new SolidColorBrush(
            (System.Windows.Media.Color)System.Windows.Media.ColorConverter.ConvertFromString(color));
}
