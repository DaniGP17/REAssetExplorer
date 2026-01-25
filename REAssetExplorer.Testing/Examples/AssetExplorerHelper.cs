using System.Diagnostics;
using REAssetExplorer.Core.Assets;
using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Core.Common;
using REAssetExplorer.Core.Pak;
using REAssetExplorer.Testing.TestHelpers;

namespace REAssetExplorer.Testing.Examples;

public static class AssetExplorerHelper
{
    public static PakFile LoadRE7Pak()
    {
        Console.WriteLine(" Loading RE7 PAK file...");
        
        var fileList = new PakFileList();
        fileList.SetupFromFile(TestDataPaths.RE7FileList);

        var pakReader = new PakReaderV4();
        var pakFile = pakReader.Open(TestDataPaths.RE7MainPak, fileList);
        
        Console.WriteLine($" PAK loaded: {pakFile.Entries.Count:N0} entries\n");
        
        return pakFile;
    }

    public static List<PakEntry> FindFilesByExtension(PakFile pakFile, string extension, int maxResults = 10)
    {
        return pakFile.Entries
            .Where(e => e.FilePath?.EndsWith(extension, StringComparison.OrdinalIgnoreCase) == true)
            .Take(maxResults)
            .ToList();
    }

    public static List<PakEntry> FindFilesByPattern(PakFile pakFile, string pattern, int maxResults = 10)
    {
        return pakFile.Entries
            .Where(e => e.FilePath?.Contains(pattern, StringComparison.OrdinalIgnoreCase) == true)
            .Take(maxResults)
            .ToList();
    }

    /// <summary>
    /// Finds and loads the first asset matching the given path pattern.
    /// </summary>
    public static Result<TData> LoadAssetByPattern<TReader, TData>(
        PakFile pakFile,
        string pathPattern,
        TReader reader)
        where TReader : IAssetReader<TData>
        where TData : class
    {
        var entry = pakFile.Entries
            .FirstOrDefault(e => e.FilePath?.Contains(pathPattern, StringComparison.OrdinalIgnoreCase) == true);
        
        if (entry.FilePath == null)
        {
            return Result<TData>.Failure($"No file found matching pattern '{pathPattern}'");
        }
        
        Console.WriteLine($" Found match: {entry.FilePath}\n");
        
        return LoadAsset<TReader, TData>(pakFile, entry, reader);
    }

    public static Result<TData> LoadAsset<TReader, TData>(
        PakFile pakFile,
        PakEntry entry,
        TReader reader)
        where TReader : IAssetReader<TData>
        where TData : class
    {
        Console.WriteLine($" Loading: {entry.FilePath}");
        Console.WriteLine($"   Size: {entry.UncompressedSize:N0} bytes");
        
        var stopwatch = Stopwatch.StartNew();
        
        var pakReader = new PakReaderV4();
        var data = pakReader.ExtractFile(pakFile, entry);
        
        if (data == null || data.Length == 0)
        {
            return Result<TData>.Failure("Failed to extract file from PAK");
        }
        
        var result = reader.Read(data, entry.FilePath);
        stopwatch.Stop();
        
        if (result.IsSuccess)
        {
            Console.WriteLine($" Loaded in {stopwatch.ElapsedMilliseconds}ms\n");
        }
        else
        {
            Console.WriteLine($" Failed: {result.Error}\n");
        }
        
        return result;
    }

    public static PakEntry? SelectFileFromList(List<PakEntry> entries, string title)
    {
        if (entries.Count == 0)
        {
            Console.WriteLine(" No files found.");
            return null;
        }

        Console.WriteLine($"\n{title}");
        Console.WriteLine(new string('=', title.Length));
        
        for (int i = 0; i < entries.Count; i++)
        {
            var entry = entries[i];
            var fileName = Path.GetFileName(entry.FilePath);
            var sizeKb = entry.UncompressedSize / 1024.0;
            Console.WriteLine($"{i + 1}. {fileName} ({sizeKb:F1} KB)");
        }
        
        Console.WriteLine($"\n0. Cancel");
        Console.Write("\nSelect file (0-{0}): ", entries.Count);
        
        if (int.TryParse(Console.ReadLine(), out int selection) && 
            selection > 0 && selection <= entries.Count)
        {
            return entries[selection - 1];
        }
        
        return null;
    }

    public static void PrintAssetData<T>(T data, string assetType) where T : AssetData
    {
        Console.WriteLine(new string('═', 80));
        ConsoleHelper.WriteColored($"  {assetType.ToUpper()}", ConsoleColor.Cyan);
        Console.WriteLine(new string('═', 80));
        
        data.PrintColored(assetType, ConsoleColor.Green, 1);
        
        Console.WriteLine(new string('═', 80));
    }

    public static void PrintAssetStats(AssetData data)
    {
        Console.WriteLine("\n Asset Statistics:");
        Console.WriteLine($"   Name: {data.Name}");
        Console.WriteLine($"   Type: {data.GetType().Name}");
        
        if (data is MaterialData material)
        {
            Console.WriteLine($"   Materials: {material.MaterialCount}");
            Console.WriteLine($"   Version: {material.Version}");
        }
        else if (data is TextureData texture)
        {
            Console.WriteLine($"   Width: {texture.Width}");
            Console.WriteLine($"   Height: {texture.Height}");
            Console.WriteLine($"   Format: {texture.Format}");
            Console.WriteLine($"   Images: {texture.NumImages}");
            Console.WriteLine($"   Mips per Image: {texture.MipsPerImage}");
        }
    }

    public static void ExploreAssetsInteractive<TReader, TData>(
        PakFile pakFile,
        string fileExtension,
        TReader reader,
        string assetTypeName)
        where TReader : IAssetReader<TData>
        where TData : AssetData
    {
        while (true)
        {
            Console.Clear();
            ConsoleHelper.WriteColored($"\n {assetTypeName} Explorer", ConsoleColor.Yellow);
            Console.WriteLine();
            
            var files = FindFilesByExtension(pakFile, fileExtension, 20);
            var selectedFile = SelectFileFromList(files, $"Available {assetTypeName} Files");
            
            if (selectedFile == null)
            {
                break;
            }
            
            var result = LoadAsset<TReader, TData>(pakFile, selectedFile.Value, reader);
            
            if (result.IsSuccess && result.Value != null)
            {
                PrintAssetData(result.Value, assetTypeName);
                PrintAssetStats(result.Value);
            }
            
            Console.WriteLine("\n\nPress any key to continue...");
            Console.ReadKey();
        }
    }
}

public static class ConsoleHelper
{
    public static void WriteColored(string text, ConsoleColor color)
    {
        var oldColor = Console.ForegroundColor;
        Console.ForegroundColor = color;
        Console.WriteLine(text);
        Console.ForegroundColor = oldColor;
    }
}
