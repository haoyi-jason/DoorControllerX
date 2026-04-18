using Android.Bluetooth;
using Android.Content;
using Android.OS;
using DoorControllerXHost.Core.Abstractions;
using DoorControllerXHost.Core.Models;
using DoorControllerXHost.Core.Protocol;
using Java.Util;
using Microsoft.Maui.ApplicationModel;
using Application = Android.App.Application;

namespace DoorControllerXHost.Maui.Platforms.Android.Services;

internal sealed class AndroidBluetoothTransport : ITransportClient
{
    private static readonly UUID SerialPortUuid = UUID.FromString("00001101-0000-1000-8000-00805F9B34FB")!;

    private readonly object _sync = new();
    private readonly SemaphoreSlim _requestGate = new(1, 1);
    private readonly List<byte> _rxBuffer = [];
    private TaskCompletionSource<DoorControllerFrame>? _pendingReply;
    private BluetoothSocket? _socket;
    private Stream? _inputStream;
    private Stream? _outputStream;
    private CancellationTokenSource? _readerCts;
    private Task? _readerTask;

    public bool IsOpen => _socket?.IsConnected == true;

    public async Task<IReadOnlyList<ConnectionEndpoint>> GetAvailableEndpointsAsync(CancellationToken cancellationToken = default)
    {
        if (Build.VERSION.SdkInt >= BuildVersionCodes.S)
        {
            await EnsureBluetoothPermissionsAsync().ConfigureAwait(false);
        }

        var adapter = GetBluetoothAdapter();
        if (adapter == null)
        {
            return [];
        }

        var devices = adapter.BondedDevices?
            .Select(device => new ConnectionEndpoint
            {
                Id = device.Address ?? string.Empty,
                DisplayName = string.IsNullOrWhiteSpace(device.Name) ? device.Address ?? string.Empty : $"{device.Name} ({device.Address})",
            })
            .Where(endpoint => !string.IsNullOrWhiteSpace(endpoint.Id))
            .OrderBy(endpoint => endpoint.DisplayName, StringComparer.OrdinalIgnoreCase)
            .ToArray();

        return devices ?? [];
    }

    public async Task ConnectAsync(string endpoint, int baudRate, CancellationToken cancellationToken = default)
    {
        _ = baudRate;

        await DisconnectAsync(cancellationToken).ConfigureAwait(false);
        if (Build.VERSION.SdkInt >= BuildVersionCodes.S)
        {
            await EnsureBluetoothPermissionsAsync().ConfigureAwait(false);
        }

        var adapter = GetBluetoothAdapter();
        if (adapter == null)
        {
            throw new NotSupportedException("Bluetooth adapter not available on this device.");
        }

        if (!adapter.IsEnabled)
        {
            throw new InvalidOperationException("Bluetooth is disabled. Please enable Bluetooth first.");
        }

        var device = FindBondedDevice(adapter, endpoint);
        if (device == null)
        {
            throw new InvalidOperationException($"Paired Bluetooth device not found: {endpoint}");
        }

        adapter.CancelDiscovery();

        var socket = device.CreateRfcommSocketToServiceRecord(SerialPortUuid);
        await Task.Run(() => socket.Connect(), cancellationToken).ConfigureAwait(false);

        var readerCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);

        _socket = socket;
        _inputStream = socket.InputStream!;
        _outputStream = socket.OutputStream!;
        _readerCts = readerCts;
        _readerTask = Task.Run(() => ReaderLoop(readerCts.Token), readerCts.Token);
    }

    public async Task DisconnectAsync(CancellationToken cancellationToken = default)
    {
        CancellationTokenSource? readerCts;
        Task? readerTask;
        BluetoothSocket? socket;
        Stream? inputStream;
        Stream? outputStream;

        lock (_sync)
        {
            readerCts = _readerCts;
            readerTask = _readerTask;
            socket = _socket;
            inputStream = _inputStream;
            outputStream = _outputStream;

            _readerCts = null;
            _readerTask = null;
            _socket = null;
            _inputStream = null;
            _outputStream = null;
            _pendingReply?.TrySetCanceled(cancellationToken);
            _pendingReply = null;
            _rxBuffer.Clear();
        }

        readerCts?.Cancel();

        if (outputStream != null)
        {
            await outputStream.DisposeAsync().ConfigureAwait(false);
        }

        if (inputStream != null)
        {
            await inputStream.DisposeAsync().ConfigureAwait(false);
        }

        socket?.Close();
        socket?.Dispose();

        if (readerTask != null)
        {
            try
            {
                await readerTask.ConfigureAwait(false);
            }
            catch (System.OperationCanceledException)
            {
            }
        }

        readerCts?.Dispose();
    }

    public async Task<uint> ReadLiveAsync(byte id, int timeoutMs = 1000, CancellationToken cancellationToken = default)
    {
        var frame = await QueryAsync(DoorControllerProtocol.BuildReadLive(id), timeoutMs, cancellationToken).ConfigureAwait(false);
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
        var frame = await QueryAsync(DoorControllerProtocol.BuildReadParam(id), timeoutMs, cancellationToken).ConfigureAwait(false);
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
        var frame = await QueryAsync(DoorControllerProtocol.BuildWriteParam(id, value), timeoutMs, cancellationToken).ConfigureAwait(false);
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
        var frame = await QueryAsync(DoorControllerProtocol.BuildWriteLive(id, value), timeoutMs, cancellationToken).ConfigureAwait(false);
        if (frame.Command == DoorControllerProtocol.CmdNak)
        {
            throw new InvalidOperationException($"Target returned NAK for live id {id} write.");
        }
        if (frame.Command != DoorControllerProtocol.CmdAck)
        {
            throw new InvalidOperationException("Unexpected reply frame.");
        }
    }

    public void Dispose()
    {
        DisconnectAsync().GetAwaiter().GetResult();
    }

    private static BluetoothAdapter? GetBluetoothAdapter()
    {
        var manager = (BluetoothManager?)Application.Context.GetSystemService(Context.BluetoothService);
        return manager?.Adapter;
    }

    private static BluetoothDevice? FindBondedDevice(BluetoothAdapter adapter, string endpoint)
    {
        var normalized = endpoint.Trim();
        return adapter.BondedDevices?.FirstOrDefault(device =>
            string.Equals(device.Address, normalized, StringComparison.OrdinalIgnoreCase) ||
            string.Equals(device.Name, normalized, StringComparison.OrdinalIgnoreCase));
    }

    private async Task<DoorControllerFrame> QueryAsync(byte[] requestFrame, int timeoutMs, CancellationToken cancellationToken)
    {
        if (!IsOpen || _outputStream == null)
        {
            throw new InvalidOperationException("Bluetooth transport is not connected.");
        }

        await _requestGate.WaitAsync(cancellationToken).ConfigureAwait(false);

        try
        {
            lock (_sync)
            {
                _pendingReply = new TaskCompletionSource<DoorControllerFrame>(TaskCreationOptions.RunContinuationsAsynchronously);
                _rxBuffer.Clear();
            }

            using var cts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            cts.CancelAfter(timeoutMs);
            using var reg = cts.Token.Register(() =>
            {
                lock (_sync)
                {
                    _pendingReply?.TrySetCanceled(cts.Token);
                }
            });

            await _outputStream.WriteAsync(requestFrame, 0, requestFrame.Length, cts.Token).ConfigureAwait(false);
            await _outputStream.FlushAsync(cts.Token).ConfigureAwait(false);

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

    private async Task EnsureBluetoothPermissionsAsync()
    {
        if (Build.VERSION.SdkInt < BuildVersionCodes.S)
        {
            return;
        }

        var connectStatus = await Permissions.CheckStatusAsync<BluetoothConnectPermission>().ConfigureAwait(false);
        if (connectStatus != PermissionStatus.Granted)
        {
            connectStatus = await Permissions.RequestAsync<BluetoothConnectPermission>().ConfigureAwait(false);
            if (connectStatus != PermissionStatus.Granted)
            {
                throw new InvalidOperationException("Bluetooth connect permission denied.");
            }
        }

        var scanStatus = await Permissions.CheckStatusAsync<BluetoothScanPermission>().ConfigureAwait(false);
        if (scanStatus != PermissionStatus.Granted)
        {
            scanStatus = await Permissions.RequestAsync<BluetoothScanPermission>().ConfigureAwait(false);
            if (scanStatus != PermissionStatus.Granted)
            {
                throw new InvalidOperationException("Bluetooth scan permission denied.");
            }
        }
    }

    private void ReaderLoop(CancellationToken cancellationToken)
    {
        if (_inputStream == null)
        {
            return;
        }

        var buffer = new byte[256];
        while (!cancellationToken.IsCancellationRequested)
        {
            int bytesRead;
            try
            {
                bytesRead = _inputStream.Read(buffer, 0, buffer.Length);
            }
            catch (Exception) when (cancellationToken.IsCancellationRequested)
            {
                break;
            }
            catch (Exception ex)
            {
                lock (_sync)
                {
                    _pendingReply?.TrySetException(ex);
                }
                break;
            }

            if (bytesRead <= 0)
            {
                continue;
            }

            lock (_sync)
            {
                _rxBuffer.AddRange(buffer[..bytesRead]);
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
    }
}
