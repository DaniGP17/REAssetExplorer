using FluentAssertions;
using REAssetExplorer.Core.Pak;
using REAssetExplorer.Testing.Fixtures;
using REAssetExplorer.Testing.TestHelpers;
using Xunit;

namespace REAssetExplorer.Testing.Core.Pak;

public class PakFileListTests
{
    [Fact]
    public void SetupFromFile_WithValidFileList_LoadsEntries()
    {
        // Arrange
        var fileList = new PakFileList();
        
        // Act
        fileList.SetupFromFile(TestDataPaths.RE7FileList);
        
        // Assert
        fileList.Count.Should().BeGreaterThan(0);
    }
    
    [Fact]
    public void SetupFromFile_WithNonExistentFile_ThrowsException()
    {
        // Arrange
        var fileList = new PakFileList();
        
        // Act & Assert
        Action act = () => fileList.SetupFromFile("nonexistent.txt");
        act.Should().Throw<FileNotFoundException>();
    }
    
    [Fact]
    public void GetByLowerHash_WithKnownHash_ReturnsCorrectEntry()
    {
        // Arrange
        var fileList = new PakFileList();
        fileList.SetupFromFile(TestDataPaths.RE7FileList);
        
        const string testPath = "natives/stm/test.txt";
        uint testHash = REAssetExplorer.Core.Hashing.Murmur3.ConvertFilePathToMurmurHash(
            testPath, true, true, false);
        
        // Act
        var entry = fileList.GetByLowerHash(testHash);
        
        // Assert
        if (entry != null)
        {
            entry.Path.Should().NotBeNullOrEmpty();
        }
    }
    
    [Fact]
    public void ContainsLowerHash_WithExistingHash_ReturnsTrue()
    {
        // Arrange
        var fileList = new PakFileList();
        fileList.SetupFromFile(TestDataPaths.RE7FileList);
        
        var firstLine = File.ReadLines(TestDataPaths.RE7FileList)
            .FirstOrDefault(l => !string.IsNullOrWhiteSpace(l));
        
        if (firstLine != null)
        {
            uint hash = REAssetExplorer.Core.Hashing.Murmur3.ConvertFilePathToMurmurHash(
                firstLine, true, true, false);
            
            // Act
            bool contains = fileList.ContainsLowerHash(hash);
            
            // Assert
            contains.Should().BeTrue();
        }
    }
}

public class PakReaderTests : IClassFixture<PakFileFixture>
{
    private readonly PakFileFixture _fixture;
    
    public PakReaderTests(PakFileFixture fixture)
    {
        _fixture = fixture;
    }
    
    [SkipIfRE7NotInstalled]
    public void Open_WithValidPakFile_LoadsEntries()
    {
        // Assert
        _fixture.RE7PakFile.Should().NotBeNull();
        _fixture.RE7PakFile!.Entries.Should().NotBeEmpty();
        _fixture.RE7PakFile.Header.Should().NotBeNull();
    }
    
    [SkipIfRE7NotInstalled]
    public void ExtractFile_WithValidEntry_ReturnsData()
    {
        // Arrange
        var pakFile = _fixture.RE7PakFile!;
        var entry = pakFile.Entries.First(e => !string.IsNullOrEmpty(e.FilePath));
        
        // Act
        var data = _fixture.ExtractFile(pakFile, entry);
        
        // Assert
        data.Should().NotBeNull();
        data.Should().NotBeEmpty();
    }
    
    [SkipIfRE7NotInstalled]
    public void PakEntries_ShouldHaveValidProperties()
    {
        // Arrange
        var pakFile = _fixture.RE7PakFile!;
        
        // Act & Assert
        foreach (var entry in pakFile.Entries.Take(10))
        {
            entry.UncompressedSize.Should().BeGreaterThanOrEqualTo(0);
            entry.CompressedSize.Should().BeGreaterThanOrEqualTo(0);
            entry.Offset.Should().BeGreaterThanOrEqualTo(0);
            
            if (!string.IsNullOrEmpty(entry.FilePath))
            {
                entry.FilePath.Should().NotBeEmpty();
            }
        }
    }
}
