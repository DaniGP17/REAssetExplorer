using Avalonia;
using Avalonia.Media;

namespace REAssetExplorer.Desktop.Models;

public static class Icons
{
    private static readonly Dictionary<string, Geometry?> Cache = new();

    public static Geometry? Get(string key)
    {
        if (Cache.TryGetValue(key, out Geometry? geometry)) return geometry;
        geometry = Application.Current?.TryGetResource(key, null, out object? value) == true ? value as Geometry : null;
        Cache[key] = geometry;
        return geometry;
    }
}
