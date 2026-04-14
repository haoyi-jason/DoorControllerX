namespace DoorControllerXHost.Core.Models;

public enum LiveDataId : byte
{
    SysState = 0,
    M1State = 1,
    M2State = 2,
    M1Pos = 3,
    M2Pos = 4,
    M1Setpoint = 5,
    M2Setpoint = 6,
    M1Error = 7,
    M2Error = 8,
    M1Pwm = 9,
    M2Pwm = 10,
    M1Current = 11,
    M2Current = 12,
    DipValue = 13,
    BlockCount = 14,
    BlockRetryCount = 15,
    BlockSourceState = 16,
    ErrorCode = 17,
    OpenCount = 18,
    CloseCount = 19,
    LockRetryCount = 20,
    OperationTimeMs = 21,
}
