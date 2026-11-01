using System.IO;
using System.Windows;
using System.Windows.Controls;
using EchoDesk.Desktop.Core.Services;
using EchoDesk.Desktop.Services;
using EchoDesk.Desktop.ViewModels;

namespace EchoDesk.Desktop.LayoutTests;

internal static class Program
{
    [STAThread]
    private static int Main()
    {
        try
        {
            var settingsPath = Path.Combine(Path.GetTempPath(), $"echodesk-layout-{Guid.NewGuid():N}", "settings.json");
            var app = new App { ShutdownMode = ShutdownMode.OnExplicitShutdown };
            app.InitializeComponent();
            var viewModel = new MainViewModel(new BrokerClient(), new PreferencesService(settingsPath))
            {
                MinimizeToTray = false,
            };
            var window = new MainWindow(viewModel)
            {
                ShowActivated = false,
                ShowInTaskbar = false,
                Opacity = 0,
                WindowStyle = WindowStyle.None,
            };
            VerifyResponsivePair(window, viewModel, "Bedside Mode", "BedsideLayout");
            VerifyResponsivePair(window, viewModel, "Device recovery & updates", "RecoveryDeviceLayout");

            window.Close();
            viewModel.DisposeAsync().AsTask().GetAwaiter().GetResult();
            app.Shutdown();
            Console.WriteLine("EchoDesk.Desktop.LayoutTests: PASS");
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"EchoDesk.Desktop.LayoutTests: FAIL\n{error}");
            return 1;
        }
    }

    private static void VerifyResponsivePair(MainWindow window, MainViewModel viewModel,
                                             string page, string layoutName)
    {
        viewModel.SelectedPage = page;
        foreach (var (width, compact) in new[]
                 {
                     (680d, true), (789d, true), (790d, true), (900d, true),
                     (999d, true), (1000d, false), (1080d, false),
                 })
        {
            VerifyAtSize(window, viewModel, layoutName, width, width < 800 ? 600 : 720, compact);
        }
    }

    private static void VerifyAtSize(MainWindow window, MainViewModel viewModel, string layoutName,
                                     double width, double height, bool compact)
    {
        window.Width = width;
        window.Height = height;
        window.ApplyResponsiveLayout(width);
        var root = window.Content as FrameworkElement
            ?? throw new InvalidOperationException("The EchoDesk window root was not created.");
        root.Measure(new Size(width, height));
        root.Arrange(new Rect(0, 0, width, height));
        root.UpdateLayout();

        Assert(viewModel.IsCompact == compact,
            $"{layoutName} compact state did not match {width} px window width.");
        var layout = window.FindName(layoutName) as Grid
            ?? throw new InvalidOperationException($"{layoutName} was not found.");
        Assert(layout.ActualWidth > 0 && layout.ActualHeight > 0,
            $"{layoutName} did not receive a measurable layout slot.");

        var visible = layout.Children.Cast<UIElement>()
            .Where(child => child.Visibility == Visibility.Visible && child.RenderSize.Width > 0 && child.RenderSize.Height > 0)
            .Select(child => new Rect(child.TranslatePoint(new Point(), layout), child.RenderSize))
            .ToArray();
        Assert(visible.Length >= 2, $"{layoutName} did not render both primary regions.");
        for (var left = 0; left < visible.Length; left++)
        {
            Assert(visible[left].Left >= -0.5 && visible[left].Right <= layout.ActualWidth + 0.5,
                $"{layoutName} region overflowed horizontally at {width} px.");
            for (var right = left + 1; right < visible.Length; right++)
            {
                var intersection = Rect.Intersect(visible[left], visible[right]);
                Assert(intersection.IsEmpty || intersection.Width <= 0.5 || intersection.Height <= 0.5,
                    $"{layoutName} regions overlap at {width} px.");
            }
        }
        if (compact)
        {
            Assert(visible[1].Top >= visible[0].Bottom - 0.5,
                $"{layoutName} did not stack vertically at compact width.");
        }
    }

    private static void Assert(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
