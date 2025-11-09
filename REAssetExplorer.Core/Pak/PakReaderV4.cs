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