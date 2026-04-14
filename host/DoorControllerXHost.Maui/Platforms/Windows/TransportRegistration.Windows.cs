using DoorControllerXHost.Core.Abstractions;
using DoorControllerXHost.Maui.PlatformServices;
using DoorControllerXHost.Maui.Platforms.Windows.Services;
using Microsoft.Extensions.DependencyInjection;

namespace DoorControllerXHost.Maui.PlatformServices;

internal static partial class TransportRegistration
{
    public static partial void RegisterPlatformTransport(IServiceCollection services)
    {
        services.AddSingleton<ITransportClient, WindowsSerialTransport>();
    }
}
