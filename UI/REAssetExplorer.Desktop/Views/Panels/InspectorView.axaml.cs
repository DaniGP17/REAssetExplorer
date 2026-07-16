using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.VisualTree;
using REAssetExplorer.Desktop.Models;
using REAssetExplorer.Desktop.ViewModels;

namespace REAssetExplorer.Desktop.Views.Panels;

public partial class InspectorView : UserControl
{
    private Flyout? openPicker;

    public InspectorView()
    {
        InitializeComponent();
    }

    private void OnSectionTapped(object? sender, TappedEventArgs e)
    {
        if (sender is Control { DataContext: InspectorSection section }) section.IsExpanded = !section.IsExpanded;
    }

    private void OnAssetThumbnailDoubleTapped(object? sender, TappedEventArgs e)
    {
        if (sender is Control { DataContext: AssetReference reference } && reference.OpenCommand.CanExecute(null))
        {
            reference.OpenCommand.Execute(null);
        }
    }

    private void OnAssetElementTapped(object? sender, TappedEventArgs e)
    {
        if (sender is Control { DataContext: AssetReference reference } && reference.OpenCommand.CanExecute(null))
        {
            reference.OpenCommand.Execute(null);
        }
    }

    private void OnPickerOpening(object? sender, EventArgs e)
    {
        openPicker = sender as Flyout;
        if (openPicker?.Target?.DataContext is AssetReference reference) reference.RefreshPicker();
    }

    private void OnPickerItemTapped(object? sender, TappedEventArgs e)
    {
        if ((e.Source as Visual)?.FindAncestorOfType<ListBoxItem>(includeSelf: true) != null) ChoosePicked(sender as ListBox);
    }

    private void OnPickerKeyDown(object? sender, KeyEventArgs e)
    {
        if (e.Key != Key.Enter) return;
        ChoosePicked(sender as ListBox);
        e.Handled = true;
    }

    private void ChoosePicked(ListBox? list)
    {
        if (list is not { SelectedItem: AssetNode node, DataContext: AssetReference reference }) return;
        openPicker?.Hide();
        reference.Choose(node);
    }
}
