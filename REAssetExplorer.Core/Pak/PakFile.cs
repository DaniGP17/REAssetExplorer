namespace REAssetExplorer.Core.Pak;

/// <summary>
/// Represents a loaded PAK archive file.
/// </summary>
public class PakFile
{
    /// <summary>
    /// Gets the file name without the path.
    /// </summary>
    public string Name => System.IO.Path.GetFileName(Path);
    
    /// <summary>
    /// Gets the full path to the PAK file.
    /// </summary>
    public string Path { get; }
    
    /// <summary>
    /// Gets or sets the PAK file header.
    /// </summary>
    public PakHeader Header { get; set; }
    
    /// <summary>
    /// Gets the list of entries in this PAK file.
    /// </summary>
    public IList<PakEntry> Entries { get; }

    public PakFile(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        
        Path = path;
        Entries = new List<PakEntry>();
        Header = new PakHeader();
    }
}
