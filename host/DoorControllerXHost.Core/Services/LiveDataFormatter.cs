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
            LiveDataId.CloseStage => $"{raw} ({FormatCloseStage(raw)})",
            LiveDataId.ResetReason => $"{raw} ({FormatResetReason(raw)})",
            LiveDataId.M1State or LiveDataId.M2State => $"{raw} ({FormatDoorState(raw)})",
            LiveDataId.ErrorCode => $"{raw} ({FormatErrorCode(raw)})",
            LiveDataId.M1Pos or LiveDataId.M2Pos or LiveDataId.M1Setpoint or LiveDataId.M2Setpoint or LiveDataId.M1Error or LiveDataId.M2Error
            or LiveDataId.M1RawAngle or LiveDataId.M2RawAngle
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

    public static string FormatErrorCode(uint value)
    {
        return value switch
        {
            0 => "NONE",
            1 => "BLOCK_RETRY_EXCEEDED",
            10 => "STARTUP_UNLOCK_CHECK_FAIL",
            11 => "STARTUP_M2_DIRECTION_FAIL",
            12 => "STARTUP_M2_HOME_TIMEOUT",
            13 => "STARTUP_M2_HOME_SWITCH_FAIL",
            14 => "STARTUP_M1_DIRECTION_FAIL",
            15 => "STARTUP_M1_HOME_TIMEOUT",
            16 => "STARTUP_M1_HOME_SWITCH_FAIL",
            17 => "STARTUP_LOCK_CHECK_FAIL",
            18 => "POT_FAULT_M1",
            19 => "POT_FAULT_M2",
            _ => "UNKNOWN",
        };
    }

    public static string FormatResetReason(uint value)
    {
        return value switch
        {
            0 => "NORMAL",
            1 => "IWDG (watchdog)",
            2 => "WWDG (window watchdog)",
            3 => "SW_RESET",
            4 => "PIN_RESET",
            5 => "POR (power-on)",
            99 => "HARD_FAULT",
            _ => "UNKNOWN",
        };
    }

    public static string FormatCloseStage(uint value)
    {
        return value switch
        {
            0 => "IDLE",
            1 => "M2_PRE_CLOSE",
            2 => "M1_MAIN_CLOSE",
            3 => "M2_FINAL_CLOSE",
            _ => "UNKNOWN",
        };
    }
}
