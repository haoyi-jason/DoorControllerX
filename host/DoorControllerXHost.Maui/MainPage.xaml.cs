using DoorControllerXHost.Core.Abstractions;
using DoorControllerXHost.Core.Models;
using DoorControllerXHost.Core.Services;

namespace DoorControllerXHost.Maui;

public partial class MainPage : ContentPage
{
	private readonly ITransportClient _transportClient;
	private readonly ParametersPage _parametersPage;
	private readonly IDispatcherTimer _pollTimer;
	private bool _pollBusy;
	private bool _endpointRefreshBusy;

	private static readonly LiveDataId[] SnapshotIds =
	[
		LiveDataId.SysState,
		LiveDataId.M1State,
		LiveDataId.M2State,
		LiveDataId.BlockCount,
		LiveDataId.BlockRetryCount,
		LiveDataId.BlockSourceState,
		LiveDataId.ErrorCode,
		LiveDataId.LockRetryCount,
		LiveDataId.OperationTimeMs,
		LiveDataId.OpenCount,
		LiveDataId.CloseCount,
	];

	public MainPage(ITransportClient transportClient, ParametersPage parametersPage)
	{
		InitializeComponent();
		_transportClient = transportClient;
		_parametersPage = parametersPage;
		_pollTimer = Dispatcher.CreateTimer();
		_pollTimer.Interval = TimeSpan.FromMilliseconds(250);
		_pollTimer.Tick += async (_, _) => await PollSnapshotAsync();

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
			Log($"Connected: {EndpointEntry.Text.Trim()} @ {baudRate}.");
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
		await _transportClient.DisconnectAsync();
		ConnectButton.IsEnabled = true;
		DisconnectButton.IsEnabled = false;
		ReadSnapshotButton.IsEnabled = false;
		PollButton.IsEnabled = false;
		PollButton.Text = "Start Poll";
		Log("Disconnected.");
	}

	private async void OnReadSnapshotClicked(object? sender, EventArgs e)
	{
		await PollSnapshotAsync();
	}

	private void OnPollClicked(object? sender, EventArgs e)
	{
		if (_pollTimer.IsRunning)
		{
			_pollTimer.Stop();
			PollButton.Text = "Start Poll";
			Log("Polling stopped.");
		}
		else
		{
			_pollTimer.Start();
			PollButton.Text = "Stop Poll";
			Log("Polling started.");
		}
	}

	private async void OnParametersClicked(object? sender, EventArgs e)
	{
		await Navigation.PushAsync(_parametersPage);
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
				var value = await _transportClient.ReadLiveAsync((byte)id);
				ApplyValue(id, value);
			}
		}
		catch (Exception ex)
		{
			_pollTimer.Stop();
			PollButton.Text = "Start Poll";
			Log($"Read snapshot failed: {ex.Message}");
		}
		finally
		{
			_pollBusy = false;
		}
	}

	private void ApplyValue(LiveDataId id, uint value)
	{
		var formatted = LiveDataFormatter.FormatValue(id, value);
		switch (id)
		{
			case LiveDataId.SysState:
				SysStateValueLabel.Text = formatted;
				break;
			case LiveDataId.M1State:
				M1StateValueLabel.Text = formatted;
				break;
			case LiveDataId.M2State:
				M2StateValueLabel.Text = formatted;
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
		}
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
}
