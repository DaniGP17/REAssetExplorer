using System.Globalization;
using System.Windows;
using System.Windows.Data;

namespace REAssetExplorer.UI.Helpers;

public class ReferenceEqualityConverter : IValueConverter
{
    public object? Convert(object value, Type targetType, object parameter, CultureInfo culture)
    {
        // value = SelectedGame
        // parameter = RadioButton (self)

        var radio = parameter as FrameworkElement;
        var radioItem = radio?.DataContext;

        return Equals(value, radioItem);
    }

    public object? ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
    {
        // parameter = RadioButton (self)
        var radio = parameter as FrameworkElement;
        var radioItem = radio?.DataContext;

        return (bool)value ? radioItem : Binding.DoNothing;
    }
}