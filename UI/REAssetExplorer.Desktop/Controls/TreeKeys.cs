using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Threading;
using REAssetExplorer.Desktop.Models;

namespace REAssetExplorer.Desktop.Controls;

public static class TreeKeys
{
    public static void Attach<T>(ListBox list, Func<T, bool> hasChildren, Func<T, T?> firstChild, Func<T, T?> parent,
                                 Action<T> expand, Action<T> collapse) where T : class, ITreeNode
    {
        // Tunnel: the list's scroll viewer would otherwise consume Left/Right.
        list.AddHandler(InputElement.KeyDownEvent, (_, e) =>
        {
            if (list.SelectedItem is not T node) return;
            switch (e.Key)
            {
                case Key.Right when hasChildren(node) && !node.IsExpanded:
                    expand(node);
                    break;
                case Key.Right when hasChildren(node) && firstChild(node) is { } child:
                    Select(list, child);
                    break;
                case Key.Left when node.IsExpanded:
                    collapse(node);
                    break;
                case Key.Left when parent(node) is { } up:
                    Select(list, up);
                    break;
                case Key.Enter when hasChildren(node):
                    if (node.IsExpanded) collapse(node);
                    else expand(node);
                    break;
                default:
                    return;
            }
            e.Handled = true;
        }, Avalonia.Interactivity.RoutingStrategies.Tunnel);
    }

    // Focus must follow the selection or Up/Down continue from the old row.
    private static void Select(ListBox list, object item)
    {
        list.SelectedItem = item;
        list.ScrollIntoView(item);
        Dispatcher.UIThread.Post(() => list.ContainerFromItem(item)?.Focus(NavigationMethod.Directional),
                                 DispatcherPriority.Loaded);
    }
}
