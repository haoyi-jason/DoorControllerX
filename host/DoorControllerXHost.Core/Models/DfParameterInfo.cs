namespace DoorControllerXHost.Core.Models;

public sealed class DfParameterInfo
{
    public required DfParamId Id { get; init; }

    public required string Name { get; init; }

    public required uint Min { get; init; }

    public required uint Max { get; init; }

    public required uint Default { get; init; }

    public override string ToString()
    {
        return $"{(byte)Id:D2} {Name}";
    }
}
