using System.Windows;
using System.Windows.Media;

namespace REAssetExplorer.TestUI;

internal static class VisualTreeExtensions
{
    /// <summary>
    /// Devuelve el primer ancestro (o el propio elemento) de tipo <typeparamref name="T"/>,
    /// sin superar <paramref name="stopAt"/>.
    /// </summary>
    public static T? AncestorOrSelf<T>(this DependencyObject element, DependencyObject? stopAt = null)
        where T : DependencyObject
    {
        var current = element;
        while (current is not null && current != stopAt)
        {
            if (current is T match)
                return match;
            current = VisualTreeHelper.GetParent(current);
        }
        return null;
    }
}
