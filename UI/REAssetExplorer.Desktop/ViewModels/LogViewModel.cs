using Avalonia.Collections;
using Avalonia.Media;
using Avalonia.Media.Immutable;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using REAssetExplorer.Desktop.Native;

namespace REAssetExplorer.Desktop.ViewModels;

public sealed class LogEntry(LogMessage message)
{
    private static readonly IBrush InfoBrush = new ImmutableSolidColorBrush(Color.Parse("#7D8591"));
    private static readonly IBrush WarningBrush = new ImmutableSolidColorBrush(Color.Parse("#E0A33A"));
    private static readonly IBrush ErrorBrush = new ImmutableSolidColorBrush(Color.Parse("#E0655A"));
    private static readonly IBrush TextBrush = new ImmutableSolidColorBrush(Color.Parse("#B7BDC7"));

    public string Time { get; } = message.Time.ToString("HH:mm:ss");
    public string Text { get; } = message.Text;
    public LogLevel Level { get; } = message.Level;
    public string LevelText => Level switch { LogLevel.Warning => "WARN", LogLevel.Error => "ERROR", _ => "INFO" };
    public IBrush LevelBrush => Level switch { LogLevel.Warning => WarningBrush, LogLevel.Error => ErrorBrush, _ => InfoBrush };
    public IBrush MessageBrush => Level switch { LogLevel.Warning => WarningBrush, LogLevel.Error => ErrorBrush, _ => TextBrush };
}

public sealed partial class LogViewModel : ObservableObject
{
    private const int MaxEntries = 5000;
    private const int MaxDrainPerTick = 1000;

    public AvaloniaList<LogEntry> Entries { get; } = [];
    public AvaloniaList<LogEntry> FilteredEntries { get; } = [];

    [ObservableProperty]
    private int infoCount;

    [ObservableProperty]
    private int warningCount;

    [ObservableProperty]
    private int errorCount;

    [ObservableProperty]
    private string searchText = string.Empty;

    [ObservableProperty]
    private bool showInfo = true;

    [ObservableProperty]
    private bool showWarning = true;

    [ObservableProperty]
    private bool showError = true;

    [ObservableProperty]
    private string statusText = string.Empty;

    public event Action? EntriesAdded;

    public void Drain()
    {
        var batch = new List<LogEntry>();
        while (batch.Count < MaxDrainPerTick && NativeLog.TryDequeue(out LogMessage message))
        {
            batch.Add(new LogEntry(message));
            switch (message.Level)
            {
                case LogLevel.Warning: WarningCount++; break;
                case LogLevel.Error: ErrorCount++; break;
                default: InfoCount++; break;
            }
        }
        if (batch.Count == 0) return;

        Entries.AddRange(batch);
        if (Entries.Count > MaxEntries) Entries.RemoveRange(0, Entries.Count - MaxEntries);

        List<LogEntry> matched = batch.Where(Matches).ToList();
        if (matched.Count > 0)
        {
            FilteredEntries.AddRange(matched);
            if (FilteredEntries.Count > MaxEntries) FilteredEntries.RemoveRange(0, FilteredEntries.Count - MaxEntries);
        }
        UpdateStatus();
        EntriesAdded?.Invoke();
    }

    partial void OnSearchTextChanged(string value) => Refresh();
    partial void OnShowInfoChanged(bool value) => Refresh();
    partial void OnShowWarningChanged(bool value) => Refresh();
    partial void OnShowErrorChanged(bool value) => Refresh();

    private bool Matches(LogEntry entry)
    {
        bool levelShown = entry.Level switch
        {
            LogLevel.Warning => ShowWarning,
            LogLevel.Error => ShowError,
            _ => ShowInfo,
        };
        if (!levelShown) return false;
        string filter = SearchText.Trim();
        return filter.Length == 0 || entry.Text.Contains(filter, StringComparison.OrdinalIgnoreCase);
    }

    private void Refresh()
    {
        FilteredEntries.Clear();
        FilteredEntries.AddRange(Entries.Where(Matches));
        UpdateStatus();
    }

    private void UpdateStatus()
    {
        StatusText = FilteredEntries.Count == Entries.Count
            ? $"{Entries.Count:N0} entries"
            : $"{FilteredEntries.Count:N0} of {Entries.Count:N0} entries";
    }

    [RelayCommand]
    private void Clear()
    {
        Entries.Clear();
        FilteredEntries.Clear();
        InfoCount = 0;
        WarningCount = 0;
        ErrorCount = 0;
        UpdateStatus();
    }
}
