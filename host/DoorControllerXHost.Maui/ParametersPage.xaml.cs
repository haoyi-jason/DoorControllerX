using DoorControllerXHost.Core.Abstractions;
using DoorControllerXHost.Core.Models;
using DoorControllerXHost.Core.Services;

namespace DoorControllerXHost.Maui;

public partial class ParametersPage : ContentPage
{
    private readonly ITransportClient _transportClient;

    public ParametersPage(ITransportClient transportClient)
    {
        InitializeComponent();
        _transportClient = transportClient;
        ParamPicker.ItemsSource = DfParameterCatalog.All.ToList();
        if (ParamPicker.Items.Count > 0)
        {
            ParamPicker.SelectedIndex = 0;
        }
    }

    private async void OnBackClicked(object? sender, EventArgs e)
    {
        if (Navigation.NavigationStack.Count > 1)
        {
            await Navigation.PopAsync();
        }
    }

    private void OnParamSelectionChanged(object? sender, EventArgs e)
    {
        if (ParamPicker.SelectedItem is not DfParameterInfo info)
        {
            return;
        }

        ParamIdLabel.Text = ((byte)info.Id).ToString();
        ParamRangeLabel.Text = $"{info.Min} ~ {info.Max}";
        ParamDefaultLabel.Text = info.Default.ToString();
        if (string.IsNullOrWhiteSpace(ParamValueEntry.Text))
        {
            ParamValueEntry.Text = info.Default.ToString();
        }
    }

    private async void OnReadClicked(object? sender, EventArgs e)
    {
        if (!EnsureConnected())
        {
            return;
        }

        if (ParamPicker.SelectedItem is not DfParameterInfo info)
        {
            return;
        }

        try
        {
            var value = await _transportClient.ReadParamAsync((byte)info.Id);
            ParamValueEntry.Text = value.ToString();
            AppendResult($"READ  {info.Name} ({(byte)info.Id}) = {value}");
        }
        catch (Exception ex)
        {
            await DisplayAlert("Read Error", ex.Message, "OK");
            AppendResult($"READ  {info.Name} failed: {ex.Message}");
        }
    }

    private async void OnWriteClicked(object? sender, EventArgs e)
    {
        if (!EnsureConnected())
        {
            return;
        }

        if (ParamPicker.SelectedItem is not DfParameterInfo info)
        {
            return;
        }

        if (!uint.TryParse(ParamValueEntry.Text, out var value))
        {
            await DisplayAlert("Invalid Value", "Please enter a valid unsigned integer.", "OK");
            return;
        }

        if (value < info.Min || value > info.Max)
        {
            await DisplayAlert("Out Of Range", $"{info.Name} range: {info.Min} ~ {info.Max}", "OK");
            return;
        }

        try
        {
            await _transportClient.WriteParamAsync((byte)info.Id, value);
            AppendResult($"WRITE {info.Name} ({(byte)info.Id}) = {value}");

            var readBack = await _transportClient.ReadParamAsync((byte)info.Id);
            ParamValueEntry.Text = readBack.ToString();

            if (readBack == value)
            {
                AppendResult($"VERIFY {info.Name} ({(byte)info.Id}) PASS (readback={readBack})");
            }
            else
            {
                AppendResult($"VERIFY {info.Name} ({(byte)info.Id}) FAIL (expected={value}, readback={readBack})");
                await DisplayAlert("Verify Failed", $"{info.Name} readback mismatch. Expected {value}, got {readBack}.", "OK");
            }
        }
        catch (Exception ex)
        {
            await DisplayAlert("Write Error", ex.Message, "OK");
            AppendResult($"WRITE {info.Name} failed: {ex.Message}");
        }
    }

    private async void OnReadAllClicked(object? sender, EventArgs e)
    {
        if (!EnsureConnected())
        {
            return;
        }

        ResultEditor.Text = string.Empty;
        foreach (var info in DfParameterCatalog.All)
        {
            try
            {
                var value = await _transportClient.ReadParamAsync((byte)info.Id);
                AppendResult($"{(byte)info.Id:D2} {info.Name,-30} = {value}");
            }
            catch (Exception ex)
            {
                AppendResult($"{(byte)info.Id:D2} {info.Name,-30} = ERR: {ex.Message}");
            }
        }
    }

    private bool EnsureConnected()
    {
        if (_transportClient.IsOpen)
        {
            return true;
        }

        _ = DisplayAlert("Not Connected", "Please connect from the main page first.", "OK");
        return false;
    }

    private void AppendResult(string line)
    {
        ResultEditor.Text += $"[{DateTime.Now:HH:mm:ss}] {line}{Environment.NewLine}";
    }
}
