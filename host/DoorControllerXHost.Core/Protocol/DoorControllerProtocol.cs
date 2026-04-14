using System.Buffers.Binary;

namespace DoorControllerXHost.Core.Protocol;

public static class DoorControllerProtocol
{
    public const byte Stx = 0xAA;
    public const byte Etx = 0x55;

    public const byte CmdReadParam = 0x01;
    public const byte CmdWriteParam = 0x02;
    public const byte CmdReadLive = 0x03;
    public const byte CmdWriteLive = 0x04;
    public const byte CmdAck = 0xF0;
    public const byte CmdNak = 0xF1;

    public static byte[] BuildReadParam(byte id)
    {
        return BuildFrame(CmdReadParam, [id]);
    }

    public static byte[] BuildReadLive(byte id)
    {
        return BuildFrame(CmdReadLive, [id]);
    }

    public static byte[] BuildWriteParam(byte id, uint value)
    {
        Span<byte> data = stackalloc byte[5];
        data[0] = id;
        BinaryPrimitives.WriteUInt32BigEndian(data[1..], value);
        return BuildFrame(CmdWriteParam, data);
    }

    public static byte[] BuildFrame(byte command, ReadOnlySpan<byte> data)
    {
        var length = checked((byte)(1 + data.Length));
        var frame = new byte[data.Length + 5];
        frame[0] = Stx;
        frame[1] = length;
        frame[2] = command;
        data.CopyTo(frame.AsSpan(3));
        frame[3 + data.Length] = ComputeCrc(frame.AsSpan(1, length + 1));
        frame[4 + data.Length] = Etx;
        return frame;
    }

    public static bool TryParseFrame(List<byte> buffer, out DoorControllerFrame? frame)
    {
        frame = null;

        while (buffer.Count > 0 && buffer[0] != Stx)
        {
            buffer.RemoveAt(0);
        }

        if (buffer.Count < 5)
        {
            return false;
        }

        var length = buffer[1];
        var totalLength = length + 4;
        if (buffer.Count < totalLength)
        {
            return false;
        }

        if (buffer[totalLength - 1] != Etx)
        {
            buffer.RemoveAt(0);
            return false;
        }

        var rxCrc = buffer[totalLength - 2];
        var calcCrc = ComputeCrc(buffer.Skip(1).Take(length + 1).ToArray());
        if (rxCrc != calcCrc)
        {
            buffer.RemoveRange(0, totalLength);
            return false;
        }

        var command = buffer[2];
        var dataLength = length - 1;
        var data = buffer.Skip(3).Take(dataLength).ToArray();
        frame = new DoorControllerFrame
        {
            Command = command,
            Data = data,
        };

        buffer.RemoveRange(0, totalLength);
        return true;
    }

    public static byte ComputeCrc(ReadOnlySpan<byte> bytes)
    {
        byte crc = 0;
        foreach (var value in bytes)
        {
            crc ^= value;
        }
        return crc;
    }

    public static uint ReadU32BigEndian(ReadOnlySpan<byte> bytes)
    {
        return BinaryPrimitives.ReadUInt32BigEndian(bytes);
    }
}
