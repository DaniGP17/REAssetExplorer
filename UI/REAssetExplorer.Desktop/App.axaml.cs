using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Avalonia.Controls;
using REAssetExplorer.Desktop.ViewModels;
using REAssetExplorer.Desktop.Views;

namespace REAssetExplorer.Desktop;

public partial class App : Application
{
    public override void Initialize() => AvaloniaXamlLoader.Load(this);

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            var main = new MainViewModel();
            desktop.MainWindow = new MainWindow
            {
                DataContext = main,
                StartupArgs = desktop.Args ?? []
            };
            // Editor windows are top-level too; they must not keep the app alive.
            desktop.ShutdownMode = ShutdownMode.OnMainWindowClose;
            desktop.Exit += (_, _) => main.Shutdown();
        }
        base.OnFrameworkInitializationCompleted();
    }
}
