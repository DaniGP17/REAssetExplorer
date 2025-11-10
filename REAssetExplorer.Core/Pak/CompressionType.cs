namespace REAssetExplorer.Core.Pak;

/// <summary>
/// Represents the compression type used for a PAK entry.
/// </summary>
public enum CompressionType
{
    /// <summary>
    /// No compression is applied.
    /// </summary>
    Uncompressed = 0,
    
    /// <summary>
    /// Deflate compression algorithm.
    /// </summary>
    Deflated = 1,
    
    /// <summary>
    /// ZStandard compression algorithm.
    /// </summary>
    ZStandard = 2,
    
    /// <summary>
    /// Unknown or unsupported compression type.
    /// </summary>
    Unknown = 255
}
