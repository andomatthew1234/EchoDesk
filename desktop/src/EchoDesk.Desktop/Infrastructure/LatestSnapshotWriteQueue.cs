namespace EchoDesk.Desktop.Infrastructure;

/// <summary>
/// Serialises whole-document writes while coalescing queued work to the newest
/// desired snapshot. A snapshot enqueued during an in-flight write is never
/// discarded; it becomes the ACK-driven follow-up write.
/// </summary>
public sealed class LatestSnapshotWriteQueue<T>(Func<T, long, Task> writer)
{
    private readonly object gate = new();
    private (T Value, long Generation)? latest;
    private bool running;
    private TaskCompletionSource idle = CompletedIdle();

    public void Enqueue(T value, long generation)
    {
        lock (gate)
        {
            latest = (value, generation);
            if (running) return;
            running = true;
            idle = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            _ = RunAsync();
        }
    }

    public Task WhenIdleAsync()
    {
        lock (gate) return idle.Task;
    }

    private async Task RunAsync()
    {
        while (true)
        {
            (T Value, long Generation)? next;
            lock (gate)
            {
                next = latest;
                latest = null;
                if (next is null)
                {
                    running = false;
                    idle.TrySetResult();
                    return;
                }
            }
            await writer(next.Value.Value, next.Value.Generation);
        }
    }

    private static TaskCompletionSource CompletedIdle()
    {
        var source = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        source.SetResult();
        return source;
    }
}

/// <summary>Defers persistence while a pointer-driven continuous edit is active.</summary>
public sealed class ContinuousEditCommitGate
{
    private int depth;
    private bool dirty;

    public void Begin() => depth++;

    /// <returns>True when the caller may start its normal debounce.</returns>
    public bool MarkChanged()
    {
        if (depth == 0)
        {
            dirty = false;
            return true;
        }
        dirty = true;
        return false;
    }

    /// <returns>True exactly once when the final active edit ends dirty.</returns>
    public bool End()
    {
        depth = Math.Max(0, depth - 1);
        if (depth != 0 || !dirty) return false;
        dirty = false;
        return true;
    }
}
