using REAssetExplorer.Core.Games;
using Microsoft.Extensions.DependencyInjection;
using System.Windows;
using REAssetExplorer.UI.Local;
using REAssetExplorer.UI.Remote;
using System.Net.Http;
using REAssetExplorer.UI.Services.Loaders;

namespace REAssetExplorer.UI;

/// <summary>
/// Interaction logic for App.xaml
/// </summary>
public partial class App : WpfApplication
{
    public static IServiceProvider Services { get; private set; } = null!;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        
        ConfigureServices();
        RegisterGameProviders();
        RegisterResourceLoaders();
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
        services.AddSingleton<Services.CacheService>();
        services.AddSingleton<Services.GameResourceLoaderRegistry>();
        
        Services = services.BuildServiceProvider();
    }

    private static void RegisterGameProviders()
    {
        GameProviderRegistry.AutoRegisterProviders();
    }

    private static void RegisterResourceLoaders()
    {
        var registry = Services.GetRequiredService<Services.GameResourceLoaderRegistry>();
        
        registry.Register(new MaterialsResourceLoader());
    }
}

