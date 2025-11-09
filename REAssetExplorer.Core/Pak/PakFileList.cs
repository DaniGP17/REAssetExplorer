using REAssetExplorer.Core.Hashing;

namespace REAssetExplorer.Core.Pak;

/// <summary>
/// Represents a file path entry with its hash values.
/// </summary>
/// <param name="LowerCaseHash">Hash of the lowercase path.</param>
/// <param name="UpperCaseHash">Hash of the uppercase path.</param>
/// <param name="Path">The original file path.</param>
public record HashListEntry(uint LowerCaseHash, uint UpperCaseHash, string Path);

/// <summary>
/// Manages a list of file paths and their hash mappings for PAK file resolution.
/// </summary>
public class PakFileList
{
    private readonly Dictionary<uint, HashListEntry> _hashDict = new();
    
    /// <summary>
    /// Loads file paths from a text file and builds hash mappings.
    /// </summary>
    /// <param name="filePath">Path to the file containing one file path per line.</param>
    /// <exception cref="FileNotFoundException">Thrown when the file is not found.</exception>
    public void SetupFromFile(string filePath)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(filePath);
        
        if (!File.Exists(filePath))
        {
            throw new FileNotFoundException($"File list not found: {filePath}");
        }

        _hashDict.Clear();
        
        var lines = File.ReadAllLines(filePath)
            .Where(line => !string.IsNullOrWhiteSpace(line));

        foreach (var line in lines)
        {
            var entry = CreateHashListEntry(line);
            _hashDict[entry.LowerCaseHash] = entry;
        }
    }

    private static HashListEntry CreateHashListEntry(string path)
    {
        var lowerHash = Murmur3.ConvertFilePathToMurmurHash(path, lower: true, changeCase: true, keepNullTerminator: false);
        var upperHash = Murmur3.ConvertFilePathToMurmurHash(path, lower: false, changeCase: true, keepNullTerminator: false);
        
        return new HashListEntry(lowerHash, upperHash, path);
    }
    
    /// <summary>
    /// Checks if a lowercase hash exists in the list.
    /// </summary>
    public bool ContainsLowerHash(uint hash) => _hashDict.ContainsKey(hash);
    
    /// <summary>
    /// Retrieves an entry by its lowercase hash.
    /// </summary>
    /// <param name="hash">The lowercase hash to look up.</param>
    /// <returns>The matching entry or null if not found.</returns>
    public HashListEntry? GetByLowerHash(uint hash)
    {
        return _hashDict.TryGetValue(hash, out var entry) ? entry : null;
    }
    
    /// <summary>
    /// Gets the total number of entries in the list.
    /// </summary>
    public int Count => _hashDict.Count;
}