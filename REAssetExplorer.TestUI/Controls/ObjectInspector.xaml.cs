using System;
using System.Globalization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;

namespace REAssetExplorer.TestUI.Controls;

[ValueConversion(typeof(bool), typeof(Visibility))]
public sealed class InverseBoolToVisibilityConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        => value is true ? Visibility.Collapsed : Visibility.Visible;

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        => throw new NotSupportedException();
}

public partial class ObjectInspector : UserControl
{
    public ObjectInspector() => InitializeComponent();

    public void ShowNode(HierarchyNode node) =>
        DataContext = InspectorViewModel.FromNode(node);

    public void Clear() =>
        DataContext = null;
}
