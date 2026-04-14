namespace DoorControllerXHost.Core.Abstractions;

using DoorControllerXHost.Core.Models;

public interface ITransportClient : IDisposable
{
    bool IsOpen { get; }

    Task<IReadOnlyList<ConnectionEndpoint>> GetAvailableEndpointsAsync(CancellationToken cancellationToken = default);

    Task ConnectAsync(string endpoint, int baudRate, CancellationToken cancellationToken = default);

    Task DisconnectAsync(CancellationToken cancellationToken = default);

    Task<uint> ReadLiveAsync(byte id, int timeoutMs = 1000, CancellationToken cancellationToken = default);

    Task<uint> ReadParamAsync(byte id, int timeoutMs = 1000, CancellationToken cancellationToken = default);

    Task WriteParamAsync(byte id, uint value, int timeoutMs = 1000, CancellationToken cancellationToken = default);
}
