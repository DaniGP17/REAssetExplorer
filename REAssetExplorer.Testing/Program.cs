using REAssetExplorer.Core.Pak;
using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Games.RE7.Assets;

var fileList = new PakFileList();
fileList.SetupFromFile("D:\\Projects\\REAssetExplorer\\FileLists\\re7.txt");

var pakReader = new PakReaderV4();
PakFile pakFile = pakReader.Open("D:\\SteamLibrary\\steamapps\\common\\RESIDENT EVIL 7 biohazard\\re_chunk_000.pak", fileList);

var textureEntry = pakFile.Entries.FirstOrDefault(e => e.FilePath.Contains(".tex.35"));
if (textureEntry.FilePath == null)
{
    Console.WriteLine("No texture found.");
    return;
}

Console.WriteLine($"Found texture: {textureEntry.FilePath}");

byte[] textureData = pakReader.ExtractFile(pakFile, textureEntry);
var result = new RE7TextureReader().Read(textureData, textureEntry.FilePath);

if (!result.IsSuccess)
{
    Console.WriteLine($"Error: {result.Error}");
    return;
}

var tex = result.Value;

if (tex == null)
{
    Console.WriteLine("Error: Failed to read texture data");
    return;
}

string outputPath = Path.Combine(
    Environment.GetFolderPath(Environment.SpecialFolder.UserProfile),
    "Downloads",
    Path.GetFileNameWithoutExtension(textureEntry.FilePath ?? "texture") + ".dds"
);

using (var bw = new BinaryWriter(File.Create(outputPath)))
{
    int headerSize = 0x28 + (tex.Mips.Length * 16);
    int availableData = textureData.Length - headerSize;
    bool isStreaming = (tex.StreamingFlags & 0x600) != 0;
    
    WriteDDSHeader(bw, tex, isStreaming ? 1 : tex.MipsPerImage);
    
    if (isStreaming)
    {
        bw.Write(textureData.AsSpan(headerSize, availableData));
        Console.WriteLine("Streaming texture, only 1 mip saved");
    }
    else
    {
        foreach (var mip in tex.Mips)
        {
            if ((int)mip.Offset + (int)mip.Size <= textureData.Length)
                bw.Write(textureData.AsSpan((int)mip.Offset, (int)mip.Size));
        }
    }
}

static void WriteDDSHeader(BinaryWriter bw, TextureData tex, int mipCount)
{
    bw.Write(0x20534444); // "DDS "
    bw.Write(124); // Header size
    
    uint flags = 0x1 | 0x2 | 0x4 | 0x1000; // CAPS | HEIGHT | WIDTH | PIXELFORMAT
    if (mipCount > 1) flags |= 0x20000; // MIPMAPCOUNT
    bw.Write(flags);
    
    bw.Write((uint)tex.Height);
    bw.Write((uint)tex.Width);
    bw.Write(0u); // Pitch
    bw.Write(0u); // Depth
    bw.Write((uint)mipCount);
    
    for (int i = 0; i < 11; i++) bw.Write(0u); // Reserved
    
    // PixelFormat
    bw.Write(32); // Size
    bw.Write(0x4); // Flags (FOURCC)
    bw.Write(0x30315844); // "DX10"
    for (int i = 0; i < 5; i++) bw.Write(0u); // RGB masks
    
    // Caps
    uint caps = 0x1000; // TEXTURE
    if (mipCount > 1) caps |= 0x8 | 0x400000; // COMPLEX | MIPMAP
    bw.Write(caps);
    for (int i = 0; i < 4; i++) bw.Write(0u); // Caps2-4, Reserved
    
    // DX10 Header
    uint dxgiFormat = tex.Format switch
    {
        TextureFormat.Bc1Unorm => 71,
        TextureFormat.Bc3Unorm => 77,
        TextureFormat.Bc5Unorm => 83,
        TextureFormat.Bc7Unorm => 98,
        _ => 98
    };
    bw.Write(dxgiFormat);
    bw.Write(3u); // TEXTURE2D
    bw.Write(0u); // miscFlag
    bw.Write((uint)tex.NumImages);
    bw.Write(0u); // miscFlags2
}