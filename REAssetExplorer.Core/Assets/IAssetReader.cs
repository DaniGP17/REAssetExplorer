using REAssetExplorer.Core.Common;

namespace REAssetExplorer.Core.Assets;

/// <summary>
/// Interface for reading and parsing game assets from binary data.
/// </summary>
/// <typeparam name="T">The type of asset this reader produces.</typeparam>
public interface IAssetReader<T> where T : class
{
    /// <summary>
    /// Gets the file extensions this reader can handle (e.g., ".mesh", ".tex").
    /// </summary>
    IReadOnlySet<string> SupportedExtensions { get; }
    
    /// <summary>
    /// Gets the asset type this reader handles.
    /// </summary>
    AssetType AssetType { get; }
    
    /// <summary>
    /// Determines if this reader can process the given file.
    /// </summary>
    /// <param name="fileName">The name of the file.</param>
    /// <param name="header">The first bytes of the file for magic number checking.</param>
    /// <returns>True if this reader can handle the file, false otherwise.</returns>
    bool CanRead(string fileName, ReadOnlySpan<byte> header);
    
    /// <summary>
    /// Reads and parses the asset from binary data.
    /// </summary>
    /// <param name="data">The raw binary data of the asset.</param>
    /// <param name="fileName">The name of the file being read.</param>
    /// <returns>A result containing the parsed asset or an error message.</returns>
    Result<T> Read(ReadOnlySpan<byte> data, string fileName);
}
