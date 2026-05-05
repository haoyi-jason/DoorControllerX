using DoorControllerXHost.Core.Abstractions;
using DoorControllerXHost.Core.Models;
using DoorControllerXHost.Core.Services;
using Microsoft.Maui.Graphics;
using System.Globalization;
using System.Text;

namespace DoorControllerXHost.Maui;

public partial class MainPage : ContentPage
{
    private readonly ITransportClient _transportClient;
    private readonly ParametersPage _parametersPage;
    private readonly IDispatcherTimer _pollTimer;
    private readonly IDispatcherTimer _chartTimer;
    private readonly RealTimeChartDrawable _chartDrawable = new();
    private readonly Dictionary<LiveDataId, uint> _latestLiveValues = new();
    private readonly List<ChartSample> _chartSamples = [];
    private readonly List<ChartMarker> _chartMarkers = [];
    private readonly HashSet<LiveDataId> _unsupportedLiveIds = [];
    private readonly CurveSlot[] _curveSlots;
    private bool _pollBusy;
    private bool _chartBusy;
    private bool _endpointRefreshBusy;
    private bool _remoteCommandBusy;
    private int _chartWindowSeconds = 120;

    private static readonly Dictionary<LiveDataId, string> LiveDataDisplayNames = new()
    {
        { LiveDataId.M1RawAngle, "LD_M1_RAW_ANGLE" },
        { LiveDataId.M2RawAngle, "LD_M2_RAW_ANGLE" },
    };

    private bool _autoY1 = true;
    private bool _autoY2 = true;
    private double _manualY1Min = 0.0;
    private double _manualY1Max = 100.0;
    private double _manualY2Min = 0.0;
    private double _manualY2Max = 100.0;
    private int _selectedTuneCurveIndex = 0;
    private DateTime? _pendingTuneRunStartUtc;
    private DateTime? _lastTuneRunStartUtc;
    private DateTime? _lastTuneRunEndUtc;
    private bool _tuneRunActive;
    private uint? _lastSysStateValue;
    private uint? _lastM1StateValue;
    private uint? _lastM2StateValue;
    private uint? _lastCloseStageValue;

    private static readonly LiveDataId[] SnapshotIds =
    [
        LiveDataId.SysState,
        LiveDataId.M1State,
        LiveDataId.M2State,
        LiveDataId.M1Pos,
        LiveDataId.M2Pos,
        LiveDataId.BlockCount,
        LiveDataId.BlockRetryCount,
        LiveDataId.BlockSourceState,
        LiveDataId.M1RawAngle,
        LiveDataId.M2RawAngle,
        LiveDataId.ErrorCode,
        LiveDataId.LockRetryCount,
        LiveDataId.OperationTimeMs,
        LiveDataId.OpenCount,
        LiveDataId.CloseCount,
        LiveDataId.CloseStage,
        LiveDataId.ResetReason,
        LiveDataId.AutoTestTarget,
        LiveDataId.AutoTestDone,
        LiveDataId.AutoTestOpenHoldSec,
    ];

    private const byte RemoteCommandLiveId = 24;
    private const uint RemoteCommandOpen = 1;
    private const uint RemoteCommandClose = 2;
    private const uint RemoteCommandLock = 3;
    private const uint RemoteCommandUnlock = 4;
    private const uint RemoteCommandClearError = 5;
    private const uint RemoteCommandAutoTune = 6;
    private const uint AutoTestCyclesMin = 0;
    private const uint AutoTestCyclesMax = 200;
    private const uint AutoTestOpenHoldSecMin = 1;
    private const uint AutoTestIntervalDefaultSec = 2;
    private const uint AutoTestOpenHoldSecMax = 60;
    private const uint PidGainScale = 1000;
    private const double PidGainMin = 0.0;
    private const double PidGainMax = 65.535;
    private const uint TuneSetpointMin = 5;
    private const uint TuneSetpointMax = 180;
    private const uint TunePwmMin = 1;
    private const uint TunePwmMax = 90;
    private const uint TuneTimeoutMin = 1;
    private const uint TuneTimeoutMax = 30;

    public MainPage(ITransportClient transportClient, ParametersPage parametersPage)
    {
        InitializeComponent();
        _transportClient = transportClient;
        _parametersPage = parametersPage;

        _pollTimer = Dispatcher.CreateTimer();
        _pollTimer.Interval = TimeSpan.FromMilliseconds(250);
        _pollTimer.Tick += async (_, _) => await PollSnapshotAsync();

        _chartTimer = Dispatcher.CreateTimer();
        _chartTimer.Interval = TimeSpan.FromSeconds(1);
        _chartTimer.Tick += async (_, _) => await SampleChartAsync();

        RealtimeChartView.Drawable = _chartDrawable;

        _curveSlots =
        [
            new CurveSlot("C1", Curve1EnabledCheckBox, Curve1SourcePicker, Curve1AxisPicker, Color.FromArgb("#D14334")),
            new CurveSlot("C2", Curve2EnabledCheckBox, Curve2SourcePicker, Curve2AxisPicker, Color.FromArgb("#2C7BE5")),
            new CurveSlot("C3", Curve3EnabledCheckBox, Curve3SourcePicker, Curve3AxisPicker, Color.FromArgb("#0F9D58")),
            new CurveSlot("C4", Curve4EnabledCheckBox, Curve4SourcePicker, Curve4AxisPicker, Color.FromArgb("#F59E0B")),
        ];

        InitializeChartOptions();

#if WINDOWS
        PlatformHintLabel.Text = "Windows: endpoint uses Bluetooth virtual COM port, e.g. COM5.";
#elif ANDROID
        PlatformHintLabel.Text = "Android: endpoint uses paired Bluetooth Classic device name or MAC address.";
#else
        PlatformHintLabel.Text = "Select transport endpoint for this platform.";
#endif

        _ = RefreshEndpointsAsync();
    }

    protected override async void OnAppearing()
    {
        base.OnAppearing();
        if (EndpointPicker.ItemsSource == null)
        {
            await RefreshEndpointsAsync();
        }
    }

    private async void OnRefreshEndpointsClicked(object? sender, EventArgs e)
    {
        await RefreshEndpointsAsync();
    }

    private void OnEndpointSelectionChanged(object? sender, EventArgs e)
    {
        if (EndpointPicker.SelectedItem is ConnectionEndpoint endpoint)
        {
            EndpointEntry.Text = endpoint.Id;
        }
    }

    private async void OnConnectClicked(object? sender, EventArgs e)
    {
        if (!int.TryParse(BaudEntry.Text, out var baudRate))
        {
            await DisplayAlert("Invalid Baud", "Baud rate must be a number.", "OK");
            return;
        }

        if (string.IsNullOrWhiteSpace(EndpointEntry.Text))
        {
            await DisplayAlert("Missing Endpoint", "Please enter COM port, Bluetooth device name, or MAC address.", "OK");
            return;
        }

        try
        {
            await _transportClient.ConnectAsync(EndpointEntry.Text.Trim(), baudRate);
            ConnectButton.IsEnabled = false;
            DisconnectButton.IsEnabled = true;
            ReadSnapshotButton.IsEnabled = true;
            PollButton.IsEnabled = true;
            RemoteOpenButton.IsEnabled = true;
            RemoteCloseButton.IsEnabled = true;
            RemoteLockButton.IsEnabled = true;
            RemoteUnlockButton.IsEnabled = true;
            RemoteClearErrorButton.IsEnabled = true;
            AutoTestReadButton.IsEnabled = true;
            AutoTestStartButton.IsEnabled = true;
            AutoTestStopButton.IsEnabled = true;
            TuneRunButton.IsEnabled = true;

            Log($"Connected: {EndpointEntry.Text.Trim()} @ {baudRate}.");
            await RefreshAutoTestCyclesAsync();
            await PollSnapshotAsync();
        }
        catch (Exception ex)
        {
            Log($"Connect failed: {ex.Message}");
            await DisplayAlert("Connect Error", ex.Message, "OK");
        }
    }

    private async void OnDisconnectClicked(object? sender, EventArgs e)
    {
        _pollTimer.Stop();
        _chartTimer.Stop();
        await _transportClient.DisconnectAsync();

        ConnectButton.IsEnabled = true;
        DisconnectButton.IsEnabled = false;
        ReadSnapshotButton.IsEnabled = false;
        PollButton.IsEnabled = false;
        RemoteOpenButton.IsEnabled = false;
        RemoteCloseButton.IsEnabled = false;
        RemoteLockButton.IsEnabled = false;
        RemoteUnlockButton.IsEnabled = false;
        RemoteClearErrorButton.IsEnabled = false;
        AutoTestReadButton.IsEnabled = false;
        AutoTestStartButton.IsEnabled = false;
        AutoTestStopButton.IsEnabled = false;
        TuneRunButton.IsEnabled = false;
        AutoTestCurrentValueLabel.Text = "-";
        AutoTestOpenHoldCurrentValueLabel.Text = "-";
        AutoTestProgressValueLabel.Text = "-";
        TuneRunStatusLabel.Text = "-";
        PollButton.Text = "Start Poll";
        Log("Disconnected.");
    }

    private async void OnReadSnapshotClicked(object? sender, EventArgs e)
    {
        await PollSnapshotAsync();
        await SampleChartAsync();
    }

    private void OnClearChartClicked(object? sender, EventArgs e)
    {
        _chartSamples.Clear();
        _chartMarkers.Clear();
        _lastSysStateValue = null;
        _lastM1StateValue = null;
        _lastM2StateValue = null;
        _lastCloseStageValue = null;
        _pendingTuneRunStartUtc = null;
        _lastTuneRunStartUtc = null;
        _lastTuneRunEndUtc = null;
        _tuneRunActive = false;
        TuneResultLabel.Text = "-";
        TuneRunStatusLabel.Text = "-";
        UpdateChartDrawable();
        Log("Chart data cleared.");
    }

    private async void OnExportChartCsvClicked(object? sender, EventArgs e)
    {
        if (_chartSamples.Count == 0)
        {
            await DisplayAlert("No Data", "目前沒有可匯出的圖表資料。", "OK");
            return;
        }

        try
        {
            var exportDir = Path.Combine(FileSystem.Current.AppDataDirectory, "exports");
            Directory.CreateDirectory(exportDir);

            var fileName = $"door_chart_{DateTime.Now:yyyyMMdd_HHmmss}.csv";
            var filePath = Path.Combine(exportDir, fileName);

            var sb = new StringBuilder(64 * 1024);
            sb.Append("timestamp_utc,elapsed_sec");
            for (var i = 0; i < _curveSlots.Length; i++)
            {
                var sourceName = GetCurveSourceName(i);
                sb.Append(',');
                sb.Append(EscapeCsv($"C{i + 1}_{sourceName}"));
            }
            sb.AppendLine();

            var firstTs = _chartSamples[0].TimestampUtc;
            foreach (var sample in _chartSamples)
            {
                sb.Append(sample.TimestampUtc.ToString("O", CultureInfo.InvariantCulture));
                sb.Append(',');
                sb.Append((sample.TimestampUtc - firstTs).TotalSeconds.ToString("F3", CultureInfo.InvariantCulture));

                for (var i = 0; i < _curveSlots.Length; i++)
                {
                    sb.Append(',');
                    if (sample.Values.Length > i && sample.Values[i].HasValue)
                    {
                        sb.Append(sample.Values[i]!.Value.ToString("G17", CultureInfo.InvariantCulture));
                    }
                }

                sb.AppendLine();
            }

            await File.WriteAllTextAsync(filePath, sb.ToString(), Encoding.UTF8);
            Log($"Chart CSV exported: {filePath}");
            await DisplayAlert("Export Completed", $"CSV 已匯出:\n{filePath}", "OK");
        }
        catch (Exception ex)
        {
            Log($"Export CSV failed: {ex.Message}");
            await DisplayAlert("Export Error", ex.Message, "OK");
        }
    }

    private string GetCurveSourceName(int index)
    {
        if (index < 0 || index >= _curveSlots.Length)
        {
            return "NA";
        }

        var slot = _curveSlots[index];
        if (slot.SourcePicker.SelectedItem is ChartSourceOption option && option.Kind != ChartSourceKind.None)
        {
            return option.DisplayName;
        }

        return "None";
    }

    private static string EscapeCsv(string text)
    {
        if (text.IndexOfAny([',', '"', '\r', '\n']) < 0)
        {
            return text;
        }

        return $"\"{text.Replace("\"", "\"\"")}\"";
    }

    private void OnPollClicked(object? sender, EventArgs e)
    {
        if (_pollTimer.IsRunning)
        {
            _pollTimer.Stop();
            _chartTimer.Stop();
            PollButton.Text = "Start Poll";
            Log("Polling stopped.");
        }
        else
        {
            _pollTimer.Start();
            if (!_chartTimer.IsRunning)
            {
                _chartTimer.Start();
            }
            PollButton.Text = "Stop Poll";
            Log("Polling started.");
        }
    }

    private async void OnParametersClicked(object? sender, EventArgs e)
    {
        await Navigation.PushAsync(_parametersPage);
    }

    private async void OnRemoteOpenClicked(object? sender, EventArgs e)
    {
        if (!await ClearAutoTestForManualCommandAsync("開門"))
        {
            return;
        }

        await SendRemoteCommandAsync(RemoteCommandOpen, "開門");
    }

    private async void OnRemoteCloseClicked(object? sender, EventArgs e)
    {
        if (!await ClearAutoTestForManualCommandAsync("關門"))
        {
            return;
        }

        await SendRemoteCommandAsync(RemoteCommandClose, "關門");
    }

    private async void OnRemoteLockClicked(object? sender, EventArgs e)
    {
        await SendRemoteCommandAsync(RemoteCommandLock, "上鎖");
    }

    private async void OnRemoteUnlockClicked(object? sender, EventArgs e)
    {
        await SendRemoteCommandAsync(RemoteCommandUnlock, "解鎖");
    }

    private async void OnRemoteClearErrorClicked(object? sender, EventArgs e)
    {
        await SendRemoteCommandAsync(RemoteCommandClearError, "清除錯誤");
    }

    private async void OnTuneRunClicked(object? sender, EventArgs e)
    {
        if (!_transportClient.IsOpen)
        {
            await DisplayAlert("Not Connected", "Please connect to target first.", "OK");
            return;
        }

        if (TuneRunTargetPicker.SelectedItem is not PidTargetOption target)
        {
            await DisplayAlert("Missing Target", "請先選擇測試目標 (M1/M2)。", "OK");
            return;
        }

        if (!uint.TryParse(TuneRunSetpointEntry.Text, out var setpoint))
        {
            await DisplayAlert("Invalid Value", "Setpoint 需為整數。", "OK");
            return;
        }

        if (setpoint < TuneSetpointMin || setpoint > TuneSetpointMax)
        {
            await DisplayAlert("Out Of Range", $"Setpoint 需為 {TuneSetpointMin}~{TuneSetpointMax}。", "OK");
            return;
        }

        if (!uint.TryParse(TuneRunPwmEntry.Text, out var pwm))
        {
            await DisplayAlert("Invalid Value", "固定 PWM 需為整數。", "OK");
            return;
        }

        if (pwm < TunePwmMin || pwm > TunePwmMax)
        {
            await DisplayAlert("Out Of Range", $"固定 PWM 需為 {TunePwmMin}~{TunePwmMax}。", "OK");
            return;
        }

        if (!uint.TryParse(TuneRunTimeoutEntry.Text, out var timeoutSec))
        {
            await DisplayAlert("Invalid Value", "逾時秒數需為整數。", "OK");
            return;
        }

        if (timeoutSec < TuneTimeoutMin || timeoutSec > TuneTimeoutMax)
        {
            await DisplayAlert("Out Of Range", $"逾時秒數需為 {TuneTimeoutMin}~{TuneTimeoutMax}。", "OK");
            return;
        }

        var targetMotor = target.Target == PidTarget.M1 ? 1u : 2u;

        try
        {
            _pendingTuneRunStartUtc = DateTime.UtcNow;
            _lastTuneRunStartUtc = null;
            _lastTuneRunEndUtc = null;
            _tuneRunActive = false;

            await _transportClient.WriteParamAsync((byte)DfParamId.TuneTargetMotor, targetMotor);
            await _transportClient.WriteParamAsync((byte)DfParamId.TuneSetpointDeg, setpoint);
            await _transportClient.WriteParamAsync((byte)DfParamId.TunePwmDuty, pwm);
            await _transportClient.WriteParamAsync((byte)DfParamId.TuneTimeoutSec, timeoutSec);

            await SendRemoteCommandAsync(RemoteCommandAutoTune, "固定PWM測試");

            var status = $"執行中: {target.Label}, SP={setpoint} deg, PWM={pwm}%, timeout={timeoutSec}s";
            TuneRunStatusLabel.Text = status;
            Log($"Tune run start: {status}");
        }
        catch (Exception ex)
        {
            var message = $"Tune run failed: {ex.Message}";
            TuneRunStatusLabel.Text = message;
            Log(message);
            await DisplayAlert("Tune Run Error", ex.Message, "OK");
        }
    }

    private async void OnAutoTestReadClicked(object? sender, EventArgs e)
    {
        await RefreshAutoTestCyclesAsync();
    }

    private async void OnAutoTestStartClicked(object? sender, EventArgs e)
    {
        if (!_transportClient.IsOpen)
        {
            await DisplayAlert("Not Connected", "Please connect to target first.", "OK");
            return;
        }

        if (!uint.TryParse(AutoTestCyclesEntry.Text, out var cycles))
        {
            await DisplayAlert("Invalid Value", "測試次數需為整數。", "OK");
            return;
        }

        if (cycles == 0u || cycles > AutoTestCyclesMax)
        {
            await DisplayAlert("Out Of Range", $"測試次數需為 1~{AutoTestCyclesMax}。", "OK");
            return;
        }

        if (!uint.TryParse(AutoTestOpenHoldSecEntry.Text, out var openHoldSec))
        {
            await DisplayAlert("Invalid Value", "測試間隔秒數需為整數。", "OK");
            return;
        }

        if (openHoldSec < AutoTestOpenHoldSecMin || openHoldSec > AutoTestOpenHoldSecMax)
        {
            await DisplayAlert("Out Of Range", $"測試間隔秒數需為 {AutoTestOpenHoldSecMin}~{AutoTestOpenHoldSecMax}。", "OK");
            return;
        }

        try
        {
            await _transportClient.WriteLiveAsync((byte)LiveDataId.AutoTestOpenHoldSec, openHoldSec);
            await _transportClient.WriteLiveAsync((byte)LiveDataId.AutoTestTarget, cycles);
            Log($"Auto test set: cycles={cycles}, open-hold={openHoldSec}s");
            await SendRemoteCommandAsync(RemoteCommandOpen, "自動測試啟動");
            await RefreshAutoTestCyclesAsync();
            await PollSnapshotAsync();
        }
        catch (Exception ex)
        {
            Log($"Set auto test cycles failed: {ex.Message}");
            await DisplayAlert("Write Error", ex.Message, "OK");
        }
    }

    private async void OnAutoTestStopClicked(object? sender, EventArgs e)
    {
        if (!_transportClient.IsOpen)
        {
            await DisplayAlert("Not Connected", "Please connect to target first.", "OK");
            return;
        }

        try
        {
            await _transportClient.WriteLiveAsync((byte)LiveDataId.AutoTestTarget, AutoTestCyclesMin);
            Log("Auto test stopped (target cycles set to 0)");
            await RefreshAutoTestCyclesAsync();
            await PollSnapshotAsync();
        }
        catch (Exception ex)
        {
            Log($"Stop auto test failed: {ex.Message}");
            await DisplayAlert("Write Error", ex.Message, "OK");
        }
    }

    private async Task SendRemoteCommandAsync(uint commandValue, string commandLabel)
    {
        if (!_transportClient.IsOpen)
        {
            await DisplayAlert("Not Connected", "Please connect to target first.", "OK");
            return;
        }

        if (_remoteCommandBusy)
        {
            Log($"Remote command ignored while previous request is still in flight: {commandLabel} ({commandValue})");
            return;
        }

        _remoteCommandBusy = true;

        try
        {
            await _transportClient.WriteLiveAsync(RemoteCommandLiveId, commandValue);
            Log($"Remote command sent: {commandLabel} ({commandValue})");
            await PollSnapshotAsync();
        }
        catch (Exception ex)
        {
            Log($"Remote command failed ({commandLabel}): {ex.Message}");
            await DisplayAlert("Remote Command Error", ex.Message, "OK");
        }
        finally
        {
            _remoteCommandBusy = false;
        }
    }

    private async Task<bool> ClearAutoTestForManualCommandAsync(string commandLabel)
    {
        if (!_transportClient.IsOpen)
        {
            await DisplayAlert("Not Connected", "Please connect to target first.", "OK");
            return false;
        }

        try
        {
            await _transportClient.WriteLiveAsync((byte)LiveDataId.AutoTestTarget, AutoTestCyclesMin);
            Log($"Auto test cleared before manual {commandLabel}.");
            await RefreshAutoTestCyclesAsync();
            return true;
        }
        catch (Exception ex)
        {
            Log($"Clear auto test before manual {commandLabel} failed: {ex.Message}");
            await DisplayAlert("Write Error", ex.Message, "OK");
            return false;
        }
    }

    private async Task RefreshAutoTestCyclesAsync()
    {
        if (!_transportClient.IsOpen)
        {
            AutoTestCurrentValueLabel.Text = "-";
            return;
        }

        try
        {
            var cycles = await _transportClient.ReadLiveAsync((byte)LiveDataId.AutoTestTarget);
            var openHoldSec = await _transportClient.ReadLiveAsync((byte)LiveDataId.AutoTestOpenHoldSec);

            if (openHoldSec == 0u)
            {
                openHoldSec = AutoTestIntervalDefaultSec;
                await _transportClient.WriteLiveAsync((byte)LiveDataId.AutoTestOpenHoldSec, openHoldSec);
            }

            AutoTestCurrentValueLabel.Text = cycles.ToString(CultureInfo.InvariantCulture);
            AutoTestOpenHoldCurrentValueLabel.Text = $"目前: {openHoldSec}s";
            AutoTestCyclesEntry.Text = cycles.ToString(CultureInfo.InvariantCulture);
            AutoTestOpenHoldSecEntry.Text = openHoldSec.ToString(CultureInfo.InvariantCulture);
            UpdateAutoTestProgressLabel();
        }
        catch (Exception ex)
        {
            Log($"Read auto test cycles failed: {ex.Message}");
        }
    }

    private void OnChartWindowSecondsChanged(object? sender, TextChangedEventArgs e)
    {
        if (int.TryParse(ChartWindowSecondsEntry.Text, out var sec))
        {
            if (sec < 10) sec = 10;
            if (sec > 3600) sec = 3600;
            _chartWindowSeconds = sec;
            TrimChartSamples();
            UpdateChartDrawable();
        }
    }

    private void OnAutoY1CheckedChanged(object? sender, CheckedChangedEventArgs e)
    {
        _autoY1 = e.Value;
        Y1MinEntry.IsEnabled = !_autoY1;
        Y1MaxEntry.IsEnabled = !_autoY1;
        UpdateChartDrawable();
    }

    private void OnAutoY2CheckedChanged(object? sender, CheckedChangedEventArgs e)
    {
        _autoY2 = e.Value;
        Y2MinEntry.IsEnabled = !_autoY2;
        Y2MaxEntry.IsEnabled = !_autoY2;
        UpdateChartDrawable();
    }

    private void OnChartConfigChanged(object? sender, EventArgs e)
    {
        UpdateChartDrawable();
    }

    private void OnChartConfigCheckedChanged(object? sender, CheckedChangedEventArgs e)
    {
        UpdateChartDrawable();
    }

    private async void OnApplyChartAxesClicked(object? sender, EventArgs e)
    {
        if (!double.TryParse(Y1MinEntry.Text, NumberStyles.Float, CultureInfo.InvariantCulture, out var y1Min) ||
            !double.TryParse(Y1MaxEntry.Text, NumberStyles.Float, CultureInfo.InvariantCulture, out var y1Max) ||
            y1Max <= y1Min)
        {
            await DisplayAlert("Invalid Y1", "Y1 Min/Max 無效 (需為數字且 Max > Min)", "OK");
            return;
        }

        if (!double.TryParse(Y2MinEntry.Text, NumberStyles.Float, CultureInfo.InvariantCulture, out var y2Min) ||
            !double.TryParse(Y2MaxEntry.Text, NumberStyles.Float, CultureInfo.InvariantCulture, out var y2Max) ||
            y2Max <= y2Min)
        {
            await DisplayAlert("Invalid Y2", "Y2 Min/Max 無效 (需為數字且 Max > Min)", "OK");
            return;
        }

        _manualY1Min = y1Min;
        _manualY1Max = y1Max;
        _manualY2Min = y2Min;
        _manualY2Max = y2Max;

        UpdateChartDrawable();
    }

    private async Task PollSnapshotAsync()
    {
        if (_pollBusy || !_transportClient.IsOpen)
        {
            return;
        }

        _pollBusy = true;
        try
        {
            foreach (var id in SnapshotIds)
            {
                if (_unsupportedLiveIds.Contains(id))
                {
                    continue;
                }

                try
                {
                    var value = await _transportClient.ReadLiveAsync((byte)id);
                    ApplyValue(id, value);
                }
                catch (Exception ex) when (TryMarkUnsupportedLiveId(id, ex))
                {
                    continue;
                }
                catch (Exception ex)
                {
                    Log($"Read live {id} failed: {ex.Message}");
                }
            }
        }
        catch (Exception ex)
        {
            Log($"Polling loop warning: {ex.Message}");
        }
        finally
        {
            _pollBusy = false;
        }
    }

    private void ApplyValue(LiveDataId id, uint value)
    {
        _latestLiveValues[id] = value;
        var formatted = LiveDataFormatter.FormatValue(id, value);

        switch (id)
        {
            case LiveDataId.SysState:
                RecordStateMarker(id, value, ref _lastSysStateValue);
                SysStateValueLabel.Text = formatted;
                UpdateTuneRunCapture(value);
                break;
            case LiveDataId.M1State:
                RecordStateMarker(id, value, ref _lastM1StateValue);
                M1StateValueLabel.Text = formatted;
                break;
            case LiveDataId.M2State:
                RecordStateMarker(id, value, ref _lastM2StateValue);
                M2StateValueLabel.Text = formatted;
                break;
            case LiveDataId.M1Pos:
                M1PosValueLabel.Text = formatted;
                break;
            case LiveDataId.M2Pos:
                M2PosValueLabel.Text = formatted;
                break;
            case LiveDataId.BlockCount:
                BlockCountValueLabel.Text = formatted;
                break;
            case LiveDataId.BlockRetryCount:
                BlockRetryCountValueLabel.Text = formatted;
                break;
            case LiveDataId.BlockSourceState:
                BlockSourceStateValueLabel.Text = formatted;
                break;
            case LiveDataId.M1RawAngle:
                M1RawAngleValueLabel.Text = formatted;
                break;
            case LiveDataId.M2RawAngle:
                M2RawAngleValueLabel.Text = formatted;
                break;
            case LiveDataId.ErrorCode:
                ErrorCodeValueLabel.Text = formatted;
                break;
            case LiveDataId.LockRetryCount:
                LockRetryCountValueLabel.Text = formatted;
                break;
            case LiveDataId.OperationTimeMs:
                OperationTimeValueLabel.Text = formatted;
                break;
            case LiveDataId.OpenCount:
                OpenCountValueLabel.Text = formatted;
                break;
            case LiveDataId.CloseCount:
                CloseCountValueLabel.Text = formatted;
                break;
            case LiveDataId.CloseStage:
                RecordStateMarker(id, value, ref _lastCloseStageValue);
                CloseStageValueLabel.Text = formatted;
                break;
            case LiveDataId.ResetReason:
                ResetReasonValueLabel.Text = formatted;
                break;
            case LiveDataId.AutoTestTarget:
            case LiveDataId.AutoTestDone:
                UpdateAutoTestProgressLabel();
                break;
            case LiveDataId.AutoTestOpenHoldSec:
                AutoTestOpenHoldCurrentValueLabel.Text = $"目前: {value}s";
                break;
        }
    }

    private void RecordStateMarker(LiveDataId id, uint value, ref uint? lastValue)
    {
        if (lastValue == value)
        {
            return;
        }

        lastValue = value;

        var descriptor = CreateStateMarker(id, value);
        if (descriptor is null)
        {
            return;
        }

        var now = DateTime.UtcNow;
        if (_chartMarkers.Count > 0)
        {
            var last = _chartMarkers[^1];
            if (last.Label == descriptor.Value.Label && (now - last.TimestampUtc).TotalMilliseconds < 200.0)
            {
                return;
            }
        }

        _chartMarkers.Add(new ChartMarker(now, descriptor.Value.Label, descriptor.Value.Color));
    }

    private static (string Label, Color Color)? CreateStateMarker(LiveDataId id, uint value)
    {
        return id switch
        {
            LiveDataId.SysState => value switch
            {
                2 => ("系統開門", Color.FromArgb("#7C3AED")),
                4 => ("系統關門", Color.FromArgb("#DC2626")),
                6 => ("系統卡門", Color.FromArgb("#B45309")),
                7 => ("系統錯誤", Color.FromArgb("#991B1B")),
                8 => ("固定PWM測試", Color.FromArgb("#2563EB")),
                _ => null,
            },
            LiveDataId.M1State => value switch
            {
                1 => ("開主門", Color.FromArgb("#D14334")),
                3 => ("關主門", Color.FromArgb("#9A3412")),
                4 => ("主門關閉", Color.FromArgb("#4B5563")),
                _ => null,
            },
            LiveDataId.M2State => value switch
            {
                1 => ("開副門", Color.FromArgb("#2C7BE5")),
                3 => ("關副門", Color.FromArgb("#1D4ED8")),
                4 => ("副門關閉", Color.FromArgb("#6B7280")),
                _ => null,
            },
            LiveDataId.CloseStage => value switch
            {
                1 => ("副門預關", Color.FromArgb("#0F9D58")),
                2 => ("主門主關", Color.FromArgb("#F59E0B")),
                3 => ("副門終關", Color.FromArgb("#059669")),
                _ => null,
            },
            _ => null,
        };
    }

    private void UpdateAutoTestProgressLabel()
    {
        var done = _latestLiveValues.TryGetValue(LiveDataId.AutoTestDone, out var doneValue) ? doneValue : 0u;

        uint target;
        if (_latestLiveValues.TryGetValue(LiveDataId.AutoTestTarget, out var targetValue))
        {
            target = targetValue;
        }
        else if (!uint.TryParse(AutoTestCurrentValueLabel.Text, out target))
        {
            target = 0u;
        }

        AutoTestProgressValueLabel.Text = $"{done}/{target}";
    }

    private void InitializeChartOptions()
    {
        var sourceOptions = new List<ChartSourceOption>
        {
            new(ChartSourceKind.None, "None", null, null)
        };

        foreach (var id in Enum.GetValues<LiveDataId>())
        {
            var displayName = LiveDataDisplayNames.TryGetValue(id, out var mapped) ? mapped : $"LD {id}";
            sourceOptions.Add(new ChartSourceOption(ChartSourceKind.LiveData, displayName, id, null));
        }

        foreach (var df in DfParameterCatalog.All)
        {
            sourceOptions.Add(new ChartSourceOption(ChartSourceKind.DfParam, df.Name, null, (byte)df.Id));
        }

        foreach (var slot in _curveSlots)
        {
            slot.SourcePicker.ItemsSource = sourceOptions;
            slot.AxisPicker.ItemsSource = new List<ChartAxisOption>
            {
                new(ChartAxis.Y1, "Y1"),
                new(ChartAxis.Y2, "Y2"),
            };
            slot.AxisPicker.SelectedIndex = 0;
        }

        TuneCurvePicker.ItemsSource = _curveSlots.Select(s => s.Name).ToList();
        TuneCurvePicker.SelectedIndex = 0;
        TuneObjectivePicker.ItemsSource = new List<TuningObjectiveOption>
        {
            new(TuningObjective.Balanced, "平衡 (Balanced)"),
            new(TuningObjective.SpeedPriority, "速度優先 (Faster rise)"),
            new(TuningObjective.ConvergencePriority, "收斂優先 (Less overshoot / faster settle)"),
        };
        TuneObjectivePicker.SelectedIndex = 0;

        PidTargetPicker.ItemsSource = new List<PidTargetOption>
        {
            new(PidTarget.M1, "M1 Door PID"),
            new(PidTarget.M2, "M2 Door PID"),
        };
        PidTargetPicker.SelectedIndex = 0;

        TuneRunTargetPicker.ItemsSource = new List<PidTargetOption>
        {
            new(PidTarget.M1, "M1 Door"),
            new(PidTarget.M2, "M2 Door"),
        };
        TuneRunTargetPicker.SelectedIndex = 0;

        _curveSlots[0].SourcePicker.SelectedItem = sourceOptions.FirstOrDefault(x => x.LiveId == LiveDataId.M1Pos) ?? sourceOptions[0];
        _curveSlots[1].SourcePicker.SelectedItem = sourceOptions.FirstOrDefault(x => x.LiveId == LiveDataId.M2Pos) ?? sourceOptions[0];
        _curveSlots[2].SourcePicker.SelectedItem = sourceOptions.FirstOrDefault(x => x.LiveId == LiveDataId.M1Pwm) ?? sourceOptions[0];
        _curveSlots[3].SourcePicker.SelectedItem = sourceOptions.FirstOrDefault(x => x.LiveId == LiveDataId.M2Pwm) ?? sourceOptions[0];

        _curveSlots[2].AxisPicker.SelectedIndex = 1;
        _curveSlots[3].AxisPicker.SelectedIndex = 1;

        UpdateChartDrawable();
    }

    private async void OnEstimatePiClicked(object? sender, EventArgs e)
    {
        try
        {
            _selectedTuneCurveIndex = Math.Clamp(TuneCurvePicker.SelectedIndex, 0, _curveSlots.Length - 1);
            var slot = _curveSlots[_selectedTuneCurveIndex];

            if (!slot.EnabledCheckBox.IsChecked)
            {
                TuneResultLabel.Text = "選定曲線未啟用";
                return;
            }

            if (slot.SourcePicker.SelectedItem is not ChartSourceOption source || source.Kind == ChartSourceKind.None)
            {
                TuneResultLabel.Text = "請先選擇有效曲線來源";
                return;
            }

            if (!double.TryParse(TuneSetpointEntry.Text, NumberStyles.Float, CultureInfo.InvariantCulture, out var setpoint))
            {
                await DisplayAlert("Invalid Setpoint", "Setpoint must be numeric.", "OK");
                return;
            }

            var objective = (TuneObjectivePicker.SelectedItem as TuningObjectiveOption)?.Objective ?? TuningObjective.Balanced;

            var order = new List<TuningObjective>
            {
                objective,
                TuningObjective.Balanced,
                TuningObjective.SpeedPriority,
                TuningObjective.ConvergencePriority,
            };

            var results = order
                .Distinct()
                .Select(obj => EstimatePiFromCurve(_selectedTuneCurveIndex, setpoint, obj))
                .Where(r => r is not null)
                .Select(r => r!)
                .ToList();

            if (results.Count == 0)
            {
                TuneResultLabel.Text = "資料不足，請先執行固定PWM測試，並確認圖表內有最近一次測試曲線";
                return;
            }

            var lines = results.Select(r =>
                r.DirectionMismatch
                    ? $"[{r.ObjectiveLabel}] Kp={r.Kp:F3}, Ki={r.Ki:F3} (方向不一致, using |K|)"
                    : $"[{r.ObjectiveLabel}] Kp={r.Kp:F3}, Ki={r.Ki:F3}");
            TuneResultLabel.Text = string.Join(Environment.NewLine, lines);

            var chosen = results[0];
            PidKpEntry.Text = chosen.Kp.ToString("F3", CultureInfo.InvariantCulture);
            PidKiEntry.Text = chosen.Ki.ToString("F3", CultureInfo.InvariantCulture);
            if (string.IsNullOrWhiteSpace(PidKdEntry.Text))
            {
                PidKdEntry.Text = "0.000";
            }
            PidWriteStatusLabel.Text = "PID write: 已帶入試算值，請確認後按 Write PID";

            foreach (var r in results)
            {
                Log($"PI Estimate {slot.Name}/{source.DisplayName}/{r.ObjectiveLabel}: Kp={r.Kp:F3}, Ki={r.Ki:F3} (L={r.L:F2}s, Tau={r.Tau:F2}s, K={r.K:F3}, mismatch={r.DirectionMismatch})");
            }
        }
        catch (Exception ex)
        {
            TuneResultLabel.Text = "試算失敗";
            Log($"PI Estimate failed: {ex.Message}");
        }
    }

    private void OnApplyTuneToPidClicked(object? sender, EventArgs e)
    {
        try
        {
            _selectedTuneCurveIndex = Math.Clamp(TuneCurvePicker.SelectedIndex, 0, _curveSlots.Length - 1);
            var objective = (TuneObjectivePicker.SelectedItem as TuningObjectiveOption)?.Objective ?? TuningObjective.Balanced;

            if (!double.TryParse(TuneSetpointEntry.Text, NumberStyles.Float, CultureInfo.InvariantCulture, out var setpoint))
            {
                PidWriteStatusLabel.Text = "PID write: Setpoint 格式錯誤";
                return;
            }

            var estimate = EstimatePiFromCurve(_selectedTuneCurveIndex, setpoint, objective);
            if (estimate is null)
            {
                PidWriteStatusLabel.Text = "PID write: 最近一次固定PWM測試曲線資料不足，無法套用試算";
                return;
            }

            PidKpEntry.Text = estimate.Kp.ToString("F3", CultureInfo.InvariantCulture);
            PidKiEntry.Text = estimate.Ki.ToString("F3", CultureInfo.InvariantCulture);
            if (string.IsNullOrWhiteSpace(PidKdEntry.Text))
            {
                PidKdEntry.Text = "0.000";
            }

            PidWriteStatusLabel.Text = $"PID write: 套用 {estimate.ObjectiveLabel} 試算值";
        }
        catch (Exception ex)
        {
            PidWriteStatusLabel.Text = $"PID write: 套用失敗 - {ex.Message}";
        }
    }

    private async void OnWritePidClicked(object? sender, EventArgs e)
    {
        if (!_transportClient.IsOpen)
        {
            await DisplayAlert("Not Connected", "Please connect to target first.", "OK");
            return;
        }

        if (PidTargetPicker.SelectedItem is not PidTargetOption target)
        {
            await DisplayAlert("Missing Target", "請先選擇 PID target (M1/M2)。", "OK");
            return;
        }

        var kpOk = TryParseGain(PidKpEntry.Text, out var kp, out var kpRaw, out var kpError);
        var kiOk = TryParseGain(PidKiEntry.Text, out var ki, out var kiRaw, out var kiError);
        var kdOk = TryParseGain(PidKdEntry.Text, out var kd, out var kdRaw, out var kdError);

        if (!kpOk || !kiOk || !kdOk)
        {
            var err = kpError ?? kiError ?? kdError ?? "Invalid PID value.";
            await DisplayAlert("Invalid PID", err, "OK");
            return;
        }

        var (kpId, kiId, kdId) = target.Target switch
        {
            PidTarget.M1 => (DfParamId.M1PidKpX1000, DfParamId.M1PidKiX1000, DfParamId.M1PidKdX1000),
            PidTarget.M2 => (DfParamId.M2PidKpX1000, DfParamId.M2PidKiX1000, DfParamId.M2PidKdX1000),
            _ => throw new InvalidOperationException("Unsupported PID target."),
        };

        try
        {
            await _transportClient.WriteParamAsync((byte)kpId, kpRaw);
            await _transportClient.WriteParamAsync((byte)kiId, kiRaw);
            await _transportClient.WriteParamAsync((byte)kdId, kdRaw);

            var kpReadBack = await _transportClient.ReadParamAsync((byte)kpId);
            var kiReadBack = await _transportClient.ReadParamAsync((byte)kiId);
            var kdReadBack = await _transportClient.ReadParamAsync((byte)kdId);

            if (kpReadBack != kpRaw || kiReadBack != kiRaw || kdReadBack != kdRaw)
            {
                var mismatch = $"PID write verify failed: {target.Label} expected ({kpRaw},{kiRaw},{kdRaw}) readback ({kpReadBack},{kiReadBack},{kdReadBack})";
                PidWriteStatusLabel.Text = $"PID write: {mismatch}";
                Log(mismatch);
                await DisplayAlert("PID Verify Failed", mismatch, "OK");
                return;
            }

            var msg = $"PID write success: {target.Label} Kp={kp:F3}, Ki={ki:F3}, Kd={kd:F3} (readback OK)";
            PidWriteStatusLabel.Text = $"PID write: {msg}";
            Log(msg);
        }
        catch (Exception ex)
        {
            var msg = $"PID write failed for {target.Label}: {ex.Message}";
            PidWriteStatusLabel.Text = $"PID write: {msg}";
            Log(msg);
            await DisplayAlert("PID Write Error", ex.Message, "OK");
        }
    }

    private static bool TryParseGain(string? text, out double gain, out uint raw, out string? error)
    {
        gain = 0.0;
        raw = 0;
        error = null;

        if (!double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out gain))
        {
            error = "PID 參數需為數字。";
            return false;
        }

        if (gain < PidGainMin || gain > PidGainMax)
        {
            error = $"PID 參數範圍需為 {PidGainMin:F3} ~ {PidGainMax:F3}。";
            return false;
        }

        raw = (uint)Math.Round(gain * PidGainScale, MidpointRounding.AwayFromZero);
        return true;
    }

    private async Task SampleChartAsync()
    {
        if (_chartBusy || !_transportClient.IsOpen)
        {
            return;
        }

        _chartBusy = true;
        try
        {
            var values = new double?[_curveSlots.Length];
            var sourceCache = new Dictionary<string, double?>();

            for (var i = 0; i < _curveSlots.Length; i++)
            {
                var slot = _curveSlots[i];
                if (!slot.EnabledCheckBox.IsChecked)
                {
                    continue;
                }

                if (slot.SourcePicker.SelectedItem is not ChartSourceOption option || option.Kind == ChartSourceKind.None)
                {
                    continue;
                }

                if (!sourceCache.TryGetValue(option.Key, out var val))
                {
                    val = await ResolveChartValueAsync(option);
                    sourceCache[option.Key] = val;
                }

                values[i] = val;
            }

            _chartSamples.Add(new ChartSample(DateTime.UtcNow, values));
            TrimChartSamples();
            UpdateChartDrawable();
        }
        catch (Exception ex)
        {
            Log($"Chart sampling failed: {ex.Message}");
        }
        finally
        {
            _chartBusy = false;
        }
    }

    private async Task<double?> ResolveChartValueAsync(ChartSourceOption option)
    {
        if (option.Kind == ChartSourceKind.LiveData && option.LiveId.HasValue)
        {
            if (_unsupportedLiveIds.Contains(option.LiveId.Value))
            {
                return null;
            }

            try
            {
                var raw = await _transportClient.ReadLiveAsync((byte)option.LiveId.Value);
                _latestLiveValues[option.LiveId.Value] = raw;
                return ConvertLiveRawToNumeric(option.LiveId.Value, raw);
            }
            catch (Exception ex) when (TryMarkUnsupportedLiveId(option.LiveId.Value, ex))
            {
                return null;
            }
            catch
            {
                return null;
            }
        }

        if (option.Kind == ChartSourceKind.DfParam && option.ParamId.HasValue)
        {
            try
            {
                var raw = await _transportClient.ReadParamAsync(option.ParamId.Value);
                return raw;
            }
            catch
            {
                return null;
            }
        }

        return null;
    }

    private bool TryMarkUnsupportedLiveId(LiveDataId id, Exception ex)
    {
        var message = ex.Message;
        var isNak = message.Contains("NAK for live id", StringComparison.OrdinalIgnoreCase) &&
                    message.Contains(((byte)id).ToString(CultureInfo.InvariantCulture), StringComparison.Ordinal);

        if (!isNak)
        {
            return false;
        }

        if (_unsupportedLiveIds.Add(id))
        {
            Log($"Live ID not supported by target, skipping: {id} ({(byte)id})");
        }

        return true;
    }

    private static double ConvertLiveRawToNumeric(LiveDataId id, uint raw)
    {
        return id switch
        {
            LiveDataId.M1Pos or LiveDataId.M2Pos or LiveDataId.M1Setpoint or LiveDataId.M2Setpoint or LiveDataId.M1Error or LiveDataId.M2Error
            or LiveDataId.M1RawAngle or LiveDataId.M2RawAngle => raw / 100.0,
            _ => raw,
        };
    }

    private void TrimChartSamples()
    {
        if (_chartSamples.Count == 0)
        {
            _chartMarkers.Clear();
            return;
        }

        var cutoff = DateTime.UtcNow - TimeSpan.FromSeconds(_chartWindowSeconds);
        _chartSamples.RemoveAll(s => s.TimestampUtc < cutoff);
        _chartMarkers.RemoveAll(m => m.TimestampUtc < cutoff);
    }

    private void UpdateChartDrawable()
    {
        var now = DateTime.UtcNow;
        var start = now - TimeSpan.FromSeconds(_chartWindowSeconds);
        var visible = _chartSamples.Where(s => s.TimestampUtc >= start).ToList();
        var markers = _chartMarkers.Where(m => m.TimestampUtc >= start).ToList();
        float? tuneStartSeconds = null;
        float? tuneEndSeconds = null;

        var series = new List<CurveSeries>();
        var y1Vals = new List<double>();
        var y2Vals = new List<double>();

        if (_lastTuneRunStartUtc.HasValue && _lastTuneRunStartUtc.Value >= start)
        {
            tuneStartSeconds = (float)(_lastTuneRunStartUtc.Value - start).TotalSeconds;
        }

        if (_lastTuneRunEndUtc.HasValue && _lastTuneRunEndUtc.Value >= start)
        {
            tuneEndSeconds = (float)(_lastTuneRunEndUtc.Value - start).TotalSeconds;
        }
        else if (_tuneRunActive && _lastTuneRunStartUtc.HasValue)
        {
            tuneEndSeconds = (float)(now - start).TotalSeconds;
        }

        for (var i = 0; i < _curveSlots.Length; i++)
        {
            var slot = _curveSlots[i];
            if (!slot.EnabledCheckBox.IsChecked)
            {
                continue;
            }

            if (slot.SourcePicker.SelectedItem is not ChartSourceOption option || option.Kind == ChartSourceKind.None)
            {
                continue;
            }

            var axis = (slot.AxisPicker.SelectedItem as ChartAxisOption)?.Axis ?? ChartAxis.Y1;
            var points = visible
                .Where(s => s.Values.Length > i && s.Values[i].HasValue)
                .Select(s => new PointF((float)(s.TimestampUtc - start).TotalSeconds, (float)s.Values[i]!.Value))
                .ToList();

            if (points.Count == 0)
            {
                continue;
            }

            var vals = points.Select(p => (double)p.Y);
            if (axis == ChartAxis.Y1)
            {
                y1Vals.AddRange(vals);
            }
            else
            {
                y2Vals.AddRange(vals);
            }

            var latestValue = points.Count > 0 ? points[^1].Y : (double?)null;
            series.Add(new CurveSeries($"{slot.Name} {option.DisplayName}", slot.Color, axis, points, latestValue));
        }

        var (y1Min, y1Max) = ResolveAxisRange(y1Vals, _autoY1, _manualY1Min, _manualY1Max);
        var (y2Min, y2Max) = ResolveAxisRange(y2Vals, _autoY2, _manualY2Min, _manualY2Max);

        _chartDrawable.UpdateData(series, markers, _chartWindowSeconds, y1Min, y1Max, y2Min, y2Max, tuneStartSeconds, tuneEndSeconds);
        RealtimeChartView.Invalidate();
    }

    private static (double Min, double Max) ResolveAxisRange(IReadOnlyList<double> values, bool auto, double manualMin, double manualMax)
    {
        if (!auto)
        {
            return (manualMin, manualMax);
        }

        if (values.Count == 0)
        {
            return (0.0, 100.0);
        }

        var min = values.Min();
        var max = values.Max();
        if (Math.Abs(max - min) < 1e-6)
        {
            return (min - 1.0, max + 1.0);
        }

        var pad = (max - min) * 0.1;
        return (min - pad, max + pad);
    }

    private PiEstimate? EstimatePiFromCurve(int curveIndex, double setpoint, TuningObjective objective)
    {
        const int minSampleCount = 4;
        var effectiveSamples = GetSamplesForLatestTuneRun();
        if (effectiveSamples.Count < minSampleCount)
        {
            return null;
        }

        var startTime = effectiveSamples[0].TimestampUtc;
        var samples = effectiveSamples
            .Where(s => s.Values.Length > curveIndex && s.Values[curveIndex].HasValue)
            .Select(s => new PointD((s.TimestampUtc - startTime).TotalSeconds, s.Values[curveIndex]!.Value))
            .ToList();

        if (samples.Count < minSampleCount)
        {
            return null;
        }

        var headCount = Math.Min(5, samples.Count / 3);
        var tailCount = Math.Min(5, samples.Count / 3);
        if (headCount <= 0 || tailCount <= 0)
        {
            return null;
        }

        var y0 = samples.Take(headCount).Average(p => p.Y);
        var yss = samples.TakeLast(tailCount).Average(p => p.Y);

        var du = setpoint - y0;
        var dy = yss - y0;
        if (Math.Abs(du) < 1e-6 || Math.Abs(dy) < 1e-6)
        {
            return null;
        }

        var dir = du >= 0 ? 1.0 : -1.0;
        var y10 = y0 + 0.10 * du;
        var y63 = y0 + 0.632 * du;

        var t10 = FindCrossingTime(samples, y10, dir);
        var t63 = FindCrossingTime(samples, y63, dir);
        if (t10 is null || t63 is null || t63 <= t10)
        {
            return null;
        }

        var tau = (t63.Value - t10.Value) / 0.895;
        var L = t10.Value - 0.105 * tau;
        if (tau <= 0.01)
        {
            return null;
        }
        if (L < 0.05)
        {
            L = 0.05;
        }

        var Ksigned = dy / du;
        if (Math.Abs(Ksigned) < 1e-6)
        {
            return null;
        }

        var directionMismatch = (du * dy) < 0.0;
        var K = Math.Abs(Ksigned);

        var policy = GetTuningPolicy(objective, tau, L);
        var lambda = policy.Lambda;
        var kp = tau / (K * (lambda + L));
        var ti = tau + (policy.TiLFactor * L);
        if (ti <= 0.01)
        {
            return null;
        }

        var ki = kp / ti;

        if (objective == TuningObjective.ConvergencePriority)
        {
            var overshootRatio = EstimateOvershootRatio(samples, setpoint, dir, du);
            if (overshootRatio > 0.0)
            {
                var kpScale = Math.Max(0.30, 1.0 / (1.0 + (5.0 * overshootRatio)));
                var kiScale = Math.Max(0.15, 1.0 / (1.0 + (10.0 * overshootRatio)));
                kp *= kpScale;
                ki *= kiScale;
            }
        }

        if (double.IsNaN(kp) || double.IsInfinity(kp) || double.IsNaN(ki) || double.IsInfinity(ki))
        {
            return null;
        }

        return new PiEstimate(kp, ki, Ksigned, L, tau, policy.ObjectiveLabel, directionMismatch);
    }

    private static TuningPolicy GetTuningPolicy(TuningObjective objective, double tau, double l)
    {
        return objective switch
        {
            TuningObjective.SpeedPriority => new TuningPolicy(Math.Max(0.35 * tau, 1.2 * l), 0.35, "速度優先"),
            TuningObjective.ConvergencePriority => new TuningPolicy(Math.Max(1.8 * tau, 4.0 * l), 1.4, "收斂優先"),
            _ => new TuningPolicy(Math.Max(0.8 * tau, 2.0 * l), 0.5, "平衡"),
        };
    }

    private static double EstimateOvershootRatio(IReadOnlyList<PointD> samples, double setpoint, double direction, double du)
    {
        var deltaRef = Math.Abs(du);
        if (deltaRef < 1e-6)
        {
            return 0.0;
        }

        if (direction >= 0.0)
        {
            var peak = samples.Max(p => p.Y);
            var overshoot = Math.Max(0.0, peak - setpoint);
            return overshoot / deltaRef;
        }

        var trough = samples.Min(p => p.Y);
        var undershoot = Math.Max(0.0, setpoint - trough);
        return undershoot / deltaRef;
    }

    private static double? FindCrossingTime(IReadOnlyList<PointD> points, double target, double direction)
    {
        for (var i = 1; i < points.Count; i++)
        {
            var y1 = points[i - 1].Y;
            var y2 = points[i].Y;

            var hit = direction >= 0
                ? (y1 <= target && y2 >= target)
                : (y1 >= target && y2 <= target);

            if (!hit)
            {
                continue;
            }

            var dy = y2 - y1;
            if (Math.Abs(dy) < 1e-9)
            {
                return points[i].X;
            }

            var ratio = (target - y1) / dy;
            return points[i - 1].X + ratio * (points[i].X - points[i - 1].X);
        }

        return null;
    }

    private void UpdateTuneRunCapture(uint sysStateRaw)
    {
        const uint tuneRunState = 8u;

        if (sysStateRaw == tuneRunState)
        {
            if (!_tuneRunActive)
            {
                _tuneRunActive = true;
                _lastTuneRunStartUtc = _pendingTuneRunStartUtc ?? DateTime.UtcNow;
                _lastTuneRunEndUtc = null;
                TuneRunStatusLabel.Text = $"記錄中: 從 {_lastTuneRunStartUtc.Value:HH:mm:ss} 開始擷取本次測試曲線";
            }

            return;
        }

        if (_tuneRunActive)
        {
            _tuneRunActive = false;
            _lastTuneRunEndUtc = DateTime.UtcNow;

            var stateLabel = LiveDataFormatter.FormatSysState(sysStateRaw);
            if (_lastTuneRunStartUtc.HasValue)
            {
                var duration = _lastTuneRunEndUtc.Value - _lastTuneRunStartUtc.Value;
                TuneRunStatusLabel.Text = $"已記錄: {_lastTuneRunStartUtc.Value:HH:mm:ss} ~ {_lastTuneRunEndUtc.Value:HH:mm:ss} ({duration.TotalSeconds:F1}s), end={stateLabel}";
            }
        }
    }

    private List<ChartSample> GetSamplesForLatestTuneRun()
    {
        const int minPreferredCount = 4;
        if (!_lastTuneRunStartUtc.HasValue)
        {
            return _chartSamples;
        }

        var startUtc = _lastTuneRunStartUtc.Value;
        var endUtc = _lastTuneRunEndUtc ?? DateTime.UtcNow;
        var filtered = _chartSamples
            .Where(sample => sample.TimestampUtc >= startUtc && sample.TimestampUtc <= endUtc)
            .ToList();

        return filtered.Count >= minPreferredCount ? filtered : _chartSamples;
    }

    private void Log(string message)
    {
        LogEditor.Text += $"[{DateTime.Now:HH:mm:ss}] {message}{Environment.NewLine}";
    }

    private async Task RefreshEndpointsAsync()
    {
        if (_endpointRefreshBusy)
        {
            return;
        }

        _endpointRefreshBusy = true;
        RefreshEndpointsButton.IsEnabled = false;
        try
        {
            var endpoints = await _transportClient.GetAvailableEndpointsAsync();
            EndpointPicker.ItemsSource = endpoints.ToList();
            if (endpoints.Count > 0)
            {
                EndpointPicker.SelectedIndex = 0;
                EndpointEntry.Text = endpoints[0].Id;
                Log($"Loaded {endpoints.Count} endpoint(s).");
            }
            else
            {
                EndpointPicker.SelectedItem = null;
                Log("No available endpoints found.");
            }
        }
        catch (Exception ex)
        {
            Log($"Load endpoints failed: {ex.Message}");
        }
        finally
        {
            RefreshEndpointsButton.IsEnabled = true;
            _endpointRefreshBusy = false;
        }
    }

    private sealed record ChartSample(DateTime TimestampUtc, double?[] Values);

    private sealed record ChartMarker(DateTime TimestampUtc, string Label, Color Color);

    private enum ChartSourceKind
    {
        None,
        LiveData,
        DfParam,
    }

    private enum ChartAxis
    {
        Y1,
        Y2,
    }

    private sealed class ChartSourceOption(ChartSourceKind kind, string displayName, LiveDataId? liveId, byte? paramId)
    {
        public ChartSourceKind Kind { get; } = kind;
        public string DisplayName { get; } = displayName;
        public LiveDataId? LiveId { get; } = liveId;
        public byte? ParamId { get; } = paramId;
        public string Key => Kind switch
        {
            ChartSourceKind.LiveData when LiveId.HasValue => $"LD:{(byte)LiveId.Value}",
            ChartSourceKind.DfParam when ParamId.HasValue => $"DF:{ParamId.Value}",
            _ => "NONE",
        };

        public override string ToString() => DisplayName;
    }

    private sealed class ChartAxisOption(ChartAxis axis, string label)
    {
        public ChartAxis Axis { get; } = axis;
        public string Label { get; } = label;
        public override string ToString() => Label;
    }

    private sealed class CurveSlot(string name, CheckBox enabledCheckBox, Picker sourcePicker, Picker axisPicker, Color color)
    {
        public string Name { get; } = name;
        public CheckBox EnabledCheckBox { get; } = enabledCheckBox;
        public Picker SourcePicker { get; } = sourcePicker;
        public Picker AxisPicker { get; } = axisPicker;
        public Color Color { get; } = color;
    }

    private sealed class CurveSeries(string label, Color color, ChartAxis axis, IReadOnlyList<PointF> points, double? latestValue)
    {
        public string Label { get; } = label;
        public Color Color { get; } = color;
        public ChartAxis Axis { get; } = axis;
        public IReadOnlyList<PointF> Points { get; } = points;
        public double? LatestValue { get; } = latestValue;
    }

    private sealed record PointD(double X, double Y);

    private enum TuningObjective
    {
        Balanced,
        SpeedPriority,
        ConvergencePriority,
    }

    private sealed class TuningObjectiveOption(TuningObjective objective, string display)
    {
        public TuningObjective Objective { get; } = objective;
        public string Display { get; } = display;
        public override string ToString() => Display;
    }

    private enum PidTarget
    {
        M1,
        M2,
    }

    private sealed class PidTargetOption(PidTarget target, string label)
    {
        public PidTarget Target { get; } = target;
        public string Label { get; } = label;
        public override string ToString() => Label;
    }

    private sealed record TuningPolicy(double Lambda, double TiLFactor, string ObjectiveLabel);

    private sealed record PiEstimate(double Kp, double Ki, double K, double L, double Tau, string ObjectiveLabel, bool DirectionMismatch);

    private sealed class RealTimeChartDrawable : IDrawable
    {
        private IReadOnlyList<CurveSeries> _series = [];
        private IReadOnlyList<ChartMarker> _markers = [];
        private int _windowSeconds = 120;
        private double _y1Min = 0.0;
        private double _y1Max = 100.0;
        private double _y2Min = 0.0;
        private double _y2Max = 100.0;
        private float? _tuneStartSeconds;
        private float? _tuneEndSeconds;

        public void UpdateData(IReadOnlyList<CurveSeries> series, IReadOnlyList<ChartMarker> markers, int windowSeconds, double y1Min, double y1Max, double y2Min, double y2Max, float? tuneStartSeconds, float? tuneEndSeconds)
        {
            _series = series;
            _markers = markers;
            _windowSeconds = Math.Max(10, windowSeconds);
            _y1Min = y1Min;
            _y1Max = y1Max;
            _y2Min = y2Min;
            _y2Max = y2Max;
            _tuneStartSeconds = tuneStartSeconds;
            _tuneEndSeconds = tuneEndSeconds;
        }

        public void Draw(ICanvas canvas, RectF dirtyRect)
        {
            canvas.SaveState();
            canvas.FillColor = Color.FromArgb("#F5F6F8");
            canvas.FillRectangle(dirtyRect);

            var left = 56f;
            var top = 26f;
            var right = 56f;
            var bottom = 28f;
            var plot = new RectF(left, top, dirtyRect.Width - left - right, dirtyRect.Height - top - bottom);

            canvas.StrokeColor = Color.FromArgb("#C7CBD3");
            canvas.StrokeSize = 1f;
            canvas.DrawRectangle(plot);

            for (var i = 1; i <= 4; i++)
            {
                var y = plot.Y + i * (plot.Height / 5f);
                canvas.StrokeColor = Color.FromArgb("#E5E7EB");
                canvas.DrawLine(plot.Left, y, plot.Right, y);
            }

            canvas.FontSize = 11;
            canvas.FontColor = Color.FromArgb("#D14334");
            canvas.DrawString(_y1Max.ToString("F2", CultureInfo.InvariantCulture), 2, plot.Top - 8, left - 8, 16, HorizontalAlignment.Right, VerticalAlignment.Center);
            canvas.DrawString(_y1Min.ToString("F2", CultureInfo.InvariantCulture), 2, plot.Bottom - 8, left - 8, 16, HorizontalAlignment.Right, VerticalAlignment.Center);

            canvas.FontColor = Color.FromArgb("#2C7BE5");
            canvas.DrawString(_y2Max.ToString("F2", CultureInfo.InvariantCulture), plot.Right + 8, plot.Top - 8, right - 8, 16, HorizontalAlignment.Left, VerticalAlignment.Center);
            canvas.DrawString(_y2Min.ToString("F2", CultureInfo.InvariantCulture), plot.Right + 8, plot.Bottom - 8, right - 8, 16, HorizontalAlignment.Left, VerticalAlignment.Center);

            canvas.FontColor = Color.FromArgb("#374151");
            canvas.DrawString("0s", plot.Left - 2, plot.Bottom + 4, 32, 16, HorizontalAlignment.Left, VerticalAlignment.Top);
            canvas.DrawString($"{_windowSeconds}s", plot.Right - 38, plot.Bottom + 4, 40, 16, HorizontalAlignment.Right, VerticalAlignment.Top);

            DrawTuneMarker(canvas, plot, _tuneStartSeconds, _windowSeconds, Color.FromArgb("#7C3AED"), "TUNE START");
            DrawTuneMarker(canvas, plot, _tuneEndSeconds, _windowSeconds, Color.FromArgb("#DC2626"), "TUNE END");
            DrawStateMarkers(canvas, plot, _markers, _windowSeconds);

            foreach (var s in _series)
            {
                var yMin = s.Axis == ChartAxis.Y1 ? _y1Min : _y2Min;
                var yMax = s.Axis == ChartAxis.Y1 ? _y1Max : _y2Max;
                DrawSeries(canvas, plot, s.Points, _windowSeconds, yMin, yMax, s.Color);
            }

            var legendX = plot.Left;
            var legendY = 4f;
            canvas.FontSize = 11;
            foreach (var s in _series.Take(4))
            {
                canvas.StrokeColor = s.Color;
                canvas.StrokeSize = 2f;
                canvas.DrawLine(legendX, legendY + 8, legendX + 16, legendY + 8);
                canvas.FontColor = Color.FromArgb("#1F2937");
                var axisText = s.Axis == ChartAxis.Y1 ? "Y1" : "Y2";
                var latestText = s.LatestValue.HasValue
                    ? $"{s.LatestValue.Value:F2}"
                    : "-";
                canvas.DrawString($"{s.Label} ({axisText}) = {latestText}", legendX + 20, legendY, 260, 16, HorizontalAlignment.Left, VerticalAlignment.Center);
                legendX += 280f;
                if (legendX > plot.Right - 260f)
                {
                    legendX = plot.Left;
                    legendY += 14f;
                }
            }

            canvas.RestoreState();
        }

        private static void DrawTuneMarker(ICanvas canvas, RectF plot, float? seconds, int windowSeconds, Color color, string label)
        {
            if (!seconds.HasValue || windowSeconds <= 0)
            {
                return;
            }

            var x = plot.Left + (seconds.Value / windowSeconds) * plot.Width;
            if (x < plot.Left || x > plot.Right)
            {
                return;
            }

            canvas.StrokeColor = color;
            canvas.StrokeSize = 1.5f;
            canvas.StrokeDashPattern = [6f, 4f];
            canvas.DrawLine(x, plot.Top, x, plot.Bottom);
            canvas.StrokeDashPattern = null;

            canvas.FontSize = 10;
            canvas.FontColor = color;
            canvas.DrawString(label, x + 4f, plot.Top + 2f, 72f, 12f, HorizontalAlignment.Left, VerticalAlignment.Top);
        }

        private static void DrawStateMarkers(ICanvas canvas, RectF plot, IReadOnlyList<ChartMarker> markers, int windowSeconds)
        {
            if (markers.Count == 0 || windowSeconds <= 0)
            {
                return;
            }

            var startUtc = DateTime.UtcNow - TimeSpan.FromSeconds(windowSeconds);
            var labelTop = plot.Top + 16f;

            foreach (var marker in markers)
            {
                var seconds = (float)(marker.TimestampUtc - startUtc).TotalSeconds;
                var x = plot.Left + (seconds / windowSeconds) * plot.Width;
                if (x < plot.Left || x > plot.Right)
                {
                    continue;
                }

                canvas.StrokeColor = marker.Color;
                canvas.StrokeSize = 1f;
                canvas.StrokeDashPattern = [2f, 3f];
                canvas.DrawLine(x, plot.Top, x, plot.Bottom);
                canvas.StrokeDashPattern = null;

                canvas.SaveState();
                canvas.Translate(x + 3f, labelTop);
                canvas.Rotate(90f);
                canvas.FontSize = 9f;
                canvas.FontColor = marker.Color;
                canvas.DrawString(marker.Label, 0f, 0f, 84f, 12f, HorizontalAlignment.Left, VerticalAlignment.Top);
                canvas.RestoreState();
            }
        }

        private static void DrawSeries(ICanvas canvas, RectF plot, IReadOnlyList<PointF> src, int windowSec, double yMin, double yMax, Color color)
        {
            if (src.Count < 2)
            {
                return;
            }

            var path = new PathF();
            var first = true;
            var ySpan = Math.Max(1e-6, yMax - yMin);

            for (var i = 0; i < src.Count; i++)
            {
                var xNorm = Math.Clamp(src[i].X / Math.Max(1, windowSec), 0f, 1f);
                var yNorm = (float)Math.Clamp((src[i].Y - yMin) / ySpan, 0.0, 1.0);
                var x = plot.Left + xNorm * plot.Width;
                var y = plot.Bottom - yNorm * plot.Height;

                if (first)
                {
                    path.MoveTo(x, y);
                    first = false;
                }
                else
                {
                    path.LineTo(x, y);
                }
            }

            canvas.StrokeColor = color;
            canvas.StrokeSize = 2f;
            canvas.DrawPath(path);
        }
    }
}
