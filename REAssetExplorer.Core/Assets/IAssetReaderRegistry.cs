using REAssetExplorer.Core.Common;

namespace REAssetExplorer.Core.Assets;

/// <summary>
/// Registry for managing asset readers for a specific game.
/// </summary>
public interface IAssetReaderRegistry
{
    /// <summary>
    /// Registers an asset reader.
    /// </summary>
    /// <typeparam name="T">The type of asset the reader produces.</typeparam>
    /// <param name="reader">The reader to register.</param>
    void RegisterReader<T>(IAssetReader<T> reader) where T : class;
    
    /// <summary>
    /// Gets a reader for the specified asset type and file extension.
    /// </summary>
    /// <typeparam name="T">The type of asset to read.</typeparam>
    /// <param name="fileName">The name of the file to read.</param>
    /// <returns>A matching reader or null if none found.</returns>
    IAssetReader<T>? GetReader<T>(string fileName) where T : class;
    
    /// <summary>
    /// Attempts to read an asset from binary data.
    /// </summary>
    /// <typeparam name="T">The type of asset to read.</typeparam>
    /// <param name="fileName">The name of the file.</param>
    /// <param name="data">The binary data to read.</param>
    /// <param name="result">The parsed asset if successful.</param>
    /// <returns>True if the asset was successfully read, false otherwise.</returns>
    bool TryRead<T>(string fileName, ReadOnlySpan<byte> data, out T? result) where T : class;
    
    /// <summary>
    /// Reads an asset from binary data.
    /// </summary>
    /// <typeparam name="T">The type of asset to read.</typeparam>
    /// <param name="fileName">The name of the file.</param>
    /// <param name="data">The binary data to read.</param>
    /// <returns>A result containing the parsed asset or an error.</returns>
    Result<T> Read<T>(string fileName, ReadOnlySpan<byte> data) where T : class;
}
