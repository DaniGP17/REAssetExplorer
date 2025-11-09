namespace REAssetExplorer.Core.Pak;

/// <summary>
/// Represents the header of a PAK file.
/// </summary>
public class PakHeader
{
    /// <summary>
    /// Magic number identifying the file format.
    /// </summary>
    public uint Magic { get; set; }
    
    /// <summary>
    /// Version of the PAK format.
    /// </summary>
    public uint Version { get; set; }
    
    /// <summary>
    /// Number of entries in the PAK file.
    /// </summary>
    public int EntryCount { get; set; }
    
    /// <summary>
    /// Checksum for validation.
    /// </summary>
    public uint CheckSum { get; set; }
}