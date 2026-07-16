namespace REAssetExplorer.Desktop.Models;

public sealed class AssetOverrides
{
    private readonly Dictionary<string, string> values = new(StringComparer.Ordinal);

    public event Action? Changed;

    public bool TryGet(string key, out string pakPath) => values.TryGetValue(key, out pakPath!);

    public bool Contains(string key) => values.ContainsKey(key);

    public void Set(string key, string pakPath)
    {
        if (values.TryGetValue(key, out string? current) && current == pakPath) return;
        values[key] = pakPath;
        Changed?.Invoke();
    }

    public void Remove(string key)
    {
        if (values.Remove(key)) Changed?.Invoke();
    }

    // No Changed: a new document starts clean, nothing to reload.
    public void Clear() => values.Clear();

    public string ToNativeText() => string.Join('\n', values.Select(p => p.Key + '\t' + p.Value));
}
