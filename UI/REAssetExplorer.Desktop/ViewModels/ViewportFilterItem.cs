using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;

namespace REAssetExplorer.Desktop.ViewModels;

public sealed partial class ViewportFilterItem(string name, int count, Color color, string? tip, bool shown,
                                               Action<ViewportFilterItem> changed) : ObservableObject
{
    public string Name { get; } = name;

    public int Count { get; } = count;

    public IBrush Brush { get; } = new SolidColorBrush(color);

    public string? Tip { get; } = tip;

    [ObservableProperty]
    private bool isShown = shown;

    partial void OnIsShownChanged(bool value) => changed(this);
}
