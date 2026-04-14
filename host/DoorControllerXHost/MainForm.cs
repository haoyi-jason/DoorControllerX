using DoorControllerXHost.Core.Models;
using DoorControllerXHost.Core.Services;
using DoorControllerXHost.Services;

namespace DoorControllerXHost;

internal sealed class MainForm : Form
{
    private readonly DoorControllerSerialClient _client = new();
    private readonly System.Windows.Forms.Timer _pollTimer = new();
    private readonly Dictionary<LiveDataId, Label> _valueLabels = [];

    private ComboBox _portCombo = null!;
    private NumericUpDown _baudInput = null!;
    private Button _refreshPortsButton = null!;
    private Button _connectButton = null!;
    private Button _disconnectButton = null!;
    private Button _readSnapshotButton = null!;
    private Button _startPollingButton = null!;
    private Button _stopPollingButton = null!;
    private TextBox _logBox = null!;
    private bool _pollBusy;

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
        LiveDataId.ErrorCode,
        LiveDataId.LockRetryCount,
        LiveDataId.OperationTimeMs,
        LiveDataId.OpenCount,
        LiveDataId.CloseCount,
    ];

    public MainForm()
    {
        BuildUi();
        WireEvents();
        _pollTimer.Interval = 250;
        _pollTimer.Tick += async (_, _) => await PollSnapshotAsync();
        RefreshPorts();
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        _pollTimer.Stop();
        _client.Dispose();
        base.OnFormClosing(e);
    }

    private void BuildUi()
    {
        Text = "DoorControllerX Host";
        Width = 1100;
        Height = 760;
        StartPosition = FormStartPosition.CenterScreen;

        var toolbar = new FlowLayoutPanel
        {
            Dock = DockStyle.Top,
            Height = 44,
            FlowDirection = FlowDirection.LeftToRight,
            Padding = new Padding(8, 8, 8, 0),
            WrapContents = false,
        };

        _portCombo = new ComboBox { Width = 120, DropDownStyle = ComboBoxStyle.DropDownList };
        _baudInput = new NumericUpDown { Width = 100, Minimum = 1200, Maximum = 2000000, Value = 115200 };
        _refreshPortsButton = new Button { Text = "Refresh Ports", AutoSize = true };
        _connectButton = new Button { Text = "Connect", AutoSize = true };
        _disconnectButton = new Button { Text = "Disconnect", AutoSize = true, Enabled = false };
        _readSnapshotButton = new Button { Text = "Read Snapshot", AutoSize = true, Enabled = false };
        _startPollingButton = new Button { Text = "Start Poll", AutoSize = true, Enabled = false };
        _stopPollingButton = new Button { Text = "Stop Poll", AutoSize = true, Enabled = false };

        toolbar.Controls.Add(new Label { Text = "COM", AutoSize = true, Padding = new Padding(0, 8, 0, 0) });
        toolbar.Controls.Add(_portCombo);
        toolbar.Controls.Add(new Label { Text = "Baud", AutoSize = true, Padding = new Padding(8, 8, 0, 0) });
        toolbar.Controls.Add(_baudInput);
        toolbar.Controls.Add(_refreshPortsButton);
        toolbar.Controls.Add(_connectButton);
        toolbar.Controls.Add(_disconnectButton);
        toolbar.Controls.Add(_readSnapshotButton);
        toolbar.Controls.Add(_startPollingButton);
        toolbar.Controls.Add(_stopPollingButton);

        var content = new SplitContainer
        {
            Dock = DockStyle.Fill,
            Orientation = Orientation.Horizontal,
            SplitterDistance = 420,
        };

        var grid = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 2,
            AutoScroll = true,
            Padding = new Padding(12),
        };
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 260));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));

        var row = 0;
        foreach (var id in SnapshotIds)
        {
            grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 32));
            var nameLabel = new Label
            {
                Text = id.ToString(),
                Dock = DockStyle.Fill,
                TextAlign = ContentAlignment.MiddleLeft,
                Font = new Font("Consolas", 10F, FontStyle.Regular),
            };
            var valueLabel = new Label
            {
                Text = "-",
                Dock = DockStyle.Fill,
                TextAlign = ContentAlignment.MiddleLeft,
                Font = new Font("Consolas", 10F, FontStyle.Bold),
                BorderStyle = BorderStyle.FixedSingle,
                Padding = new Padding(6, 6, 6, 6),
            };
            _valueLabels[id] = valueLabel;
            grid.Controls.Add(nameLabel, 0, row);
            grid.Controls.Add(valueLabel, 1, row);
            row++;
        }

        _logBox = new TextBox
        {
            Dock = DockStyle.Fill,
            Multiline = true,
            ReadOnly = true,
            ScrollBars = ScrollBars.Vertical,
            Font = new Font("Consolas", 10F, FontStyle.Regular),
        };

        content.Panel1.Controls.Add(grid);
        content.Panel2.Controls.Add(_logBox);

        Controls.Add(content);
        Controls.Add(toolbar);
    }

    private void WireEvents()
    {
        _refreshPortsButton.Click += (_, _) => RefreshPorts();
        _connectButton.Click += async (_, _) => await ConnectAsync();
        _disconnectButton.Click += (_, _) => Disconnect();
        _readSnapshotButton.Click += async (_, _) => await PollSnapshotAsync();
        _startPollingButton.Click += (_, _) =>
        {
            _pollTimer.Start();
            _startPollingButton.Enabled = false;
            _stopPollingButton.Enabled = true;
            Log("Polling started.");
        };
        _stopPollingButton.Click += (_, _) =>
        {
            _pollTimer.Stop();
            _startPollingButton.Enabled = true;
            _stopPollingButton.Enabled = false;
            Log("Polling stopped.");
        };
    }

    private void RefreshPorts()
    {
        var ports = System.IO.Ports.SerialPort.GetPortNames().OrderBy(x => x).ToArray();
        _portCombo.Items.Clear();
        _portCombo.Items.AddRange(ports);
        if (ports.Length > 0)
        {
            _portCombo.SelectedIndex = 0;
        }
        Log($"Found {ports.Length} serial port(s).");
    }

    private async Task ConnectAsync()
    {
        if (_portCombo.SelectedItem is not string portName)
        {
            MessageBox.Show("Select a COM port first.", "DoorControllerX Host", MessageBoxButtons.OK, MessageBoxIcon.Information);
            return;
        }

        try
        {
            await _client.ConnectAsync(portName, (int)_baudInput.Value);
            _connectButton.Enabled = false;
            _disconnectButton.Enabled = true;
            _readSnapshotButton.Enabled = true;
            _startPollingButton.Enabled = true;
            Log($"Connected: {portName} @ {(int)_baudInput.Value}.");
            await PollSnapshotAsync();
        }
        catch (Exception ex)
        {
            Log($"Connect failed: {ex.Message}");
            MessageBox.Show(ex.Message, "Connect Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private void Disconnect()
    {
        _pollTimer.Stop();
        _ = _client.DisconnectAsync();
        _connectButton.Enabled = true;
        _disconnectButton.Enabled = false;
        _readSnapshotButton.Enabled = false;
        _startPollingButton.Enabled = false;
        _stopPollingButton.Enabled = false;
        Log("Disconnected.");
    }

    private async Task PollSnapshotAsync()
    {
        if (_pollBusy || !_client.IsOpen)
        {
            return;
        }

        _pollBusy = true;
        try
        {
            foreach (var id in SnapshotIds)
            {
                var value = await _client.ReadLiveAsync((byte)id);
                _valueLabels[id].Text = LiveDataFormatter.FormatValue(id, value);
            }
        }
        catch (Exception ex)
        {
            Log($"Read snapshot failed: {ex.Message}");
            _pollTimer.Stop();
            _startPollingButton.Enabled = _client.IsOpen;
            _stopPollingButton.Enabled = false;
        }
        finally
        {
            _pollBusy = false;
        }
    }

    private void Log(string message)
    {
        _logBox.AppendText($"[{DateTime.Now:HH:mm:ss}] {message}{Environment.NewLine}");
    }
}
