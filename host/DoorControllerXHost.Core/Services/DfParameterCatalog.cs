using DoorControllerXHost.Core.Models;

namespace DoorControllerXHost.Core.Services;

public static class DfParameterCatalog
{
    public static readonly IReadOnlyList<DfParameterInfo> All =
    [
        new() { Id = DfParamId.BlockRetryDelaySec, Name = "DF_BLOCK_RETRY_DELAY_SEC", Min = 1, Max = 10, Default = 1 },
        new() { Id = DfParamId.OpenTriggerAngle, Name = "DF_OPEN_TRIGGER_ANGLE", Min = 5, Max = 30, Default = 10 },
        new() { Id = DfParamId.OpenDiffAngle, Name = "DF_OPEN_DIFF_ANGLE", Min = 10, Max = 30, Default = 10 },
        new() { Id = DfParamId.LockActiveTime, Name = "DF_LOCK_ACTIVE_TIME", Min = 10, Max = 50, Default = 20 },
        new() { Id = DfParamId.BlockDetectAngle, Name = "DF_BLOCK_DETECT_ANGLE", Min = 1, Max = 20, Default = 2 },
        new() { Id = DfParamId.BlockDetectTime, Name = "DF_BLOCK_DETECT_TIME", Min = 100, Max = 2000, Default = 500 },
        new() { Id = DfParamId.TimeWindow, Name = "DF_TIME_WINDOW", Min = 1, Max = 20, Default = 5 },
        new() { Id = DfParamId.M1StartDuty, Name = "DF_M1_START_DUTY", Min = 1, Max = 90, Default = 20 },
        new() { Id = DfParamId.M1MaxDuty, Name = "DF_M1_MAX_DUTY", Min = 1, Max = 90, Default = 80 },
        new() { Id = DfParamId.M2StartDuty, Name = "DF_M2_START_DUTY", Min = 1, Max = 90, Default = 20 },
        new() { Id = DfParamId.M2MaxDuty, Name = "DF_M2_MAX_DUTY", Min = 1, Max = 90, Default = 80 },
        new() { Id = DfParamId.M3StartDuty, Name = "DF_M3_START_DUTY", Min = 1, Max = 90, Default = 30 },
        new() { Id = DfParamId.M3MaxDuty, Name = "DF_M3_MAX_DUTY", Min = 1, Max = 50, Default = 50 },
        new() { Id = DfParamId.M1OpenAngle, Name = "DF_M1_OPEN_ANGLE", Min = 50, Max = 120, Default = 100 },
        new() { Id = DfParamId.M2OpenAngle, Name = "DF_M2_OPEN_ANGLE", Min = 50, Max = 120, Default = 100 },
        new() { Id = DfParamId.M1OpenRevDuty, Name = "DF_M1_OPEN_REV_DUTY", Min = 5, Max = 50, Default = 20 },
        new() { Id = DfParamId.M1OpenRevDutyDelta, Name = "DF_M1_OPEN_REV_DUTY_DELTA", Min = 1, Max = 20, Default = 5 },
        new() { Id = DfParamId.M1CloseRevDuty, Name = "DF_M1_CLOSE_REV_DUTY", Min = 5, Max = 50, Default = 20 },
        new() { Id = DfParamId.M1CloseRevDutyDelta, Name = "DF_M1_CLOSE_REV_DUTY_DELTA", Min = 1, Max = 20, Default = 5 },
        new() { Id = DfParamId.M1ZeroMin, Name = "DF_M1_ZERO_MIN", Min = 100, Max = 250, Default = 150 },
        new() { Id = DfParamId.M1ZeroMax, Name = "DF_M1_ZERO_MAX", Min = 100, Max = 350, Default = 210 },
        new() { Id = DfParamId.M2ZeroMin, Name = "DF_M2_ZERO_MIN", Min = 100, Max = 250, Default = 150 },
        new() { Id = DfParamId.M2ZeroMax, Name = "DF_M2_ZERO_MAX", Min = 100, Max = 350, Default = 210 },
        new() { Id = DfParamId.MaxOpenOperationTime, Name = "DF_MAX_OPEN_OPERATION_TIME", Min = 5, Max = 120, Default = 30 },
        new() { Id = DfParamId.M1CloseHoldTime, Name = "DF_M1_CLOSE_HOLD_TIME", Min = 1, Max = 10, Default = 2 },
        new() { Id = DfParamId.M1ZeroError, Name = "DF_M1_ZERO_ERROR", Min = 1, Max = 20, Default = 5 },
        new() { Id = DfParamId.HomeZeroSampleTime, Name = "DF_HOME_ZERO_SAMPLE_TIME", Min = 1, Max = 5, Default = 1 },
        new() { Id = DfParamId.M2ZeroError, Name = "DF_M2_ZERO_ERROR", Min = 1, Max = 20, Default = 5 },
        new() { Id = DfParamId.AutoTestCycles, Name = "DF_AUTO_TEST_CYCLES", Min = 0, Max = 200, Default = 0 },
        new() { Id = DfParamId.AutoTestOpenHoldSec, Name = "DF_AUTO_TEST_OPEN_HOLD_SEC", Min = 0, Max = 60, Default = 1 },
        new() { Id = DfParamId.M1StartupReliefMs, Name = "DF_M1_STARTUP_RELIEF_MS", Min = 100, Max = 2000, Default = 500 },
    ];
}
