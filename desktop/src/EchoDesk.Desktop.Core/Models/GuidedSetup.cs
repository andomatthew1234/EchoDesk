namespace EchoDesk.Desktop.Core.Models;

public enum GuidedSetupStep
{
    Connect,
    SelectDevice,
    ChoosePath,
    Install,
    QuickSettings,
    Complete,
}

public enum GuidedSetupTransport { Usb, Wifi }

public enum GuidedWifiStep { JoinTemporaryNetwork, FindDevice, EnterCredentials, Provisioning }

/// <summary>
/// Pure navigation and target-pinning policy for first-run setup. I/O remains
/// in the WPF view model; this prevents a target swap after setup has started.
/// </summary>
public sealed class GuidedSetupFlow
{
    public GuidedSetupStep Step { get; private set; } = GuidedSetupStep.Connect;
    public GuidedSetupTransport Transport { get; private set; } = GuidedSetupTransport.Usb;
    public GuidedWifiStep WifiStep { get; private set; } = GuidedWifiStep.JoinTemporaryNetwork;
    public string? SelectedPort { get; private set; }
    public string? PinnedPort { get; private set; }
    public string? SelectedWifiDeviceId { get; private set; }
    public string? PinnedWifiDeviceId { get; private set; }
    public bool SelectedDeviceRequiresConfirmation { get; private set; }
    public bool BoardConfirmed { get; private set; }

    public bool CanContinue => Step == GuidedSetupStep.SelectDevice
        && (Transport == GuidedSetupTransport.Usb
            ? !string.IsNullOrWhiteSpace(SelectedPort)
              && (!SelectedDeviceRequiresConfirmation || BoardConfirmed)
            : !string.IsNullOrWhiteSpace(SelectedWifiDeviceId));

    public void SelectTransport(GuidedSetupTransport transport)
    {
        if (Step != GuidedSetupStep.Connect) return;
        Transport = transport;
        Step = GuidedSetupStep.SelectDevice;
        WifiStep = GuidedWifiStep.JoinTemporaryNetwork;
        ClearSelections();
    }

    public void Select(string? port, IReadOnlyList<FirmwareDeviceCandidate> devices)
    {
        if (PinnedPort is not null || Step != GuidedSetupStep.SelectDevice) return;
        var selected = devices.FirstOrDefault(candidate =>
            string.Equals(candidate.Port, port, StringComparison.OrdinalIgnoreCase)
            && IsDiscoverable(candidate));
        SelectedPort = selected?.Port;
        SelectedDeviceRequiresConfirmation = selected?.RequiresBoardConfirmation == true;
        BoardConfirmed = false;
    }

    public void SetBoardConfirmed(bool confirmed) =>
        BoardConfirmed = SelectedDeviceRequiresConfirmation && confirmed;

    public void SelectWifi(string? deviceId, IReadOnlyList<WifiSetupDevice> devices)
    {
        if (PinnedWifiDeviceId is not null || Step != GuidedSetupStep.SelectDevice) return;
        var selected = devices.FirstOrDefault(candidate =>
            string.Equals(candidate.DeviceId, deviceId, StringComparison.Ordinal)
            && IsDiscoverable(candidate));
        SelectedWifiDeviceId = selected?.DeviceId;
    }

    public bool AdvanceWifi()
    {
        if (Transport != GuidedSetupTransport.Wifi || Step != GuidedSetupStep.SelectDevice) return false;
        var next = WifiStep switch
        {
            GuidedWifiStep.JoinTemporaryNetwork => GuidedWifiStep.FindDevice,
            GuidedWifiStep.FindDevice when SelectedWifiDeviceId is not null => GuidedWifiStep.EnterCredentials,
            _ => WifiStep,
        };
        if (next == WifiStep) return false;
        WifiStep = next;
        return true;
    }

    public bool MoveBack()
    {
        if (Step == GuidedSetupStep.SelectDevice && Transport == GuidedSetupTransport.Wifi
            && WifiStep != GuidedWifiStep.JoinTemporaryNetwork)
        {
            WifiStep = WifiStep == GuidedWifiStep.EnterCredentials
                ? GuidedWifiStep.FindDevice
                : GuidedWifiStep.JoinTemporaryNetwork;
            return true;
        }
        if (Step == GuidedSetupStep.SelectDevice)
        {
            Step = GuidedSetupStep.Connect;
            ClearSelections();
            return true;
        }
        if (Step == GuidedSetupStep.ChoosePath)
        {
            Step = GuidedSetupStep.SelectDevice;
            PinnedPort = null;
            return true;
        }
        return false;
    }

    public bool ConfirmDevice()
    {
        if (!CanContinue || Transport != GuidedSetupTransport.Usb) return false;
        PinnedPort = SelectedPort;
        Step = GuidedSetupStep.ChoosePath;
        return true;
    }

    public bool GetStartedNow()
    {
        if (Step != GuidedSetupStep.ChoosePath || PinnedPort is null) return false;
        Step = GuidedSetupStep.QuickSettings;
        return true;
    }

    public bool StartUpdateCheck()
    {
        if (Step != GuidedSetupStep.ChoosePath || PinnedPort is null) return false;
        Step = GuidedSetupStep.Install;
        return true;
    }

    public bool FinishInstall(bool writeVerified, bool reconnected)
    {
        if (Step != GuidedSetupStep.Install || !writeVerified || !reconnected) return false;
        Step = GuidedSetupStep.QuickSettings;
        return true;
    }

    public bool SkipInstallWhenCurrent()
    {
        if (Step != GuidedSetupStep.Install) return false;
        Step = GuidedSetupStep.QuickSettings;
        return true;
    }

    public bool BeginWifiProvisioning()
    {
        if (!CanContinue || Transport != GuidedSetupTransport.Wifi
            || WifiStep != GuidedWifiStep.EnterCredentials) return false;
        PinnedWifiDeviceId = SelectedWifiDeviceId;
        WifiStep = GuidedWifiStep.Provisioning;
        Step = GuidedSetupStep.Install;
        return true;
    }

    public bool CompleteWifi(bool provisioned, string? selectedDeviceId)
    {
        if (Step != GuidedSetupStep.Install || Transport != GuidedSetupTransport.Wifi
            || !provisioned || string.IsNullOrWhiteSpace(PinnedWifiDeviceId)
            || !string.Equals(PinnedWifiDeviceId, selectedDeviceId, StringComparison.Ordinal)) return false;
        Step = GuidedSetupStep.Complete;
        return true;
    }

    public bool Complete()
    {
        if (Step != GuidedSetupStep.QuickSettings) return false;
        Step = GuidedSetupStep.Complete;
        return true;
    }

    public void Reset()
    {
        Step = GuidedSetupStep.Connect;
        Transport = GuidedSetupTransport.Usb;
        WifiStep = GuidedWifiStep.JoinTemporaryNetwork;
        PinnedPort = null;
        PinnedWifiDeviceId = null;
        ClearSelections();
    }

    public static bool IsKnown(FirmwareDeviceCandidate candidate) => candidate.Compatible
        && (string.Equals(candidate.IdentityStatus, "verified", StringComparison.OrdinalIgnoreCase)
            || string.Equals(candidate.IdentityStatus, "bootloader", StringComparison.OrdinalIgnoreCase)
            || (candidate.EchoDeskHandshake && string.Equals(
                candidate.HardwareModel, "EchoDesk Mini", StringComparison.OrdinalIgnoreCase)));

    public static bool IsUnknown(FirmwareDeviceCandidate candidate) => candidate.Compatible
        && candidate.RequiresBoardConfirmation && !IsKnown(candidate);

    public static bool IsDiscoverable(FirmwareDeviceCandidate candidate) => IsKnown(candidate) || IsUnknown(candidate);

    public static bool IsDiscoverable(WifiSetupDevice candidate) =>
        !string.IsNullOrWhiteSpace(candidate.DeviceId)
        && string.Equals(candidate.Model, "Freenove FNK0114A_2P4", StringComparison.OrdinalIgnoreCase)
        && candidate.SetupMode && candidate.Port == 39321 && !string.IsNullOrWhiteSpace(candidate.Address);

    private void ClearSelections()
    {
        SelectedPort = null;
        SelectedWifiDeviceId = null;
        SelectedDeviceRequiresConfirmation = false;
        BoardConfirmed = false;
    }
}
