using System.Globalization;
using System.Windows.Data;
using System.Windows.Media;

namespace REAssetExplorer.UI.Converters;

/// <summary>
/// Converts file properties to icon colors based on file type.
/// </summary>
public class FileIconColorConverter : IMultiValueConverter
{
    private static readonly Dictionary<string, string> IconColorMap = new(StringComparer.OrdinalIgnoreCase)
    {
        // Folders
        ["Folder32"] = "#FFD700",
        
        // File types by icon
        ["Image16"] = "#00CED1",
        ["CircleImage16"] = "#FF69B4",
        ["SelectObject24"] = "#32CD32",
        ["PersonWalking16"] = "#9370DB",
        ["HeadphonesSoundWave20"] = "#FFA500",
        
        // Default
        ["DEFAULT"] = "#FFFFFF"
    };
    
    public object Convert(object[] values, Type targetType, object parameter, CultureInfo culture)
    {
        if (values.Length >= 2 && values[0] is string icon && values[1] is bool isFolder)
        {
            if (IconColorMap.TryGetValue(icon, out var colorHex))
            {
                return new SolidColorBrush((Color)ColorConverter.ConvertFromString(colorHex));
            }
        }
        
        return new SolidColorBrush((Color)ColorConverter.ConvertFromString(IconColorMap["DEFAULT"]));
    }

    public object[] ConvertBack(object value, Type[] targetTypes, object parameter, CultureInfo culture)
    {
        throw new NotImplementedException();
    }
}
