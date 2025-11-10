namespace REAssetExplorer.Core.Pak;

/// <summary>
/// Represents an entry (file) within a PAK archive.
/// </summary>
public unsafe struct PakEntry
{
    private const int FlagsLength = 8;
    
    /// <summary>
    /// The file path within the PAK archive.
    /// </summary>
    public string FilePath;
    
    /// <summary>
    /// Hash of the lowercase file path.
    /// </summary>
    public uint LowerCaseHash;
    
    /// <summary>
    /// Hash of the uppercase file path.
    /// </summary>
    public uint UpperCaseHash;
    
    /// <summary>
    /// Offset of the file data within the PAK file.
    /// </summary>
    public long Offset;
    
    /// <summary>
    /// Size of the compressed file data.
    /// </summary>
    public long CompressedSize;
    
    /// <summary>
    /// Size of the uncompressed file data.
    /// </summary>
    public long UncompressedSize;
    
    /// <summary>
    /// File flags (8 bytes).
    /// </summary>
    public fixed byte Flags[FlagsLength];
    
    /// <summary>
    /// Checksum value for data integrity.
    /// </summary>
    public long Checksum;
    
    /// <summary>
    /// Gets whether the file is compressed.
    /// </summary>
    public readonly bool IsCompressed => CompressedSize != UncompressedSize;
    
    /// <summary>
    /// Gets the compression type used for this entry.
    /// </summary>
    public readonly CompressionType CompressionType
    {
        get
        {
            fixed (byte* ptr = Flags)
            {
                var compressionFlag = ptr[0] & 0x0F;
                return compressionFlag switch
                {
                    0 => CompressionType.Uncompressed,
                    1 => CompressionType.Deflated,
                    2 => CompressionType.ZStandard,
                    _ => CompressionType.Unknown
                };
            }
        }
    }
}