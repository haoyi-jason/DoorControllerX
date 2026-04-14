namespace DoorControllerXHost.Core.Models;

public enum DfParamId : byte
{
    BlockRetryDelaySec = 0,
    OpenTriggerAngle = 1,
    OpenDiffAngle = 2,
    LockActiveTime = 3,
    BlockDetectAngle = 4,
    BlockDetectTime = 5,
    TimeWindow = 6,
    M1StartDuty = 7,
    M1MaxDuty = 8,
    M2StartDuty = 9,
    M2MaxDuty = 10,
    M3StartDuty = 11,
    M3MaxDuty = 12,
    M1OpenAngle = 13,
    M2OpenAngle = 14,
    M1OpenRevDuty = 15,
    M1OpenRevDutyDelta = 16,
    M1CloseFwdDuty = 17,
    M1CloseFwdDutyDelta = 18,
    M1ZeroError = 19,
    M2ZeroError = 20,
    MaxOpenOperationTime = 21,
}