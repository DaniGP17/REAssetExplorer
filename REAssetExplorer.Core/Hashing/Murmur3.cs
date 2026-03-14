using System.Text;

namespace REAssetExplorer.Core.Hashing;

/// <summary>
/// Provides MurmurHash3 hashing functionality optimized for file path hashing.
/// </summary>
public static class Murmur3
{
    private const uint DefaultSeed = 0xFFFFFFFF;
    private const uint C1 = 0xcc9e2d51;
    private const uint C2 = 0x1b873593;
    private const uint MixConstant = 0xe6546b64;
    private const int ChunkSize = 4;
    private const int RotationAmount = 15;
    private const int HashRotation = 13;
    private const int HashMultiplier = 5;
    
    /// <summary>
    /// Converts a file path to a MurmurHash3 32-bit hash.
    /// </summary>
    /// <param name="filePath">The file path to hash.</param>
    /// <param name="lower">If true and changeCase is true, converts to lowercase.</param>
    /// <param name="changeCase">Whether to change the case of the path.</param>
    /// <param name="keepNullTerminator">Whether to include a null terminator in the hash.</param>
    /// <returns>The 32-bit hash value.</returns>
    public static uint ConvertFilePathToMurmurHash(string filePath, bool lower, bool changeCase, bool keepNullTerminator)
    {
        ArgumentNullException.ThrowIfNull(filePath);
        
        var normalizedPath = NormalizePath(filePath, lower, changeCase);
        var bytes = EncodePathToBytes(normalizedPath, keepNullTerminator);
        
        return MurmurHash3_x86_32(bytes, DefaultSeed);
    }
    
    public static uint MakeHash(string str)
    {
        var bytes = Encoding.ASCII.GetBytes(str);
        return MurmurHash3_x86_32(bytes, DefaultSeed);
    }

    private static string NormalizePath(string path, bool lower, bool changeCase)
    {
        path = path.Replace('\\', '/');
        
        if (changeCase)
        {
            path = lower ? path.ToLowerInvariant() : path.ToUpperInvariant();
        }
        
        return path;
    }

    private static byte[] EncodePathToBytes(string path, bool includeNullTerminator)
    {
        var bytes = Encoding.Unicode.GetBytes(path);
        
        if (includeNullTerminator)
        {
            Array.Resize(ref bytes, bytes.Length + 2);
        }
        
        return bytes;
    }

    private static uint MurmurHash3_x86_32(byte[] data, uint seed)
    {
        uint hash = seed;
        int currentIndex = 0;
        int blocks = data.Length / ChunkSize;

        // Process 4-byte chunks
        hash = ProcessBlocks(data, hash, blocks, ref currentIndex);
        
        // Process remaining bytes
        hash = ProcessTail(data, hash, currentIndex);
        
        // Finalize hash
        hash ^= (uint)data.Length;
        hash = Fmix32(hash);

        return hash;
    }

    private static uint ProcessBlocks(byte[] data, uint hash, int blocks, ref int currentIndex)
    {
        for (int i = 0; i < blocks; i++)
        {
            uint k1 = BitConverter.ToUInt32(data, currentIndex);
            currentIndex += ChunkSize;

            k1 = MixKey(k1);
            hash = MixHash(hash, k1);
        }

        return hash;
    }

    private static uint ProcessTail(byte[] data, uint hash, int currentIndex)
    {
        int remaining = data.Length - currentIndex;
        
        if (remaining == 0)
            return hash;

        uint tailKey = ExtractTailKey(data, currentIndex, remaining);
        tailKey = MixKey(tailKey);
        
        return hash ^ tailKey;
    }

    private static uint ExtractTailKey(byte[] data, int startIndex, int count)
    {
        uint key = 0;
        
        for (int i = count - 1; i >= 0; i--)
        {
            key <<= 8;
            key |= data[startIndex + i];
        }
        
        return key;
    }

    private static uint MixKey(uint key)
    {
        key *= C1;
        key = RotateLeft(key, RotationAmount);
        key *= C2;
        return key;
    }

    private static uint MixHash(uint hash, uint key)
    {
        hash ^= key;
        hash = RotateLeft(hash, HashRotation);
        hash = hash * HashMultiplier + MixConstant;
        return hash;
    }

    private static uint RotateLeft(uint x, int n)
    {
        return (x << n) | (x >> (32 - n));
    }

    private static uint Fmix32(uint h)
    {
        h ^= h >> 16;
        h *= 0x85ebca6b;
        h ^= h >> 13;
        h *= 0xc2b2ae35;
        h ^= h >> 16;
        return h;
    }
}