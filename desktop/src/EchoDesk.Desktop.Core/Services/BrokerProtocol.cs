using System.Text.Json;
using System.Text.Json.Serialization;

namespace EchoDesk.Desktop.Core.Services;

public static class BrokerProtocol
{
    public const string PipeName = "EchoDesk.Companion.v1";
    public const int Version = 1;
    public const int MaximumLineBytes = 64 * 1024;

    public static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web)
    {
        PropertyNameCaseInsensitive = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
    };
}

public sealed record BrokerError(
    [property: JsonPropertyName("code")] string Code,
    [property: JsonPropertyName("message")] string Message);

internal sealed record BrokerEnvelope
{
    [JsonPropertyName("version")]
    public int Version { get; init; }

    [JsonPropertyName("id")]
    public string? Id { get; init; }

    [JsonPropertyName("event")]
    public string? Event { get; init; }

    [JsonPropertyName("ok")]
    public bool? Ok { get; init; }

    [JsonPropertyName("error")]
    public BrokerError? Error { get; init; }

    [JsonPropertyName("result")]
    public JsonElement? Result { get; init; }

    [JsonPropertyName("data")]
    public JsonElement? Data { get; init; }
}

public sealed class BrokerException(string code, string message) : Exception(message)
{
    public string Code { get; } = code;
}
