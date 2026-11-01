namespace EchoDesk.Desktop.Core.Models;

public enum GuidedSetupStep
{
    Terms,
    Connect,
    SelectDevice,
    Install,
    Complete,
}

public enum GuidedSetupTransport
{
    Usb,
    Wifi,
}

public sealed record GuidedSetupLegalDocuments(
    string? TermsPath,
    string? PrivacyPath,
    string? ThirdPartyNoticesPath)
{
    public bool HasTerms => IsReadableFile(TermsPath);
    public bool HasPrivacy => IsReadableFile(PrivacyPath);
    public bool HasRequiredDocuments => HasTerms && HasPrivacy;

    public static GuidedSetupLegalDocuments Discover(string applicationDirectory)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(applicationDirectory);
        var legalDirectory = Path.Combine(applicationDirectory, "legal");
        return new GuidedSetupLegalDocuments(
            Existing(Path.Combine(legalDirectory, "ECHODESK-TERMS.md")),
            Existing(Path.Combine(legalDirectory, "PRIVACY.md")),
            Existing(Path.Combine(applicationDirectory, "THIRD-PARTY-NOTICES.md")));
    }

    private static string? Existing(string path) => File.Exists(path) ? Path.GetFullPath(path) : null;
    private static bool IsReadableFile(string? path) => !string.IsNullOrWhiteSpace(path) && File.Exists(path);
}

/// <summary>
/// Pure setup navigation and selection policy. The WPF layer owns I/O, while
/// this object keeps the blank introductory step and device pinning
/// independently testable.
/// </summary>
public sealed class GuidedSetupFlow
{
    public GuidedSetupStep Step { get; private set; } = GuidedSetupStep.Terms;
    public GuidedSetupTransport Transport { get; private set; } = GuidedSetupTransport.Usb;
    public bool TermsAccepted => true;
    public string? SelectedPort { get; private set; }
    public string? PinnedPort { get; private set; }
    public string? SelectedWifiDeviceId { get; private set; }
    public string? PinnedWifiDeviceId { get; private set; }
    public bool SelectedDeviceRequiresConfirmation { get; private set; }
    public bool BoardConfirmed { get; private set; }

    public bool CanAcceptTerms => true;
    public bool CanContinue => Step switch
    {
        GuidedSetupStep.Terms => true,
        GuidedSetupStep.Connect => true,
        GuidedSetupStep.SelectDevice => Transport == GuidedSetupTransport.Usb
            ? !string.IsNullOrWhiteSpace(SelectedPort)
                && (!SelectedDeviceRequiresConfirmation || BoardConfirmed)
            : !string.IsNullOrWhiteSpace(SelectedWifiDeviceId),
        _ => false,
    };

    // Kept for binary/source compatibility with the original gated flow. The
    // introductory Terms/Privacy step is intentionally blank and never asks
    // the customer to accept text that has not been supplied.
    public void SetTermsAccepted(bool _) { }

    public void SelectTransport(GuidedSetupTransport transport)
    {
        if (Step != GuidedSetupStep.Connect) return;
        Transport = transport;
        SelectedPort = null;
        SelectedWifiDeviceId = null;
        SelectedDeviceRequiresConfirmation = false;
        BoardConfirmed = false;
    }

    public void Select(string? port, IReadOnlyList<FirmwareDeviceCandidate> devices)
    {
        if (PinnedPort is not null) return;
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
        if (PinnedWifiDeviceId is not null) return;
        var selected = devices.FirstOrDefault(candidate =>
            string.Equals(candidate.DeviceId, deviceId, StringComparison.Ordinal)
            && IsDiscoverable(candidate));
        SelectedWifiDeviceId = selected?.DeviceId;
    }

    public bool MoveNext()
    {
        if (!CanContinue) return false;
        Step = Step switch
        {
            GuidedSetupStep.Terms => GuidedSetupStep.Connect,
            GuidedSetupStep.Connect => GuidedSetupStep.SelectDevice,
            GuidedSetupStep.SelectDevice => PinAndInstall(),
            _ => Step,
        };
        return true;
    }

    public bool MoveBack()
    {
        if (Step is GuidedSetupStep.Install or GuidedSetupStep.Complete) return false;
        Step = Step switch
        {
            GuidedSetupStep.Connect => GuidedSetupStep.Terms,
            GuidedSetupStep.SelectDevice => GuidedSetupStep.Connect,
            _ => Step,
        };
        return true;
    }

    public bool Complete(bool writeVerified, bool reconnected, bool setupAcknowledged)
    {
        if (Step != GuidedSetupStep.Install || !writeVerified || !reconnected || !setupAcknowledged)
            return false;
        Step = GuidedSetupStep.Complete;
        return true;
    }

    public bool CompleteWifi(bool provisioned, string? selectedDeviceId)
    {
        if (Step != GuidedSetupStep.Install || Transport != GuidedSetupTransport.Wifi
            || !provisioned || string.IsNullOrWhiteSpace(PinnedWifiDeviceId)
            || !string.Equals(PinnedWifiDeviceId, selectedDeviceId, StringComparison.Ordinal))
        {
            return false;
        }
        Step = GuidedSetupStep.Complete;
        return true;
    }

    public void Reset()
    {
        Step = GuidedSetupStep.Terms;
        Transport = GuidedSetupTransport.Usb;
        SelectedPort = null;
        PinnedPort = null;
        SelectedWifiDeviceId = null;
        PinnedWifiDeviceId = null;
        SelectedDeviceRequiresConfirmation = false;
        BoardConfirmed = false;
    }

    public static bool IsDiscoverable(FirmwareDeviceCandidate candidate)
    {
        var modelMatches = string.Equals(
            candidate.HardwareModel, "EchoDesk Mini", StringComparison.OrdinalIgnoreCase);
        var provenApplication = candidate.EchoDeskHandshake && modelMatches;
        var provenBootloader = candidate.InBootloader
            && string.Equals(candidate.ChipFamily, "esp32", StringComparison.OrdinalIgnoreCase)
            && modelMatches;
        // Unproven USB serial candidates remain explicitly unidentified. They
        // are selectable only after the customer confirms the physical board;
        // the existing updater then probes the ROM and enforces ESP32/4 MB.
        return candidate.Compatible
            && (provenApplication || provenBootloader || candidate.RequiresBoardConfirmation);
    }

    public static bool IsDiscoverable(WifiSetupDevice candidate) =>
        !string.IsNullOrWhiteSpace(candidate.DeviceId)
        && string.Equals(candidate.Model, "Freenove FNK0114A_2P4", StringComparison.OrdinalIgnoreCase)
        && candidate.SetupMode
        && candidate.Port == 39321
        && !string.IsNullOrWhiteSpace(candidate.Address);

    private GuidedSetupStep PinAndInstall()
    {
        if (Transport == GuidedSetupTransport.Usb)
            PinnedPort = SelectedPort;
        else
            PinnedWifiDeviceId = SelectedWifiDeviceId;
        return GuidedSetupStep.Install;
    }
}
