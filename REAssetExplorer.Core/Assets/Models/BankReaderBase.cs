using REAssetExplorer.Core.Common;
using System.Text;

namespace REAssetExplorer.Core.Assets.Models;

public abstract class BankReaderBase : IAssetReader<BankData>
{
    /// <inheritdoc/>
    public abstract IReadOnlySet<string> SupportedExtensions { get; }

    /// <inheritdoc/>
    public AssetType AssetType => AssetType.Bank;

    /// <inheritdoc/>
    public abstract bool CanRead(string fileName, ReadOnlySpan<byte> header);

    /// <inheritdoc/>
    public Result<BankData> Read(ReadOnlySpan<byte> data, string fileName)
    {
        try
        {
            var bnk = new BankData
            {
                FirstChunkHeader = new BankChunkHeader(),
                Header = new BankHeader()
            };

            using var br = new BinaryReader(new MemoryStream(data.ToArray()));

            ReadFirstChunkHeader(br, bnk);
            ReadHeader(br, bnk);
            
            while (br.BaseStream.Position < br.BaseStream.Length)
            {
                var chunk = ReadChunkHeader(br);

                switch (chunk.Tag)
                {
                    case BankTags.DIDX:
                        ReadDIDX(br, chunk, bnk);
                        break;
                    case BankTags.DATA:
                        ReadDATA(br, chunk, bnk);
                        break;
                    case BankTags.HIRC:
                        ReadHIRC(br, chunk, bnk);
                        break;
                    case BankTags.STID:
                        ReadSTID(br, chunk, bnk);
                        break;
                    case BankTags.INIT:
                    case BankTags.STMG:
                    case BankTags.ENVS:
                    case BankTags.PLAT:
                        // We're going to ignore these chunks for now
                        br.BaseStream.Seek(chunk.Size, SeekOrigin.Current);
                        break;
                    default:
                        throw new NotSupportedException($"Unsupported chunk tag: {chunk.Tag}");
                }
            }
            
            LoadMediaData(br, bnk);

            return Result<BankData>.Success(bnk);
        }
        catch (Exception ex)
        {
            return Result<BankData>.Failure($"Failed to read bnk '{fileName}': {ex}");
        }
    }

    protected void LoadMediaData(BinaryReader br, BankData bnk)
    {
        if (bnk.MediaHeaders == null)
        {
            return;
        }
        
        foreach (var media in bnk.MediaHeaders)
        {
            long start = bnk.DataOffset + media.Offset;

            br.BaseStream.Seek(start, SeekOrigin.Begin);
            var wemData = br.ReadBytes((int)media.Size);
            bnk.MediaHeaders[Array.IndexOf(bnk.MediaHeaders, media)].Data = wemData;
        }
    }

    protected virtual void ReadFirstChunkHeader(BinaryReader br, BankData bnk)
    {
        bnk.FirstChunkHeader.Tag = (BankTags)br.ReadUInt32();
        bnk.FirstChunkHeader.Size = br.ReadUInt32();
    }
    
    protected virtual void ReadHeader(BinaryReader br, BankData bnk)
    {
        bnk.Header.Version = br.ReadUInt32();
        bnk.Header.BankID = br.ReadUInt32();
        bnk.Header.LanguageID = br.ReadUInt32();
        br.ReadBytes(2);
        bnk.Header.DeviceAllocated = br.ReadUInt16();
        bnk.Header.ProjectID = br.ReadUInt32();
        int remaining = checked((int)bnk.FirstChunkHeader.Size - 0x14);
        br.ReadBytes(remaining);
    }
    
    protected virtual BankChunkHeader ReadChunkHeader(BinaryReader br)
    {
        if (br.BaseStream.Position + 8 > br.BaseStream.Length)
            throw new EndOfStreamException();

        return new BankChunkHeader
        {
            Tag = (BankTags)br.ReadUInt32(),
            Size = br.ReadUInt32()
        };
    }
    
    protected virtual void ReadDIDX(BinaryReader br, BankChunkHeader chunk, BankData bnk)
    {
        if (chunk.Size % 12 != 0)
            throw new InvalidDataException("Invalid DIDX chunk size");

        int count = (int)(chunk.Size / 12);
        bnk.MediaHeaders = new MediaHeader[count];

        for (int i = 0; i < count; i++)
        {
            bnk.MediaHeaders[i] = new MediaHeader
            {
                Id = br.ReadUInt32(),
                Offset = br.ReadUInt32(),
                Size = br.ReadUInt32()
            };
        }
    }

    protected virtual void ReadDATA(BinaryReader br, BankChunkHeader chunk, BankData bnk)
    {
        bnk.DataOffset = br.BaseStream.Position;
        bnk.BnkDataSize = chunk.Size;

        br.BaseStream.Seek(chunk.Size, SeekOrigin.Current);
    }
    
    protected virtual void ReadHIRC(BinaryReader br, BankChunkHeader chunk, BankData bnk)
    {
        long start = br.BaseStream.Position;

        uint objectCount = br.ReadUInt32();
        bnk.HircObjects = new List<HircObject>((int)objectCount);

        for (int i = 0; i < objectCount; i++)
        {
            byte type = br.ReadByte();
            uint size = br.ReadUInt32();
            
            byte[] payload = br.ReadBytes((int)size);
            
            uint id = BitConverter.ToUInt32(payload, 0);

            bnk.HircObjects.Add(new HircObject
            {
                Type = (HircType)type,
                ID = id,
                Data = payload
            });
        }

        if (br.BaseStream.Position - start != chunk.Size)
            throw new InvalidDataException("HIRC size mismatch");
    }
    
    protected virtual void ReadSTID(BinaryReader br, BankChunkHeader chunk, BankData bnk)
    {
        long start = br.BaseStream.Position;

        uint count = br.ReadUInt32();
        uint flags = br.ReadUInt32();

        bnk.StringTable = new Dictionary<uint, string>((int)count);

        for (int i = 0; i < count; i++)
        {
            uint id = br.ReadUInt32();
            byte len = br.ReadByte();

            var nameBytes = br.ReadBytes(len);
            string name = Encoding.ASCII.GetString(nameBytes);

            bnk.StringTable[id] = name;
        }

        long read = br.BaseStream.Position - start;
        if (read != chunk.Size)
            br.BaseStream.Seek(start + chunk.Size, SeekOrigin.Begin);
    }

    public bool ResolveDependencies(BankData asset) => true;
}
