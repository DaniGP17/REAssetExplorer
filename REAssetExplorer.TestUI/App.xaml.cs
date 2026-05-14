using System.Windows;
using REAssetExplorer.Core.Games;

namespace REAssetExplorer.TestUI;

public partial class App : Application
{
    public static IGameProvider? CurrentProvider { get; set; }

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        GameProviderRegistry.AutoRegisterProviders();
        new SelectGameWindow().Show();
    }
}
