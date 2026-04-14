namespace DoorControllerXHost.Core.Protocol;

public sealed class DoorControllerFrame
{
    public required byte Command { get; init; }

    public required byte[] Data { get; init; }
}
