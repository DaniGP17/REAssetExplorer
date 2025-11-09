using REAssetExplorer.Core.Games;
using Microsoft.Extensions.DependencyInjection;
using System.Windows;
using REAssetExplorer.UI.Local;
using REAssetExplorer.UI.Remote;
using System.Net.Http;

namespace REAssetExplorer.UI;

/// <summary>
/// Interaction logic for App.xaml
/// </summary>
public partial class App : Application
{
    public static IServiceProvider Services { get; private set; } = null!;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        
        ConfigureServices();
        RegisterGameProviders();
    }

    private void ConfigureServices()
    {
        var services = new ServiceCollection();
        
        // Storage services
        services.AddSingleton<FileListStorage>();
        
        // HTTP services
        services.AddSingleton<HttpClient>();
        services.AddSingleton<FileListApiClient>();
        
        // Application services
        services.AddSingleton<Services.GameLoadingService>();
        
        Services = services.BuildServiceProvider();
    }

    private static void RegisterGameProviders()
    {
        GameProviderRegistry.AutoRegisterProviders();
    }
}
