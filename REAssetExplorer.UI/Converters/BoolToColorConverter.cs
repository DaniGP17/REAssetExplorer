using System;
using System.Globalization;
using System.Windows.Data;
using System.Windows.Media;

namespace REAssetExplorer.UI.Converters;

public class BoolToColorConverter : IValueConverter
{
    public string TrueColor { get; set; } = "#4CAF50";
    public string FalseColor { get; set; } = "#888888";

    public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
    {
        if (value is bool boolValue)
        {
            var colorString = boolValue ? TrueColor : FalseColor;
            return (SolidColorBrush)new BrushConverter().ConvertFrom(colorString)!;
        }
        return new SolidColorBrush(Colors.Gray);
    }

    public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
    {
        throw new NotImplementedException();
    }
}
