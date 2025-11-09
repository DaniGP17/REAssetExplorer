namespace REAssetExplorer.Core.Pak;

/// <summary>
/// Interface for reading PAK archive files.
/// </summary>
public interface IPakReader
{
    /// <summary>
    /// Opens and reads a PAK file.
    /// </summary>
    /// <param name="path">Path to the PAK file.</param>
    /// <param name="fileList">File list for resolving hashed file names.</param>
    /// <returns>The loaded PAK file with all entries.</returns>
    PakFile Open(string path, PakFileList fileList);
    
    /// <summary>
    /// Lists all entries in a PAK file.
    /// </summary>
    /// <param name="file">The PAK file to enumerate.</param>
    /// <returns>Collection of PAK entries.</returns>
    IEnumerable<PakEntry> ListEntries(PakFile file);
}