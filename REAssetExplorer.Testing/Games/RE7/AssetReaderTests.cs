using FluentAssertions;
using REAssetExplorer.Core.Assets;
using REAssetExplorer.Core.Assets.Models;
using REAssetExplorer.Games.RE7.Assets;
using REAssetExplorer.Testing.Fixtures;
using REAssetExplorer.Testing.TestHelpers;
using Xunit;

namespace REAssetExplorer.Testing.Games.RE7;

public class RE7MaterialReaderTests : IClassFixture<PakFileFixture>
{
    private readonly PakFileFixture _fixture;
    private readonly RE7MaterialReader _reader;
    
    public RE7MaterialReaderTests(PakFileFixture fixture)
    {
        _fixture = fixture;
        _reader = new RE7MaterialReader();
    }
    
    [SkipIfRE7NotInstalled]
    [IntegrationTest]
    public void Read_WithValidMaterialFile_ReturnsSuccess()
    {
        var pakFile = _fixture.RE7PakFile!;
        var materialEntry = pakFile.Entries.FirstOrDefault(e => 
            e.FilePath?.EndsWith(".mdf2.10") == true);
        
        if (materialEntry.FilePath == null)
        {
            return;
        }
        
        var data = _fixture.ExtractFile(pakFile, materialEntry);
        var result = _reader.Read(data!, materialEntry.FilePath);
        
        result.IsSuccess.Should().BeTrue();
        result.Value.Should().NotBeNull();
    }
    
    [Fact]
    public void Read_WithNullData_ReturnsError()
    {
        var result = _reader.Read(null!, "test.mdf2.10");
        
        result.IsSuccess.Should().BeFalse();
        result.Error.Should().NotBeNullOrEmpty();
    }
    
    [Fact]
    public void Read_WithEmptyData_ReturnsError()
    {
        var result = _reader.Read(Array.Empty<byte>(), "test.mdf2.10");
        
        result.IsSuccess.Should().BeFalse();
    }
    
    [SkipIfRE7NotInstalled]
    [IntegrationTest]
    [SlowTest]
    public void Read_AllMaterialFiles_ShouldSucceed()
    {
        var pakFile = _fixture.RE7PakFile!;
        
        var summary = AssetReaderTestHelper.TestAllAssetsOfType<RE7MaterialReader, MaterialData>(
            _fixture,
            pakFile,
            ".mdf2.21",
            _reader,
            maxFiles: null
        );
        
        summary.PrintReport("RE7 Material");
        AssetReaderTestHelper.AssertMinimumSuccessRate(summary, 100, "Material");
    }
}

public class RE7TextureReaderTests : IClassFixture<PakFileFixture>
{
    private readonly PakFileFixture _fixture;
    private readonly RE7TextureReader _reader;
    
    public RE7TextureReaderTests(PakFileFixture fixture)
    {
        _fixture = fixture;
        _reader = new RE7TextureReader();
    }
    
    [SkipIfRE7NotInstalled]
    [IntegrationTest]
    public void Read_WithValidTextureFile_ReturnsSuccess()
    {
        var pakFile = _fixture.RE7PakFile!;
        var textureEntry = pakFile.Entries.FirstOrDefault(e => 
            e.FilePath?.EndsWith(".tex.10") == true);
        
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
        var result = _reader.Read(invalidData, "test.tex.10");
        
        result.IsSuccess.Should().BeFalse();
    }
    
    [SkipIfRE7NotInstalled]
    [IntegrationTest]
    [SlowTest]
    public void Read_AllTextureFiles_ShouldSucceed()
    {
        var pakFile = _fixture.RE7PakFile!;
        
        var summary = AssetReaderTestHelper.TestAllAssetsOfType<RE7TextureReader, TextureData>(
            _fixture,
            pakFile,
            ".tex.35",
            _reader,
            maxFiles: null
        );
        
        summary.PrintReport("RE7 Texture");
        AssetReaderTestHelper.AssertMinimumSuccessRate(summary, 100, "Texture");
    }
}

public class RE7AIMapReaderTests : IClassFixture<PakFileFixture>
{
    private readonly PakFileFixture _fixture;
    private readonly RE7AIMapReader _reader;
    
    public RE7AIMapReaderTests(PakFileFixture fixture)
    {
        _fixture = fixture;
        _reader = new RE7AIMapReader();
    }
    
    [SkipIfRE7NotInstalled]
    [IntegrationTest]
    [SlowTest]
    public void Read_WithValidAIMapFile_ReturnsSuccess()
    {
        var pakFile = _fixture.RE7PakFile!;
        var aimapEntry = pakFile.Entries.FirstOrDefault(e => 
            e.FilePath?.Contains("c01_aimap.aimap") == true);
        
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
    
    [SkipIfRE7NotInstalled]
    [IntegrationTest]
    [SlowTest]
    public void Read_AllAIMapFiles_ShouldSucceed()
    {
        var pakFile = _fixture.RE7PakFile!;
        
        var summary = AssetReaderTestHelper.TestAllAssetsOfType<RE7AIMapReader, AIMapData>(
            _fixture,
            pakFile,
            ".aimap.41",
            _reader,
            maxFiles: null
        );
        
        summary.PrintReport("RE7 AIMap");
        
        AssetReaderTestHelper.AssertMinimumSuccessRate(summary, 100, "AIMap");
    }
    
    [SkipIfRE7NotInstalled]
    [IntegrationTest]
    public void Read_AIMapFiles_ShouldHaveValidStructure()
    {
        var pakFile = _fixture.RE7PakFile!;
        var aimapEntries = pakFile.Entries
            .Where(e => e.FilePath?.EndsWith(".aimap.41") == true)
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

public class RE7MeshReaderTests : IClassFixture<PakFileFixture>
{
    private readonly PakFileFixture _fixture;
    private readonly RE7MeshReader _reader;
    
    public RE7MeshReaderTests(PakFileFixture fixture)
    {
        _fixture = fixture;
        _reader = new RE7MeshReader();
    }
    
    [SkipIfRE7NotInstalled]
    [IntegrationTest]
    [SlowTest]
    public void Read_WithValidMeshFile_ReturnsSuccess()
    {
        var pakFile = _fixture.RE7PakFile!;
        var aimapEntry = pakFile.Entries.FirstOrDefault(e => 
            e.FilePath?.Contains("pl0000.mesh") == true);
        
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
            Console.WriteLine($"Mesh loaded: {aimapEntry.FilePath}");
            Console.WriteLine($"Data size: {data!.Length} bytes");
        }
    }
    
    [SkipIfRE7NotInstalled]
    [IntegrationTest]
    [SlowTest]
    public void Read_AllMeshFiles_ShouldSucceed()
    {
        var pakFile = _fixture.RE7PakFile!;
        
        var summary = AssetReaderTestHelper.TestAllAssetsOfType<RE7MeshReader, MeshData>(
            _fixture,
            pakFile,
            ".mesh.220128762",
            _reader,
            maxFiles: null
        );
        
        summary.PrintReport("RE7 Mesh");
        
        AssetReaderTestHelper.AssertMinimumSuccessRate(summary, 100, "Mesh");
    }
    
    [SkipIfRE7NotInstalled]
    [IntegrationTest]
    public void Read_MeshFiles_ShouldHaveValidStructure()
    {
        var pakFile = _fixture.RE7PakFile!;
        var meshEntries = pakFile.Entries
            .Where(e => e.FilePath?.EndsWith(".mesh.220128762") == true)
            .Take(10)
            .ToList();
        
        if (meshEntries.Count == 0)
        {
            return;
        }
        
        int successCount = 0;
        int emptyNameCount = 0;
        
        foreach (var entry in meshEntries)
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
        
        successCount.Should().BeGreaterThan(0, "At least some mesh files should be readable");
        Console.WriteLine($"\nSummary: {successCount} meshes read, {emptyNameCount} with empty names");
    }
}

public class RE7BankReaderTests : IClassFixture<PakFileFixture>
{
    private readonly PakFileFixture _fixture;
    private readonly RE7BankReader _reader;
    
    public RE7BankReaderTests(PakFileFixture fixture)
    {
        _fixture = fixture;
        _reader = new RE7BankReader();
    }
    
    [SkipIfRE7NotInstalled]
    [IntegrationTest]
    [SlowTest]
    public void Read_WithValidBankFile_ReturnsSuccess()
    {
        var pakFile = _fixture.RE7PakFile!;
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
    
    [SkipIfRE7NotInstalled]
    [IntegrationTest]
    [SlowTest]
    public void Read_AllBankFiles_ShouldSucceed()
    {
        var pakFile = _fixture.RE7PakFile!;
        
        var summary = AssetReaderTestHelper.TestAllAssetsOfType<RE7BankReader, BankData>(
            _fixture,
            pakFile,
            ".bnk.2.stm.*",
            _reader,
            maxFiles: null
        );
        
        summary.PrintReport("RE7 Bank");
        
        AssetReaderTestHelper.AssertMinimumSuccessRate(summary, 100, "Bank");
    }
    
    [SkipIfRE7NotInstalled]
    [IntegrationTest]
    public void Read_BankFiles_ShouldHaveValidStructure()
    {
        var pakFile = _fixture.RE7PakFile!;
        var bankEntries = pakFile.Entries
            .Where(e => e.FilePath?.EndsWith(".bnk.2.stm") == true)
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

public class RE7SdfReaderTests : IClassFixture<PakFileFixture>
{
    private readonly PakFileFixture _fixture;
    private readonly RE7SdfReader _reader;
    
    public RE7SdfReaderTests(PakFileFixture fixture)
    {
        _fixture = fixture;
        _reader = new RE7SdfReader();
    }
    
    [SkipIfRE7NotInstalled]
    [IntegrationTest]
    [SlowTest]
    public void Read_WithValidSdfFile_ReturnsSuccess()
    {
        var pakFile = _fixture.RE7PakFile!;
        var sdfEntry = pakFile.Entries.FirstOrDefault(e => 
            e.FilePath?.Contains("env_default.mmtr") == true);
        
        if (sdfEntry.FilePath == null)
        {
            return;
        }
        
        var data = _fixture.ExtractFile(pakFile, sdfEntry);
        var result = _reader.Read(data!, sdfEntry.FilePath);
        
        result.IsSuccess.Should().BeTrue();
        result.Value.Should().NotBeNull();
        
        if (result.IsSuccess)
        {
            Console.WriteLine($"SDF loaded: {sdfEntry.FilePath}");
            Console.WriteLine($"Data size: {data!.Length} bytes");
        }
    }
    
    [SkipIfRE7NotInstalled]
    [IntegrationTest]
    [SlowTest]
    public void Read_AllSdfFiles_ShouldSucceed()
    {
        var pakFile = _fixture.RE7PakFile!;
        
        var summary = AssetReaderTestHelper.TestAllAssetsOfType<RE7SdfReader, SdfData>(
            _fixture,
            pakFile,
            ".mmtr.220128762",
            _reader,
            maxFiles: null
        );
        
        summary.PrintReport("RE7 SDF");
        
        AssetReaderTestHelper.AssertMinimumSuccessRate(summary, 100, "SDF");
    }
    
    [SkipIfRE7NotInstalled]
    [IntegrationTest]
    public void Read_SdfFiles_ShouldHaveValidStructure()
    {
        var pakFile = _fixture.RE7PakFile!;
        var sdfEntries = pakFile.Entries
            .Where(e => e.FilePath?.EndsWith(".mmtr.220128762") == true)
            .Take(10)
            .ToList();
        
        if (sdfEntries.Count == 0)
        {
            return;
        }
        
        int successCount = 0;
        int emptyNameCount = 0;
        
        foreach (var entry in sdfEntries)
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
        
        successCount.Should().BeGreaterThan(0, "At least some SDF files should be readable");
        Console.WriteLine($"\nSummary: {successCount} SDF read, {emptyNameCount} with empty names");
    }
}

public class RE7SceneReaderTests : IClassFixture<PakFileFixture>
{
    private readonly PakFileFixture _fixture;
    private readonly RE7SceneReader _reader;
    
    public RE7SceneReaderTests(PakFileFixture fixture)
    {
        _fixture = fixture;
        _reader = new RE7SceneReader();
    }
    
    [SkipIfRE7NotInstalled]
    [IntegrationTest]
    [SlowTest]
    public void Read_WithValidSceneile_ReturnsSuccess()
    {
        var pakFile = _fixture.RE7PakFile!;
        var scnEntry = pakFile.Entries.FirstOrDefault(e => 
            e.FilePath?.Contains("environment/scene/chapter1/c01_2f.scn.20") == true);
        
        if (scnEntry.FilePath == null)
        {
            return;
        }
        
        var data = _fixture.ExtractFile(pakFile, scnEntry);
        var result = _reader.Read(data!, scnEntry.FilePath);

        result.IsSuccess.Should().BeTrue(result.Exception?.ToString() ?? result.Error ?? "unknown error");
        result.Value.Should().NotBeNull();
        
        if (result.IsSuccess)
        {
            Console.WriteLine($"Scene loaded: {scnEntry.FilePath}");
            Console.WriteLine($"Data size: {data!.Length} bytes");
        }
    }
    
    [SkipIfRE7NotInstalled]
    [IntegrationTest]
    [SlowTest]
    public void Read_AllSceneFiles_ShouldSucceed()
    {
        var pakFile = _fixture.RE7PakFile!;
        
        var summary = AssetReaderTestHelper.TestAllAssetsOfType<RE7SceneReader, SceneData>(
            _fixture,
            pakFile,
            ".scn.20",
            _reader,
            maxFiles: null
        );
        
        summary.PrintReport("RE7 Scene");
        
        AssetReaderTestHelper.AssertMinimumSuccessRate(summary, 100, "Scene");
    }
    
    [SkipIfRE7NotInstalled]
    [IntegrationTest]
    public void Read_SceneFiles_ShouldHaveValidStructure()
    {
        var pakFile = _fixture.RE7PakFile!;
        var scnEntries = pakFile.Entries
            .Where(e => e.FilePath?.EndsWith(".scn.20") == true)
            .Take(10)
            .ToList();
        
        if (scnEntries.Count == 0)
        {
            return;
        }
        
        int successCount = 0;
        int emptyNameCount = 0;
        
        foreach (var entry in scnEntries)
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
        
        successCount.Should().BeGreaterThan(0, "At least some Scene files should be readable");
        Console.WriteLine($"\nSummary: {successCount} scenes read, {emptyNameCount} with empty names");
    }
}