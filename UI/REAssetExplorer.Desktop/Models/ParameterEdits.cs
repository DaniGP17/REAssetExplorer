using System.Globalization;

namespace REAssetExplorer.Desktop.Models;

public sealed class ParameterEdits
{
    private readonly Dictionary<string, float[]> values = new(StringComparer.Ordinal);

    // Key and the values now in effect (the originals after a reset).
    public event Action<string, float[]>? Changed;

    public bool TryGet(string key, out float[] current) => values.TryGetValue(key, out current!);

    public void Set(string key, float[] current)
    {
        values[key] = current;
        Changed?.Invoke(key, current);
    }

    // Records a value the viewport already shows.
    public void Store(string key, float[] current) => values[key] = current;

    public void Reset(string key, float[] original)
    {
        if (values.Remove(key)) Changed?.Invoke(key, original);
    }

    public void Clear() => values.Clear();

    public IEnumerable<KeyValuePair<string, float[]>> Entries => values;

    public string ToNativeText() => string.Join('\n', values.Select(p =>
        p.Key + '\t' + string.Join(',', p.Value.Select(v => v.ToString("R", CultureInfo.InvariantCulture)))));
}
