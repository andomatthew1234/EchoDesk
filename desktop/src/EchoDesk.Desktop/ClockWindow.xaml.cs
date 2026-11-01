using System.Windows;
using EchoDesk.Desktop.ViewModels;

namespace EchoDesk.Desktop;

public partial class ClockWindow : Window
{
    private readonly MainViewModel viewModel;

    public ClockWindow(MainViewModel viewModel, string? requestedMode = null)
    {
        InitializeComponent();
        this.viewModel = viewModel;
        DataContext = viewModel;
        ShowOnlyRequestedMode(requestedMode);
    }

    public void ShowOnlyRequestedMode(string? requestedMode)
    {
        var mode = requestedMode is "countdown" or "focus" ? requestedMode : "stopwatch";
        StopwatchPanel.Visibility = mode == "stopwatch" ? Visibility.Visible : Visibility.Collapsed;
        CountdownPanel.Visibility = mode == "countdown" ? Visibility.Visible : Visibility.Collapsed;
        FocusPanel.Visibility = mode == "focus" ? Visibility.Visible : Visibility.Collapsed;
        WindowHeading.Text = mode switch
        {
            "countdown" => "Set a countdown",
            "focus" => "Set a focus timer",
            _ => "Stopwatch",
        };
        WindowSubheading.Text = mode == "stopwatch"
            ? "Control the stopwatch on your EchoDesk Mini."
            : "This setup is sent directly to your EchoDesk Mini.";
        Title = mode switch
        {
            "countdown" => "Configure EchoDesk Countdown",
            "focus" => "Configure EchoDesk Focus Timer",
            _ => "EchoDesk Stopwatch",
        };
    }

    private async void CountdownDone_Click(object sender, RoutedEventArgs e)
    {
        if (await viewModel.ConfigureCountdownForDeviceAsync()) Close();
    }

    private async void FocusDone_Click(object sender, RoutedEventArgs e)
    {
        if (await viewModel.ConfigureFocusForDeviceAsync()) Close();
    }

    private void Close_Click(object sender, RoutedEventArgs e) => Close();
}
