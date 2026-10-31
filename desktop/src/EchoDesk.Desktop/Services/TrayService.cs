using Drawing = System.Drawing;
using Forms = System.Windows.Forms;

namespace EchoDesk.Desktop.Services;

public sealed class TrayService : IDisposable
{
    private readonly Forms.NotifyIcon icon;

    public event EventHandler? OpenRequested;
    public event EventHandler? ExitRequested;

    public TrayService()
    {
        var menu = new Forms.ContextMenuStrip();
        menu.Items.Add("Open EchoDesk", null, (_, _) => OpenRequested?.Invoke(this, EventArgs.Empty));
        menu.Items.Add(new Forms.ToolStripSeparator());
        menu.Items.Add("Exit", null, (_, _) => ExitRequested?.Invoke(this, EventArgs.Empty));
        Drawing.Icon trayIcon;
        try { trayIcon = Drawing.Icon.ExtractAssociatedIcon(Environment.ProcessPath!) ?? Drawing.SystemIcons.Application; }
        catch { trayIcon = Drawing.SystemIcons.Application; }
        icon = new Forms.NotifyIcon
        {
            Icon = trayIcon,
            Text = "EchoDesk Desktop · Connecting",
            Visible = true,
            ContextMenuStrip = menu,
        };
        icon.DoubleClick += (_, _) => OpenRequested?.Invoke(this, EventArgs.Empty);
    }

    public void Update(bool connected, string? port)
    {
        var status = connected ? $"Connected{(string.IsNullOrWhiteSpace(port) ? string.Empty : $" · {port}")}" : "Disconnected";
        icon.Text = $"EchoDesk Desktop · {status}"[..Math.Min(63, $"EchoDesk Desktop · {status}".Length)];
    }

    public void Dispose()
    {
        icon.Visible = false;
        icon.Dispose();
    }
}
