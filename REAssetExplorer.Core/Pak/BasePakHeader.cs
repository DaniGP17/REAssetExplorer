namespace REAssetExplorer.Core.Pak;

/// <summary>
/// Base class for PAK file readers.
/// </summary>
public abstract class BasePakReader : IPakReader
{
    /// <summary>
    /// Opens and reads a PAK file.
    /// </summary>
    /// <param name="path">Path to the PAK file.</param>
    /// <param name="fileList">File list for hash resolution.</param>
    /// <returns>The loaded PAK file.</returns>
    public abstract PakFile Open(string path, PakFileList fileList);

    /// <summary>
    /// Lists all entries in the PAK file.
    /// </summary>
    /// <param name="file">The PAK file to list entries from.</param>
    /// <returns>Enumerable of PAK entries.</returns>
    public virtual IEnumerable<PakEntry> ListEntries(PakFile file)
    {
        ArgumentNullException.ThrowIfNull(file);
        return file.Entries;
    }
}