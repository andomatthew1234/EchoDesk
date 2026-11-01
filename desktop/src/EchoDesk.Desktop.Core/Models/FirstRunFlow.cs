namespace EchoDesk.Desktop.Core.Models;

public enum FirstRunStep
{
    Welcome,
    SetupPrompt,
    Complete,
}

/// <summary>
/// Host-independent navigation policy for the Desktop app's first launch.
/// Persistence is owned by the Windows preferences service.
/// </summary>
public sealed class FirstRunFlow(bool alreadyCompleted = false)
{
    public FirstRunStep Step { get; private set; } = alreadyCompleted
        ? FirstRunStep.Complete
        : FirstRunStep.Welcome;

    public bool Continue()
    {
        if (Step != FirstRunStep.Welcome) return false;
        Step = FirstRunStep.SetupPrompt;
        return true;
    }

    public bool Finish()
    {
        if (Step != FirstRunStep.SetupPrompt) return false;
        Step = FirstRunStep.Complete;
        return true;
    }
}
