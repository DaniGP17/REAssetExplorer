using REAssetExplorer.Core.Common;

namespace REAssetExplorer.Core.Assets;

/// <summary>
/// Default implementation of the asset reader registry.
/// </summary>
public class AssetReaderRegistry : IAssetReaderRegistry
{
    private readonly Dictionary<Type, List<object>> _readers = new();

    /// <inheritdoc/>
    public void RegisterReader<T>(IAssetReader<T> reader) where T : class
    {
        ArgumentNullException.ThrowIfNull(reader);

        var type = typeof(T);
        if (!_readers.ContainsKey(type))
        {
            _readers[type] = new List<object>();
        }

        _readers[type].Add(reader);
    }

    /// <inheritdoc/>
    public IAssetReader<T>? GetReader<T>(string fileName) where T : class
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(fileName);

        var type = typeof(T);
        if (!_readers.TryGetValue(type, out var readers))
            return null;

        var lowerFileName = fileName.ToLowerInvariant();

        // Try to find a reader that supports this file
        foreach (var reader in readers.Cast<IAssetReader<T>>())
        {
            // Check each supported extension
            foreach (var supportedExt in reader.SupportedExtensions)
            {
                var lowerExt = supportedExt.ToLowerInvariant();
                
                if (lowerExt.Contains('*'))
                {
                    var pattern = lowerExt.Replace(".", "\\.").Replace("*", ".*");
                    if (System.Text.RegularExpressions.Regex.IsMatch(lowerFileName, pattern + "$"))
                    {
                        return reader;
                    }
                }
                else
                {
                    if (lowerFileName.EndsWith(lowerExt))
                    {
                        return reader;
                    }
                }
            }
        }

        return null;
    }

    /// <inheritdoc/>
    public bool TryRead<T>(string fileName, ReadOnlySpan<byte> data, out T? result) where T : class
    {
        result = null;

        var reader = GetReader<T>(fileName);
        if (reader == null)
            return false;

        if (!reader.CanRead(fileName, data.Length >= 16 ? data[..16] : data))
            return false;

        var readResult = reader.Read(data, fileName);
        if (!readResult.IsSuccess)
            return false;

        result = readResult.Value;
        return true;
    }

    /// <inheritdoc/>
    public Result<T> Read<T>(string fileName, ReadOnlySpan<byte> data) where T : class
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(fileName);

        var reader = GetReader<T>(fileName);
        if (reader == null)
            return Result<T>.Failure($"No reader found for asset type {typeof(T).Name} with file: {fileName}");

        var header = data.Length >= 16 ? data[..16] : data;
        if (!reader.CanRead(fileName, header))
            return Result<T>.Failure($"Reader for {typeof(T).Name} cannot read file: {fileName}");

        return reader.Read(data, fileName);
    }
}
