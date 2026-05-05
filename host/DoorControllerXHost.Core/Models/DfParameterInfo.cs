namespace DoorControllerXHost.Core.Models;

public sealed class DfParameterInfo
{
    public required DfParamId Id { get; init; }

    public required string Name { get; init; }

    public required uint Min { get; init; }

    public required uint Max { get; init; }

    public required uint Default { get; init; }

    public string PickerDisplayName => $"{(byte)Id:D2} {GetChineseName(Id)} [{Name}]";

    public override string ToString()
    {
        return $"{(byte)Id:D2} {Name}";
    }

    private static string GetChineseName(DfParamId id)
    {
        return id switch
        {
            DfParamId.BlockRetryDelaySec => "卡門重試間隔(秒)",
            DfParamId.OpenTriggerAngle => "開門觸發角度",
            DfParamId.OpenDiffAngle => "開門差異角度",
            DfParamId.LockActiveTime => "電鎖動作時間(x0.1s)",
            DfParamId.BlockDetectAngle => "卡門偵測角度",
            DfParamId.BlockDetectTime => "卡門偵測時間(ms)",
            DfParamId.TimeWindow => "感測器時間窗口(x0.1s)",
            DfParamId.M1StartDuty => "主門起始PWM(%)",
            DfParamId.M1MaxDuty => "主門最大PWM(%)",
            DfParamId.M2StartDuty => "副門起始PWM(%)",
            DfParamId.M2MaxDuty => "副門最大PWM(%)",
            DfParamId.M3StartDuty => "電鎖起始PWM(%)",
            DfParamId.M3MaxDuty => "電鎖最大PWM(%)",
            DfParamId.M1OpenAngle => "主門開門角度",
            DfParamId.M2OpenAngle => "副門開門角度",
            DfParamId.M1OpenRevDuty => "主門開門反轉PWM(%)",
            DfParamId.M1OpenRevDutyDelta => "主門開門反轉PWM增量",
            DfParamId.M1CloseRevDuty => "主門關門反轉PWM(%)",
            DfParamId.M1CloseRevDutyDelta => "主門關門反轉PWM增量",
            DfParamId.M1ZeroMin => "主門原點ADC最小值",
            DfParamId.M1ZeroMax => "主門原點ADC最大值",
            DfParamId.M2ZeroMin => "副門原點ADC最小值",
            DfParamId.M2ZeroMax => "副門原點ADC最大值",
            DfParamId.MaxOpenOperationTime => "最大開門動作時間(秒)",
            DfParamId.M1CloseHoldTime => "主門關門保持時間(秒)",
            DfParamId.M1ZeroError => "主門原點允許誤差",
            DfParamId.HomeZeroSampleTime => "原點採樣時間(x0.1s)",
            DfParamId.M2ZeroError => "副門原點允許誤差",
            DfParamId.AutoTestCycles => "自動測試次數",
            DfParamId.AutoTestOpenHoldSec => "開門等待時間(秒)",
            DfParamId.M1StartupReliefMs => "主門啟動緩衝時間(ms)",
            DfParamId.M1PidKpX1000 => "主門 PID Kp(x1000)",
            DfParamId.M1PidKiX1000 => "主門 PID Ki(x1000)",
            DfParamId.M1PidKdX1000 => "主門 PID Kd(x1000)",
            DfParamId.M2PidKpX1000 => "副門 PID Kp(x1000)",
            DfParamId.M2PidKiX1000 => "副門 PID Ki(x1000)",
            DfParamId.M2PidKdX1000 => "副門 PID Kd(x1000)",
            DfParamId.TuneTargetMotor => "調參測試目標馬達",
            DfParamId.TuneSetpointDeg => "調參測試目標角度",
            DfParamId.TunePwmDuty => "調參測試固定PWM(%)",
            DfParamId.TuneTimeoutSec => "調參測試逾時(秒)",
            DfParamId.M1DecelZoneDeg => "主門減速區間角度",
            DfParamId.M1DecelMaxDuty => "主門減速區最大PWM(%)",
            DfParamId.M2DecelZoneDeg => "副門減速區間角度",
            DfParamId.M2DecelMaxDuty => "副門減速區最大PWM(%)",
            DfParamId.BlockNoCheckAngle => "卡門檢測起始角度",
            DfParamId.M1BlockFreeAngle => "主門免檢角度(超過不檢測)",
            DfParamId.M2BlockFreeAngle => "副門免檢角度(超過不檢測)",
            _ => id.ToString()
        };
    }
}
