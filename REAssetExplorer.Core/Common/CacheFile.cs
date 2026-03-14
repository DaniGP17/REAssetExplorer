using System.IO;

namespace REAssetExplorer.Core.Common;

/// <summary>
/// Base class for cache files that can be serialized and deserialized.
/// </summary>
public abstract class CacheFile
{
    /// <summary>
    /// Reads the cache data from a byte array.
    /// </summary>
    /// <param name="data">The byte array containing the cache data.</param>
    public abstract void ReadBytes(byte[] data);
    
    /// <summary>
    /// Writes the cache data to a stream.
    /// </summary>
    /// <param name="stream">The stream to write the cache data to.</param>
    public abstract void WriteBytes(Stream stream);

    /// <summary>
    /// Loads a cache file from disk.
    /// </summary>
    /// <typeparam name="C">The type of cache file to load.</typeparam>
    /// <param name="path">The path to the cache file.</param>
    /// <returns>The loaded cache file, or null if the file doesn't exist.</returns>
    public static C? LoadFromFile<C>(string path)
        where C : CacheFile, new()
    {
        if (!File.Exists(path))
            return null;

        byte[] fileData = File.ReadAllBytes(path);

        C cacheFile = new C();
        cacheFile.ReadBytes(fileData);

        return cacheFile;
    }
    
    /// <summary>
    /// Saves a cache file to disk.
    /// </summary>
    /// <param name="path">The path where the cache file will be saved.</param>
    public void SaveToFile(string path)
    {
        string? directory = Path.GetDirectoryName(path);
        if (!string.IsNullOrEmpty(directory) && !Directory.Exists(directory))
        {
            Directory.CreateDirectory(directory);
        }

        using var fs = new FileStream(path, FileMode.Create, FileAccess.Write);
        WriteBytes(fs);
    }
}
