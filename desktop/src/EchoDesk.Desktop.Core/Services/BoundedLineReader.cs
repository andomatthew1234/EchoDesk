using System.Text;
using System.Runtime.InteropServices;

namespace EchoDesk.Desktop.Core.Services;

internal sealed class BoundedLineReader(Stream stream)
{
    private readonly byte[] readBuffer = new byte[4096];
    private readonly List<byte> current = new(4096);
    private readonly Queue<string> ready = new();

    public async Task<string?> ReadLineAsync(CancellationToken cancellationToken)
    {
        while (ready.Count == 0)
        {
            var count = await stream.ReadAsync(readBuffer, cancellationToken).ConfigureAwait(false);
            if (count == 0)
            {
                return current.Count == 0 ? null : throw new EndOfStreamException("Broker closed mid-message.");
            }

            for (var index = 0; index < count; index++)
            {
                var value = readBuffer[index];
                if (value == (byte)'\n')
                {
                    if (current.Count > 0 && current[^1] == (byte)'\r')
                    {
                        current.RemoveAt(current.Count - 1);
                    }

                    ready.Enqueue(Encoding.UTF8.GetString(CollectionsMarshal.AsSpan(current)));
                    current.Clear();
                    continue;
                }

                current.Add(value);
                if (current.Count > BrokerProtocol.MaximumLineBytes)
                {
                    throw new InvalidDataException("Broker message exceeded 64 KiB.");
                }
            }
        }

        return ready.Dequeue();
    }
}
