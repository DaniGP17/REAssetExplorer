namespace REAssetExplorer.Desktop.Native;

// Session logs and crash reports under %LOCALAPPDATA%/REAssetExplorer.
public static class Diagnostics
{
    private const int KeptCrashes = 20;

    private static readonly string Root =
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "REAssetExplorer");

    public static string LogsDirectory { get; } = Path.Combine(Root, "Logs");
    public static string CrashesDirectory { get; } = Path.Combine(Root, "Crashes");

    public static void Install()
    {
        NativeLog.Install();
        NativeLog.Write(LogLevel.Info, $"REAssetExplorer {typeof(Diagnostics).Assembly.GetName().Version} started");
        PruneCrashes();
        NativeMethods.rae_install_crash_handler(CrashesDirectory, NativeLog.FilePath ?? string.Empty);
        AppDomain.CurrentDomain.UnhandledException += OnUnhandledException;
        TaskScheduler.UnobservedTaskException += (_, e) => NativeLog.Write(LogLevel.Warning, $"Unobserved task exception: {e.Exception}");
    }

    private static void OnUnhandledException(object sender, UnhandledExceptionEventArgs e)
    {
        string text = $"Unhandled .NET exception: {e.ExceptionObject}";
        NativeLog.Write(LogLevel.Error, text);
        NativeMethods.rae_write_crash_report(text);
    }

    private static void PruneCrashes()
    {
        if (!Directory.Exists(CrashesDirectory)) return;
        foreach (string dump in Directory.GetFiles(CrashesDirectory, "*.dmp").OrderDescending().Skip(KeptCrashes))
        {
            try
            {
                File.Delete(dump);
                File.Delete(Path.ChangeExtension(dump, ".txt"));
            }
            catch (IOException)
            {
            }
        }
    }
}
