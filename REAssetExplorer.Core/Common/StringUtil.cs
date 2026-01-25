using System.Text;

namespace REAssetExplorer.Core.Common;

/// <summary>
/// Utility class for string operations in binary data.
/// </summary>
public static class StringUtil
{
    /// <summary>
    /// Reads a null-terminated string from a BinaryReader.
    /// </summary>
    /// <param name="reader">The BinaryReader to read from.</param>
    /// <param name="encoding">Optional encoding to use. Defaults to UTF-16 (Unicode).</param>
    /// <returns>The string read from the stream, without the null terminator.</returns>
    public static string ReadNullTerminatedString(BinaryReader reader, Encoding? encoding = null)
    {
        encoding ??= Encoding.Unicode; // UTF-16 Little Endian
        
        var bytes = new List<byte>();
        
        // For UTF-16, we need to read 2 bytes at a time
        while (true)
        {
            byte b1 = reader.ReadByte();
            byte b2 = reader.ReadByte();
            
            // Check if we hit the null terminator (0x00 0x00)
            if (b1 == 0 && b2 == 0)
            {
                break;
            }
            
            bytes.Add(b1);
            bytes.Add(b2);
        }
        
        return encoding.GetString(bytes.ToArray());
    }
    
    /// <summary>
    /// Reads a null-terminated string from a byte span at the specified offset.
    /// </summary>
    /// <param name="data">The byte span containing the string.</param>
    /// <param name="offset">The offset where the string starts.</param>
    /// <param name="encoding">Optional encoding to use. Defaults to UTF-16 (Unicode).</param>
    /// <returns>The string read from the span, without the null terminator.</returns>
    public static string ReadNullTerminatedString(ReadOnlySpan<byte> data, int offset = 0, Encoding? encoding = null)
    {
        encoding ??= Encoding.Unicode; // UTF-16 Little Endian
        
        int length = 0;
        // For UTF-16, check pairs of bytes
        while (offset + length + 1 < data.Length)
        {
            // Check if we hit the null terminator (0x00 0x00)
            if (data[offset + length] == 0 && data[offset + length + 1] == 0)
            {
                break;
            }
            length += 2;
        }
        
        return encoding.GetString(data.Slice(offset, length));
    }
}
