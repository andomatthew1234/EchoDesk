using System.Windows;
using EchoDesk.Desktop.ViewModels;

namespace EchoDesk.Desktop;

public partial class ClockWindow : Window
{
    public ClockWindow(MainViewModel viewModel, string? requestedMode = null)
    {
        InitializeComponent();
        DataContext = viewModel;
        if (requestedMode is "countdown" or "focus")
            Title = $"Configure EchoDesk {requestedMode}";
    }
}
