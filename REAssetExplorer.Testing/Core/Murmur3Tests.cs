using FluentAssertions;
using REAssetExplorer.Core.Hashing;
using Xunit;

namespace REAssetExplorer.Testing.Core;

public class Murmur3Tests
{
    [Theory]
    [InlineData("test.txt", true, true, false)]
    [InlineData("natives/folder/file.mesh", true, true, false)]
    [InlineData("", true, true, false)]
    public void ConvertFilePathToMurmurHash_WithValidPaths_ReturnsHash(
        string path, bool lower, bool changeCase, bool keepNullTerminator)
    {
        // Act
        uint hash = Murmur3.ConvertFilePathToMurmurHash(path, lower, changeCase, keepNullTerminator);
        
        // Assert
        hash.Should().NotBe(0u, "hash should not be zero for non-empty paths");
    }
    
    [Fact]
    public void ConvertFilePathToMurmurHash_WithNullPath_ThrowsArgumentNullException()
    {
        // Act & Assert
        Action act = () => Murmur3.ConvertFilePathToMurmurHash(null!, true, true, false);
        act.Should().Throw<ArgumentNullException>();
    }
    
    [Fact]
    public void ConvertFilePathToMurmurHash_WithSamePath_ReturnsSameHash()
    {
        // Arrange
        const string path = "natives/test/file.mesh";
        
        // Act
        uint hash1 = Murmur3.ConvertFilePathToMurmurHash(path, true, true, false);
        uint hash2 = Murmur3.ConvertFilePathToMurmurHash(path, true, true, false);
        
        // Assert
        hash1.Should().Be(hash2);
    }
    
    [Fact]
    public void ConvertFilePathToMurmurHash_WithDifferentCase_ReturnsDifferentHashIfCaseSensitive()
    {
        // Arrange
        const string path1 = "natives/test/File.mesh";
        const string path2 = "natives/test/file.mesh";
        
        // Act - sin cambiar case
        uint hash1 = Murmur3.ConvertFilePathToMurmurHash(path1, false, false, false);
        uint hash2 = Murmur3.ConvertFilePathToMurmurHash(path2, false, false, false);
        
        // Assert
        hash1.Should().NotBe(hash2, "case-sensitive hashing should produce different hashes");
    }
    
    [Fact]
    public void ConvertFilePathToMurmurHash_WithDifferentCase_ReturnsSameHashIfLowercase()
    {
        // Arrange
        const string path1 = "natives/test/File.mesh";
        const string path2 = "natives/test/FILE.mesh";
        
        // Act - convertir a lowercase
        uint hash1 = Murmur3.ConvertFilePathToMurmurHash(path1, true, true, false);
        uint hash2 = Murmur3.ConvertFilePathToMurmurHash(path2, true, true, false);
        
        // Assert
        hash1.Should().Be(hash2, "lowercase conversion should produce same hash");
    }
    
    [Fact]
    public void ConvertFilePathToMurmurHash_NormalizesBackslashes()
    {
        // Arrange
        const string pathWithBackslash = "natives\\test\\file.mesh";
        const string pathWithForwardslash = "natives/test/file.mesh";
        
        // Act
        uint hash1 = Murmur3.ConvertFilePathToMurmurHash(pathWithBackslash, true, true, false);
        uint hash2 = Murmur3.ConvertFilePathToMurmurHash(pathWithForwardslash, true, true, false);
        
        // Assert
        hash1.Should().Be(hash2, "backslashes should be normalized to forward slashes");
    }
}
