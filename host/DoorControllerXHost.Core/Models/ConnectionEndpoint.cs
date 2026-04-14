namespace DoorControllerXHost.Core.Models;

public sealed class ConnectionEndpoint
{
    public required string Id { get; init; }

    public required string DisplayName { get; init; }

    public override string ToString()
    {
        return DisplayName;
    }
}