using System.Reflection;
using System.Text;

namespace REAssetExplorer.Testing;

/// <summary>
/// Extension methods for debugging and printing objects.
/// </summary>
public static class DebugExtensions
{
    /// <summary>
    /// Prints all properties and fields of an object with their values.
    /// </summary>
    public static void Print(this object? obj, string title = "", int indent = 0, int maxDepth = 3)
    {
        if (obj == null)
        {
            Console.WriteLine($"{GetIndent(indent)}[null]");
            return;
        }

        if (!string.IsNullOrEmpty(title))
        {
            Console.WriteLine($"{GetIndent(indent)}{title}:");
            indent++;
        }

        var type = obj.GetType();

        if (type.IsPrimitive || type == typeof(string) || type == typeof(decimal))
        {
            Console.WriteLine($"{GetIndent(indent)}{obj}");
            return;
        }

        if (obj is byte[] byteArray)
        {
            var preview = byteArray.Length <= 4 
                ? string.Join(" ", byteArray.Select(b => b.ToString("X2")))
                : $"{string.Join(" ", byteArray.Take(4).Select(b => b.ToString("X2")))}... ({byteArray.Length} bytes)";
            
            // Try to interpret as ASCII if it looks like text
            var ascii = TryGetAsciiString(byteArray);
            if (!string.IsNullOrEmpty(ascii))
            {
                Console.WriteLine($"{GetIndent(indent)}[{preview}] \"{ascii}\"");
            }
            else
            {
                Console.WriteLine($"{GetIndent(indent)}[{preview}]");
            }
            return;
        }

        var properties = type.GetProperties(BindingFlags.Public | BindingFlags.Instance)
            .Where(p => p.CanRead);

        foreach (var prop in properties)
        {
            try
            {
                var value = prop.GetValue(obj);
                PrintPropertyOrField(prop.Name, value, prop.PropertyType, indent, maxDepth);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"{GetIndent(indent)}{prop.Name}: [Error: {ex.Message}]");
            }
        }

        var fields = type.GetFields(BindingFlags.Public | BindingFlags.Instance);

        foreach (var field in fields)
        {
            try
            {
                var value = field.GetValue(obj);
                PrintPropertyOrField(field.Name, value, field.FieldType, indent, maxDepth);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"{GetIndent(indent)}{field.Name}: [Error: {ex.Message}]");
            }
        }
    }

    private static void PrintPropertyOrField(string name, object? value, Type type, int indent, int maxDepth)
    {
        if (value == null)
        {
            Console.WriteLine($"{GetIndent(indent)}{name}: [null]");
            return;
        }

        if (value is System.Collections.IEnumerable enumerable && type != typeof(string) && type != typeof(byte[]))
        {
            var items = enumerable.Cast<object>().ToList();
            Console.WriteLine($"{GetIndent(indent)}{name}: [{items.Count} items]");
            
            if (maxDepth > 0 && items.Count > 0)
            {
                int index = 0;
                foreach (var item in items.Take(10))
                {
                    var itemType = item?.GetType();
                    
                    if (item != null && !IsSimpleType(itemType!))
                    {
                        Console.WriteLine($"{GetIndent(indent + 1)}[{index}]:");
                        item.Print(indent: indent + 2, maxDepth: maxDepth - 1);
                    }
                    else
                    {
                        Console.WriteLine($"{GetIndent(indent + 1)}[{index}]: {FormatValue(item, itemType!)}");
                    }
                    index++;
                }
                
                if (items.Count > 10)
                {
                    Console.WriteLine($"{GetIndent(indent + 1)}... and {items.Count - 10} more");
                }
            }
        }
        // Check if it's a complex object that should be expanded
        else if (!IsSimpleType(type) && maxDepth > 0)
        {
            Console.WriteLine($"{GetIndent(indent)}{name}:");
            value.Print(indent: indent + 1, maxDepth: maxDepth - 1);
        }
        else
        {
            var formattedValue = FormatValue(value, type);
            Console.WriteLine($"{GetIndent(indent)}{name}: {formattedValue}");
        }
    }

    private static bool IsSimpleType(Type type)
    {
        return type.IsPrimitive 
            || type.IsEnum
            || type == typeof(string) 
            || type == typeof(decimal)
            || type == typeof(DateTime)
            || type == typeof(DateTimeOffset)
            || type == typeof(TimeSpan)
            || type == typeof(Guid)
            || type == typeof(byte[]);
    }

    private static string FormatValue(object? value, Type type)
    {
        if (value == null)
            return "[null]";

        if (value is byte[] bytes)
        {
            if (bytes.Length == 0)
                return "[]";
            
            var preview = bytes.Length <= 4
                ? $"[{string.Join(" ", bytes.Select(b => $"{b:X2}"))}]"
                : $"[{string.Join(" ", bytes.Take(4).Select(b => $"{b:X2}"))}... ({bytes.Length} bytes)]";
            
            var ascii = TryGetAsciiString(bytes);
            if (!string.IsNullOrEmpty(ascii))
                return $"{preview} \"{ascii}\"";
            
            return preview;
        }

        if (type == typeof(uint))
        {
            var numValue = (uint)value;
            return $"{numValue} (0x{numValue:X})";
        }
        
        if (type == typeof(ulong))
        {
            var numValue = (ulong)value;
            return $"{numValue} (0x{numValue:X})";
        }
        
        if (type == typeof(ushort))
        {
            var numValue = (ushort)value;
            return $"{numValue} (0x{numValue:X})";
        }
        
        if (type == typeof(byte))
        {
            var numValue = (byte)value;
            return $"{numValue} (0x{numValue:X2})";
        }

        if (type == typeof(int))
        {
            var numValue = (int)value;
            return $"{numValue} (0x{numValue:X})";
        }
        
        if (type == typeof(long))
        {
            var numValue = (long)value;
            return $"{numValue} (0x{numValue:X})";
        }
        
        if (type == typeof(short))
        {
            var numValue = (short)value;
            return $"{numValue} (0x{numValue:X})";
        }
        
        if (type == typeof(sbyte))
        {
            var numValue = (sbyte)value;
            return $"{numValue} (0x{numValue:X2})";
        }

        if (type.IsEnum)
            return $"{value} ({Convert.ToInt32(value)})";

        if (type == typeof(float) || type == typeof(double))
            return string.Format("{0:F3}", value);

        if (value is System.Collections.IEnumerable enumerable && type != typeof(string))
        {
            var items = enumerable.Cast<object>().Take(5).ToList();
            if (items.Count == 0)
                return "[]";
            
            var preview = string.Join(", ", items);
            var count = enumerable.Cast<object>().Count();
            if (count > 5)
                preview += $"... ({count} total)";
            
            return $"[{preview}]";
        }

        return value.ToString() ?? "[no string representation]";
    }

    private static string? TryGetAsciiString(byte[] bytes)
    {
        if (bytes.Length == 0 || bytes.Length > 32)
            return null;

        // Check if bytes are printable ASCII
        if (!bytes.All(b => (b >= 32 && b <= 126) || b == 0))
            return null;

        var str = Encoding.ASCII.GetString(bytes).TrimEnd('\0');
        return str.Length > 0 && str.All(c => !char.IsControl(c)) ? str : null;
    }

    private static string GetIndent(int level)
    {
        return new string(' ', level * 2);
    }

    /// <summary>
    /// Prints an object with a colored title.
    /// </summary>
    public static void PrintColored(this object? obj, string title, ConsoleColor titleColor = ConsoleColor.Cyan, int maxDepth = 3)
    {
        var originalColor = Console.ForegroundColor;
        Console.ForegroundColor = titleColor;
        Console.WriteLine($"\n{title}");
        Console.WriteLine(new string('─', title.Length));
        Console.ForegroundColor = originalColor;
        obj.Print(indent: 0, maxDepth: maxDepth);
    }
}
