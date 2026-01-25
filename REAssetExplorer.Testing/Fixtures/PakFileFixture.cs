using REAssetExplorer.Core.Pak;

namespace REAssetExplorer.Testing.Fixtures;

public class PakFileFixture : IDisposable
{
    public PakFile? RE7PakFile { get; private set; }
    public PakFile? RE8PakFile { get; private set; }
    
    public PakFileList? RE7FileList { get; private set; }
    public PakFileList? RE8FileList { get; private set; }
    
    private readonly PakReaderV4 _reader = new();
    
    public PakFileFixture()
    {
        InitializeRE7();
        InitializeRE8();
    }
    
    private void InitializeRE7()
    {
        try
        {
            if (!TestHelpers.TestDataPaths.IsRE7Installed())
            {
                Console.WriteLine("RE7 not installed, skipping PAK initialization");
                return;
            }
            
            RE7FileList = new PakFileList();
            RE7FileList.SetupFromFile(TestHelpers.TestDataPaths.RE7FileList);
            
            RE7PakFile = _reader.Open(TestHelpers.TestDataPaths.RE7MainPak, RE7FileList);
            Console.WriteLine($"RE7 PAK loaded: {RE7PakFile.Entries.Count} entries");
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Failed to initialize RE7 PAK: {ex.Message}");
        }
    }
    
    private void InitializeRE8()
    {
        try
        {
            if (!TestHelpers.TestDataPaths.IsRE8Installed())
            {
                Console.WriteLine("RE8 not installed, skipping PAK initialization");
                return;
            }
            
            RE8FileList = new PakFileList();
            RE8FileList.SetupFromFile(TestHelpers.TestDataPaths.RE8FileList);
            
            RE8PakFile = _reader.Open(TestHelpers.TestDataPaths.RE8MainPak, RE8FileList);
            Console.WriteLine($"RE8 PAK loaded: {RE8PakFile.Entries.Count} entries");
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Failed to initialize RE8 PAK: {ex.Message}");
        }
    }
    
    public byte[]? ExtractFile(PakFile pakFile, PakEntry entry)
    {
        return _reader.ExtractFile(pakFile, entry);
    }
    
    public void Dispose()
    {
        GC.SuppressFinalize(this);
    }
}
