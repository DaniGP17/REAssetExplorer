using System.IO;

namespace REAssetExplorer.Core.Common;

/// <summary>
/// Cache for storing material name to file path mappings.
/// </summary>
public class MaterialsCache : CacheFile
{
    private const int MAGIC = 0x4D41544C; // "MATL"
    private const int VERSION = 1;
    private readonly Dictionary<string, string> _materials = new();
    
    /// <summary>
    /// Gets the number of materials in the cache.
    /// </summary>
    public int Count => _materials.Count;
    
    /// <summary>
    /// Reads the cache data from a byte array.
    /// </summary>
    /// <param name="data">The byte array containing the cache data.</param>
    public override void ReadBytes(byte[] data)
    {
        _materials.Clear();
        
        using BinaryReader br = new(new MemoryStream(data));
        
        int magic = br.ReadInt32();
        if (magic != MAGIC)
            throw new InvalidDataException("Invalid magic number");
        
        int version = br.ReadInt32();
        if (version != VERSION)
            throw new InvalidDataException($"Unsupported version: {version}");
        
        int count = br.ReadInt32();
        if (count < 0)
            throw new InvalidDataException("Invalid material count");
        
        for (int i = 0; i < count; i++)
        {
            string key = br.ReadString();
            string value = br.ReadString();
            _materials[key] = value;
        }
    }

    /// <summary>
    /// Writes the cache data to a stream.
    /// </summary>
    /// <param name="stream">The stream to write the cache data to.</param>
    public override void WriteBytes(Stream stream)
    {
        using BinaryWriter bw = new(stream);
        bw.Write(MAGIC);
        bw.Write(VERSION);
        bw.Write(_materials.Count);
        foreach (var kvp in _materials)
        {
            bw.Write(kvp.Key);
            bw.Write(kvp.Value);
        }
        bw.Flush();
    }
    
    /// <summary>
    /// Adds or updates a material in the cache.
    /// </summary>
    /// <param name="name">The name of the material.</param>
    /// <param name="path">The file path of the material.</param>
    public void AddMaterial(string name, string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        
        _materials[name] = path;
    }
    
    /// <summary>
    /// Tries to get the file path for a material by name.
    /// </summary>
    /// <param name="name">The name of the material.</param>
    /// <param name="path">The file path if found.</param>
    /// <returns>True if the material was found, false otherwise.</returns>
    public bool TryGetMaterial(string name, out string? path)
    {
        return _materials.TryGetValue(name, out path);
    }
    
    /// <summary>
    /// Clears all materials from the cache.
    /// </summary>
    public void Clear()
    {
        _materials.Clear();
    }
}
