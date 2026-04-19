using System.IO.Ports;
using DoorControllerXHost.Core.Abstractions;
using DoorControllerXHost.Core.Models;
using DoorControllerXHost.Core.Protocol;

namespace DoorControllerXHost.Services;

internal sealed class DoorControllerSerialClient : ITransportClient
{
    private readonly SerialPort _port = new();
    private readonly object _sync = new();
    private readonly SemaphoreSlim _requestGate = new(1, 1);
    private readonly List<byte> _rxBuffer = [];
    private TaskCompletionSource<DoorControllerFrame>? _pendingReply;

    public bool IsOpen => _port.IsOpen;

    public Task<IReadOnlyList<ConnectionEndpoint>> GetAvailableEndpointsAsync(CancellationToken cancellationToken = default)
    {
        var endpoints = SerialPort.GetPortNames()
            .OrderBy(name => name, StringComparer.OrdinalIgnoreCase)
            .Select(name => new ConnectionEndpoint
            {
                Id = name,
                DisplayName = name,
            })
            .ToArray();

        return Task.FromResult<IReadOnlyList<ConnectionEndpoint>>(endpoints);
    }

    public Task ConnectAsync(string endpoint, int baudRate, CancellationToken cancellationToken = default)
    {
        Close();

        _port.PortName = endpoint;
        _port.BaudRate = baudRate;
        _port.DataBits = 8;
        _port.Parity = Parity.None;
        _port.StopBits = StopBits.One;
        _port.Handshake = Handshake.None;
        _port.ReadTimeout = 500;
        _port.WriteTimeout = 500;

        _port.DataReceived -= OnDataReceived;
        _port.DataReceived += OnDataReceived;
        _port.Open();

        return Task.CompletedTask;
    }

    public Task DisconnectAsync(CancellationToken cancellationToken = default)
    {
        _port.DataReceived -= OnDataReceived;
        if (_port.IsOpen)
        {
            _port.Close();
        }

        return Task.CompletedTask;
    }

    public void Close()
    {
        _ = DisconnectAsync();
    }

    public async Task<uint> ReadLiveAsync(byte id, int timeoutMs = 1000, CancellationToken cancellationToken = default)
    {
        var frame = await QueryAsync(DoorControllerProtocol.BuildReadLive(id), timeoutMs).ConfigureAwait(false);
        if (frame.Command == DoorControllerProtocol.CmdNak)
        {
            throw new InvalidOperationException($"Target returned NAK for live id {id}.");
        }
        if (frame.Command != DoorControllerProtocol.CmdAck || frame.Data.Length != 4)
        {
            throw new InvalidOperationException("Unexpected reply frame.");
        }
        return DoorControllerProtocol.ReadU32BigEndian(frame.Data);
    }

    public async Task<uint> ReadParamAsync(byte id, int timeoutMs = 1000, CancellationToken cancellationToken = default)
    {
        var frame = await QueryAsync(DoorControllerProtocol.BuildReadParam(id), timeoutMs).ConfigureAwait(false);
        if (frame.Command == DoorControllerProtocol.CmdNak)
        {
            throw new InvalidOperationException($"Target returned NAK for param id {id}.");
        }
        if (frame.Command != DoorControllerProtocol.CmdAck || frame.Data.Length != 4)
        {
            throw new InvalidOperationException("Unexpected reply frame.");
        }
        return DoorControllerProtocol.ReadU32BigEndian(frame.Data);
    }

    public async Task WriteParamAsync(byte id, uint value, int timeoutMs = 1000, CancellationToken cancellationToken = default)
    {
        var frame = await QueryAsync(DoorControllerProtocol.BuildWriteParam(id, value), timeoutMs).ConfigureAwait(false);
        if (frame.Command == DoorControllerProtocol.CmdNak)
        {
            throw new InvalidOperationException($"Target returned NAK for param id {id} write.");
        }
        if (frame.Command != DoorControllerProtocol.CmdAck)
        {
            throw new InvalidOperationException("Unexpected reply frame.");
        }
    }

    public async Task WriteLiveAsync(byte id, uint value, int timeoutMs = 1000, CancellationToken cancellationToken = default)
    {
        var frame = await QueryAsync(DoorControllerProtocol.BuildWriteLive(id, value), timeoutMs).ConfigureAwait(false);
        if (frame.Command == DoorControllerProtocol.CmdNak)
        {
            throw new InvalidOperationException($"Target returned NAK for live id {id} write.");
        }
        if (frame.Command != DoorControllerProtocol.CmdAck)
        {
            throw new InvalidOperationException("Unexpected reply frame.");
        }
    }

    private async Task<DoorControllerFrame> QueryAsync(byte[] requestFrame, int timeoutMs)
    {
        if (!_port.IsOpen)
        {
            throw new InvalidOperationException("Serial port is not open.");
        }

        await _requestGate.WaitAsync().ConfigureAwait(false);

        try
        {
            lock (_sync)
            {
                _pendingReply = new TaskCompletionSource<DoorControllerFrame>(TaskCreationOptions.RunContinuationsAsynchronously);
                _rxBuffer.Clear();
            }

            using var cts = new CancellationTokenSource(timeoutMs);
            using var reg = cts.Token.Register(() =>
            {
                lock (_sync)
                {
                    _pendingReply?.TrySetCanceled(cts.Token);
                }
            });

            _port.Write(requestFrame, 0, requestFrame.Length);
            Task<DoorControllerFrame> waitTask;
            lock (_sync)
            {
                waitTask = _pendingReply!.Task;
            }
            return await waitTask.ConfigureAwait(false);
        }
        finally
        {
            lock (_sync)
            {
                _pendingReply = null;
            }
            _requestGate.Release();
        }
    }

    private void OnDataReceived(object? sender, SerialDataReceivedEventArgs e)
    {
        var count = _port.BytesToRead;
        if (count <= 0)
        {
            return;
        }

        var chunk = new byte[count];
        _port.Read(chunk, 0, chunk.Length);

        lock (_sync)
        {
            _rxBuffer.AddRange(chunk);
            while (DoorControllerProtocol.TryParseFrame(_rxBuffer, out var frame))
            {
                if (frame == null)
                {
                    break;
                }
                _pendingReply?.TrySetResult(frame);
            }
        }
    }

    public void Dispose()
    {
        Close();
        _port.Dispose();
    }
}
