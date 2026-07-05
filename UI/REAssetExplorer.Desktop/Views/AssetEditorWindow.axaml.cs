using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.VisualTree;
using REAssetExplorer.Desktop.Models;
using REAssetExplorer.Desktop.ViewModels;

namespace REAssetExplorer.Desktop.Views;

public partial class AssetEditorWindow : Window
{
    private AssetEditorViewModel? editor;
    private bool closeRequested;
    private Flyout? openMotlistPicker;
    private Flyout? openPartPicker;

    public AssetEditorWindow()
    {
        InitializeComponent();
    }

    private void OnMotlistPickerOpening(object? sender, EventArgs e)
    {
        openMotlistPicker = sender as Flyout;
        if (openMotlistPicker?.Target?.DataContext is AnimationToolbarViewModel animation) animation.PreparePicker();
    }

    private void OnPartPickerOpening(object? sender, EventArgs e)
    {
        openPartPicker = sender as Flyout;
        if (openPartPicker?.Target?.DataContext is AnimationToolbarViewModel animation) animation.PreparePartPicker();
    }

    private void OnPartPickerTapped(object? sender, TappedEventArgs e)
    {
        if ((e.Source as Visual)?.FindAncestorOfType<ListBoxItem>(includeSelf: true) != null) ChoosePart(sender as ListBox);
    }

    private void OnPartPickerKeyDown(object? sender, KeyEventArgs e)
    {
        if (e.Key != Key.Enter) return;
        ChoosePart(sender as ListBox);
        e.Handled = true;
    }

    private void ChoosePart(ListBox? list)
    {
        if (list is not { SelectedItem: AssetNode node, DataContext: AnimationToolbarViewModel animation }) return;
        openPartPicker?.Hide();
        animation.AddPart(node);
    }

    private void OnMotlistPickerTapped(object? sender, TappedEventArgs e)
    {
        if ((e.Source as Visual)?.FindAncestorOfType<ListBoxItem>(includeSelf: true) != null) ChooseMotlist(sender as ListBox);
    }

    private void OnMotlistPickerKeyDown(object? sender, KeyEventArgs e)
    {
        if (e.Key != Key.Enter) return;
        ChooseMotlist(sender as ListBox);
        e.Handled = true;
    }

    private void ChooseMotlist(ListBox? list)
    {
        if (list is not { SelectedItem: AssetNode node, DataContext: AnimationToolbarViewModel animation }) return;
        openMotlistPicker?.Hide();
        _ = animation.ChooseAsync(node);
    }

    protected override void OnOpened(EventArgs e)
    {
        base.OnOpened(e);
        editor = DataContext as AssetEditorViewModel;
        if (editor != null) editor.CloseRequested += Close;
    }

    protected override void OnClosing(WindowClosingEventArgs e)
    {
        base.OnClosing(e);
        if (editor == null) return;
        if (closeRequested || editor.WhenIdle().IsCompleted)
        {
            editor.Viewport?.Park();
            return;
        }
        // A load in flight still uses the viewport; close once it finishes.
        e.Cancel = true;
        closeRequested = true;
        Hide();
        editor.WhenIdle().ContinueWith(_ => Close(), TaskScheduler.FromCurrentSynchronizationContext());
    }

    protected override void OnClosed(EventArgs e)
    {
        base.OnClosed(e);
        if (editor == null) return;
        editor.CloseRequested -= Close;
        editor.OnClosed();
    }

    private void OnMinimizeClick(object? sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;

    private void OnMaximizeClick(object? sender, RoutedEventArgs e) =>
        WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;

    private void OnCloseClick(object? sender, RoutedEventArgs e) => Close();

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property != WindowStateProperty || MaximizeIcon == null) return;
        bool maximized = WindowState == WindowState.Maximized;
        MaximizeIcon.IsVisible = !maximized;
        RestoreIcon.IsVisible = maximized;
    }
}
