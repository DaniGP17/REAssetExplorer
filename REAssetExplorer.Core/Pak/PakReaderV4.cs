using ZstdSharp;

namespace REAssetExplorer.Core.Pak;

/// <summary>
/// PAK reader implementation for version 4 format.
/// </summary>
public class PakReaderV4 : BasePakReader
{
    private const int FlagsSize = 8;

    public override PakFile Open(string path, PakFileList fileList)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        ArgumentNullException.ThrowIfNull(fileList);

        if (!File.Exists(path))
        {
            throw new FileNotFoundException($"PAK file not found: {path}");
        }

        using var fs = File.OpenRead(path);
        using var br = new BinaryReader(fs);

        var pak = new PakFile(path)
        {
            Header = ReadHeader(br)
        };

        ReadEntries(br, pak, fileList);

        return pak;
    }

    /// <summary>
    /// Extracts the raw data of a file entry from the PAK file.
    /// </summary>
    /// <param name="pakFile">The PAK file containing the entry.</param>
    /// <param name="entry">The entry to extract.</param>
    /// <returns>The raw file data (uncompressed if necessary).</returns>
    public override byte[] ExtractFile(PakFile pakFile, PakEntry entry)
    {
        ArgumentNullException.ThrowIfNull(pakFile);
        
        if (!File.Exists(pakFile.Path))
        {
            throw new FileNotFoundException($"PAK file not found: {pakFile.Path}");
        }

        using var fs = File.OpenRead(pakFile.Path);
        using var br = new BinaryReader(fs);

        // Seek to the file data
        fs.Seek(entry.Offset, SeekOrigin.Begin);

        byte[] data = br.ReadBytes((int)entry.CompressedSize);

        // If compressed, decompress it
        if (entry.IsCompressed)
        {
            data = DecompressData(data, entry);
        }

        return data;
    }

    /// <summary>
    /// Decompresses data based on the compression type in the entry flags.
    /// </summary>
    private byte[] DecompressData(byte[] compressedData, PakEntry entry)
    {
        var compressionType = entry.CompressionType;

        return compressionType switch
        {
            CompressionType.Uncompressed => compressedData,
            CompressionType.Deflated => DecompressDeflate(compressedData, (int)entry.UncompressedSize),
            CompressionType.ZStandard => DecompressZStandard(compressedData, (int)entry.UncompressedSize),
            _ => throw new NotSupportedException($"Compression type {compressionType} is not supported.")
        };
    }

    /// <summary>
    /// Decompresses data using Deflate algorithm.
    /// </summary>
    private byte[] DecompressDeflate(byte[] compressedData, int uncompressedSize)
    {
        using var compressedStream = new MemoryStream(compressedData);
        using var deflateStream = new System.IO.Compression.DeflateStream(compressedStream, System.IO.Compression.CompressionMode.Decompress);
        using var decompressedStream = new MemoryStream(uncompressedSize);
        
        deflateStream.CopyTo(decompressedStream);
        return decompressedStream.ToArray();
    }

    /// <summary>
    /// Decompresses data using ZStandard algorithm.
    /// </summary>
    private byte[] DecompressZStandard(byte[] compressedData, int uncompressedSize)
    {
        using var decompressor = new Decompressor();
        return decompressor.Unwrap(compressedData).ToArray();
    }

    private static PakHeader ReadHeader(BinaryReader reader)
    {
        return new PakHeader
        {
            Magic = reader.ReadUInt32(),
            Version = reader.ReadUInt32(),
            EntryCount = reader.ReadInt32(),
            CheckSum = reader.ReadUInt32()
        };
    }

    private static void ReadEntries(BinaryReader reader, PakFile pak, PakFileList fileList)
    {
        for (int i = 0; i < pak.Header.EntryCount; i++)
        {
            var entry = ReadEntry(reader, fileList);
            pak.Entries.Add(entry);
        }
    }

    private static PakEntry ReadEntry(BinaryReader reader, PakFileList fileList)
    {
        PakEntry entry = default;

        entry.LowerCaseHash = reader.ReadUInt32();
        entry.UpperCaseHash = reader.ReadUInt32();
        entry.Offset = reader.ReadInt64();
        entry.CompressedSize = reader.ReadInt64();
        entry.UncompressedSize = reader.ReadInt64();

        entry.FilePath = ResolveFilePath(entry.LowerCaseHash, fileList);

        ReadFlags(reader, ref entry);

        entry.Checksum = reader.ReadInt64();

        return entry;
    }

    private static string ResolveFilePath(uint hash, PakFileList fileList)
    {
        return fileList.GetByLowerHash(hash)?.Path 
               ?? $"Unknown_{hash:X8}";
    }

    private static void ReadFlags(BinaryReader reader, ref PakEntry entry)
    {
        var flags = reader.ReadBytes(FlagsSize);

        unsafe
        {
            fixed (byte* src = flags)
            {
                for (int j = 0; j < FlagsSize; j++)
                {
                    entry.Flags[j] = src[j];
                }
            }
        }
    }
}