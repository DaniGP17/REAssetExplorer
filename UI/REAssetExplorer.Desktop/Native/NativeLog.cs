using System.Collections.Concurrent;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace REAssetExplorer.Desktop.Native;

public enum LogLevel
{
    Info,
    Warning,
    Error
}

public readonly record struct LogMessage(DateTime Time, LogLevel Level, string Text);

// The native callback runs on worker and render threads; the UI drains the queue. Each message also
// reaches the session's file before Write returns, so a crash loses none of them.
public static unsafe class NativeLog
{
    private const int KeptLogFiles = 50;

    private static readonly ConcurrentQueue<LogMessage> Pending = new();
    private static readonly Lock FileLock = new();
    private static StreamWriter? file;
    private static bool fileFailed;

    // Logs/<local time of the first message>.log
    public static string? FilePath { get; private set; }

    public static void Install() => NativeMethods.rae_set_log_callback(&OnNativeLog);

    public static void Write(LogLevel level, string text)
    {
        var message = new LogMessage(DateTime.Now, level, text);
        Pending.Enqueue(message);
        AppendToFile(message);
    }

    public static bool TryDequeue(out LogMessage message) => Pending.TryDequeue(out message);

    private static void AppendToFile(LogMessage message)
    {
        lock (FileLock)
        {
            if (fileFailed) return;
            try
            {
                file ??= OpenFile(message.Time);
                string level = message.Level switch { LogLevel.Warning => "WARN", LogLevel.Error => "ERROR", _ => "INFO" };
                file.WriteLine($"{message.Time:HH:mm:ss.fff} {level,-5} {message.Text}");
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException)
            {
                fileFailed = true;
            }
        }
    }

    private static StreamWriter OpenFile(DateTime time)
    {
        Directory.CreateDirectory(Diagnostics.LogsDirectory);
        string name = time.ToString("yyyy-MM-dd_HH-mm-ss");
        string path = Path.Combine(Diagnostics.LogsDirectory, name + ".log");
        for (int i = 2; File.Exists(path); i++) path = Path.Combine(Diagnostics.LogsDirectory, $"{name}_{i}.log");
        var stream = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.Read);
        FilePath = path;
        foreach (string old in Directory.GetFiles(Diagnostics.LogsDirectory, "*.log").OrderDescending().Skip(KeptLogFiles))
        {
            try
            {
                File.Delete(old);
            }
            catch (IOException)
            {
            }
        }
        return new StreamWriter(stream) { AutoFlush = true };
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void OnNativeLog(int level, byte* message)
    {
        string text = Marshal.PtrToStringUTF8((IntPtr)message) ?? string.Empty;
        Write(level switch { 1 => LogLevel.Warning, 2 => LogLevel.Error, _ => LogLevel.Info }, text);
    }
}
