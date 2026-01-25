using FluentAssertions;
using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Games.RE8.Assets;
using REAssetExplorer.Testing.Fixtures;
using REAssetExplorer.Testing.TestHelpers;
using Xunit;

namespace REAssetExplorer.Testing.Games.RE8;

public class RE8TextureReaderTests : IClassFixture<PakFileFixture>
{
    private readonly PakFileFixture _fixture;
    private readonly RE8TextureReader _reader;
    
    public RE8TextureReaderTests(PakFileFixture fixture)
    {
        _fixture = fixture;
        _reader = new RE8TextureReader();
    }
    
    [SkipIfRE8NotInstalled]
    [IntegrationTest]
    public void Read_WithValidTextureFile_ReturnsSuccess()
    {
        var pakFile = _fixture.RE8PakFile!;
        var textureEntry = pakFile.Entries.FirstOrDefault(e => 
            e.FilePath?.EndsWith(".tex.30") == true);
        
        if (textureEntry.FilePath == null)
        {
            return;
        }
        
        var data = _fixture.ExtractFile(pakFile, textureEntry);
        var result = _reader.Read(data!, textureEntry.FilePath);
        
        result.IsSuccess.Should().BeTrue();
        result.Value.Should().NotBeNull();
    }
    
    [Fact]
    public void Read_WithInvalidData_ReturnsError()
    {
        byte[] invalidData = new byte[] { 0x00, 0x01, 0x02, 0x03 };
        var result = _reader.Read(invalidData, "test.tex.30");
        
        result.IsSuccess.Should().BeFalse();
    }
    
    [SkipIfRE8NotInstalled]
    [IntegrationTest]
    [SlowTest]
    public void Read_AllTextureFiles_ShouldSucceed()
    {
        var pakFile = _fixture.RE8PakFile!;
        
        var summary = AssetReaderTestHelper.TestAllAssetsOfType<RE8TextureReader, TextureData>(
            _fixture,
            pakFile,
            ".tex.30",
            _reader,
            maxFiles: null
        );
        
        summary.PrintReport("RE8 Texture");
        AssetReaderTestHelper.AssertMinimumSuccessRate(summary, 100, "Texture");
    }
}

public class RE8AIMapReaderTests : IClassFixture<PakFileFixture>
{
    private readonly PakFileFixture _fixture;
    private readonly RE8AIMapReader _reader;
    
    public RE8AIMapReaderTests(PakFileFixture fixture)
    {
        _fixture = fixture;
        _reader = new RE8AIMapReader();
    }
    
    [SkipIfRE8NotInstalled]
    [IntegrationTest]
    [SlowTest]
    public void Read_WithValidAIMapFile_ReturnsSuccess()
    {
        var pakFile = _fixture.RE8PakFile!;
        var aimapEntry = pakFile.Entries.FirstOrDefault(e => 
            e.FilePath?.Contains("c02_1_courtbasement_aimap.ainvm") == true);
        
        if (aimapEntry.FilePath == null)
        {
            return;
        }
        
        var data = _fixture.ExtractFile(pakFile, aimapEntry);
        var result = _reader.Read(data!, aimapEntry.FilePath);
        
        result.IsSuccess.Should().BeTrue();
        result.Value.Should().NotBeNull();
        
        if (result.IsSuccess)
        {
            Console.WriteLine($"AIMap loaded: {aimapEntry.FilePath}");
            Console.WriteLine($"Data size: {data!.Length} bytes");
        }
    }
    
    [SkipIfRE8NotInstalled]
    [IntegrationTest]
    [SlowTest]
    public void Read_AllAIMapFiles_ShouldSucceed()
    {
        var pakFile = _fixture.RE8PakFile!;
        
        var summary = AssetReaderTestHelper.TestAllAssetsOfType<RE8AIMapReader, AIMapData>(
            _fixture,
            pakFile,
            ".ainvm.8",
            _reader,
            maxFiles: null
        );
        
        summary.PrintReport("RE8 AIMap");
        
        AssetReaderTestHelper.AssertMinimumSuccessRate(summary, 100, "AIMap");
    }
    
    [SkipIfRE8NotInstalled]
    [IntegrationTest]
    public void Read_AIMapFiles_ShouldHaveValidStructure()
    {
        var pakFile = _fixture.RE8PakFile!;
        var aimapEntries = pakFile.Entries
            .Where(e => e.FilePath?.EndsWith(".ainvm.8") == true)
            .Take(10)
            .ToList();
        
        if (aimapEntries.Count == 0)
        {
            return;
        }
        
        int successCount = 0;
        int emptyNameCount = 0;
        
        foreach (var entry in aimapEntries)
        {
            var data = _fixture.ExtractFile(pakFile, entry);
            var result = _reader.Read(data!, entry.FilePath);
            
            if (result.IsSuccess)
            {
                result.Value.Should().NotBeNull();
                successCount++;
                
                if (string.IsNullOrEmpty(result.Value!.Name))
                {
                    emptyNameCount++;
                    Console.WriteLine($" {entry.FilePath}: (empty name)");
                }
                else
                {
                    Console.WriteLine($" {entry.FilePath}: {result.Value.Name}");
                }
            }
        }
        
        successCount.Should().BeGreaterThan(0, "At least some AIMap files should be readable");
        Console.WriteLine($"\nSummary: {successCount} AIMaps read, {emptyNameCount} with empty names");
    }
}

public class RE8BankReaderTests : IClassFixture<PakFileFixture>
{
    private readonly PakFileFixture _fixture;
    private readonly RE8BankReader _reader;
    
    public RE8BankReaderTests(PakFileFixture fixture)
    {
        _fixture = fixture;
        _reader = new RE8BankReader();
    }
    
    [SkipIfRE8NotInstalled]
    [IntegrationTest]
    [SlowTest]
    public void Read_WithValidBankFile_ReturnsSuccess()
    {
        var pakFile = _fixture.RE8PakFile!;
        var bankEntry = pakFile.Entries.FirstOrDefault(e => 
            e.FilePath?.Contains("em4100_v.bnk.2.stm") == true);
        
        if (bankEntry.FilePath == null)
        {
            return;
        }
        
        var data = _fixture.ExtractFile(pakFile, bankEntry);
        var result = _reader.Read(data!, bankEntry.FilePath);
        
        result.IsSuccess.Should().BeTrue();
        result.Value.Should().NotBeNull();
        
        if (result.IsSuccess)
        {
            Console.WriteLine($"Bank loaded: {bankEntry.FilePath}");
            Console.WriteLine($"Data size: {data!.Length} bytes");
        }
    }
    
    [SkipIfRE8NotInstalled]
    [IntegrationTest]
    [SlowTest]
    public void Read_AllBankFiles_ShouldSucceed()
    {
        var pakFile = _fixture.RE8PakFile!;
        
        var summary = AssetReaderTestHelper.TestAllAssetsOfType<RE8BankReader, BankData>(
            _fixture,
            pakFile,
            ".bnk.2.x64.*",
            _reader,
            maxFiles: null
        );
        
        summary.PrintReport("RE8 Bank");
        
        AssetReaderTestHelper.AssertMinimumSuccessRate(summary, 100, "Bank");
    }
    
    [SkipIfRE8NotInstalled]
    [IntegrationTest]
    public void Read_BankFiles_ShouldHaveValidStructure()
    {
        var pakFile = _fixture.RE8PakFile!;
        var bankEntries = pakFile.Entries
            .Where(e => e.FilePath?.EndsWith(".bnk.2.x64") == true)
            .Take(10)
            .ToList();
        
        if (bankEntries.Count == 0)
        {
            return;
        }
        
        int successCount = 0;
        int emptyNameCount = 0;
        
        foreach (var entry in bankEntries)
        {
            var data = _fixture.ExtractFile(pakFile, entry);
            var result = _reader.Read(data!, entry.FilePath);
            
            if (result.IsSuccess)
            {
                result.Value.Should().NotBeNull();
                successCount++;
                
                if (string.IsNullOrEmpty(result.Value!.Name))
                {
                    emptyNameCount++;
                    Console.WriteLine($" {entry.FilePath}: (empty name)");
                }
                else
                {
                    Console.WriteLine($" {entry.FilePath}: {result.Value.Name}");
                }
            }
        }
        
        successCount.Should().BeGreaterThan(0, "At least some Bank files should be readable");
        Console.WriteLine($"\nSummary: {successCount} Banks read, {emptyNameCount} with empty names");
    }
}