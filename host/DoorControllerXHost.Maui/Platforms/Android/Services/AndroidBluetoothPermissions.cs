using Android;
using Microsoft.Maui.ApplicationModel;
using System.Runtime.Versioning;

namespace DoorControllerXHost.Maui.Platforms.Android.Services;

[SupportedOSPlatform("android31.0")]
internal sealed class BluetoothConnectPermission : Permissions.BasePlatformPermission
{
    public override (string androidPermission, bool isRuntime)[] RequiredPermissions =>
    [
        (Manifest.Permission.BluetoothConnect, true),
    ];
}

[SupportedOSPlatform("android31.0")]
internal sealed class BluetoothScanPermission : Permissions.BasePlatformPermission
{
    public override (string androidPermission, bool isRuntime)[] RequiredPermissions =>
    [
        (Manifest.Permission.BluetoothScan, true),
    ];
}
