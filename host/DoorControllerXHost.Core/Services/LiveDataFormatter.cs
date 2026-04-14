using DoorControllerXHost.Core.Models;

namespace DoorControllerXHost.Core.Services;

public static class LiveDataFormatter
{
    public static string FormatValue(LiveDataId id, uint raw)
    {
        return id switch
        {
            LiveDataId.SysState => $"{raw} ({FormatSysState(raw)})",
            LiveDataId.BlockSourceState => $"{raw} ({FormatSysState(raw)})",
            LiveDataId.M1State or LiveDataId.M2State => $"{raw} ({FormatDoorState(raw)})",
            LiveDataId.M1Pos or LiveDataId.M2Pos or LiveDataId.M1Setpoint or LiveDataId.M2Setpoint or LiveDataId.M1Error or LiveDataId.M2Error
                => $"{raw / 100.0:F2} deg",
            LiveDataId.M1Pwm or LiveDataId.M2Pwm => $"{raw} %",
            LiveDataId.OperationTimeMs => $"{raw} ms",
            LiveDataId.DipValue => $"0x{raw:X}",
            _ => raw.ToString(),
        };
    }

    public static string FormatSysState(uint value)
    {
        return value switch
        {
            0 => "INIT",
            1 => "WAIT",
            2 => "OPENING",
            3 => "OPEN_DONE",
            4 => "CLOSING",
            5 => "CLOSE_DONE",
            6 => "BLOCKED",
            7 => "ERROR",
            _ => "UNKNOWN",
        };
    }

    public static string FormatDoorState(uint value)
    {
        return value switch
        {
            0 => "IDLE",
            1 => "OPENING",
            2 => "OPEN",
            3 => "CLOSING",
            4 => "CLOSED",
            5 => "BLOCKED",
            6 => "ERROR",
            _ => "UNKNOWN",
        };
    }
}
