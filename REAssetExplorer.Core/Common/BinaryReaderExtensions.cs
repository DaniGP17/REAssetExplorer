using System;
using System.Collections.Generic;
using System.IO;
using System.Numerics;
using System.Runtime.InteropServices;
using System.Text;

namespace REAssetExplorer.Core.Common;

/// <summary>
/// Extension methods for BinaryReader to simplify reading structured data.
/// </summary>
public static class BinaryReaderExtensions
{
    /// <summary>
    /// Reads an array of primitive values from the stream.
    /// </summary>
    /// <typeparam name="T">The type of elements to read (must be an unmanaged type).</typeparam>
    /// <param name="reader">The BinaryReader instance.</param>
    /// <param name="count">The number of elements to read.</param>
    /// <returns>An array of T with the specified count.</returns>
    /// <exception cref="ArgumentOutOfRangeException">Thrown when count is negative.</exception>
    public static T[] ReadArray<T>(this BinaryReader reader, int count) where T : unmanaged
    {
        if (count < 0)
            throw new ArgumentOutOfRangeException(nameof(count), "Count cannot be negative.");

        if (count == 0)
            return Array.Empty<T>();

        var array = new T[count];
        var elementSize = Marshal.SizeOf<T>();
        var totalBytes = elementSize * count;
        var bytes = reader.ReadBytes(totalBytes);

        if (bytes.Length < totalBytes)
            throw new EndOfStreamException($"Expected {totalBytes} bytes but only {bytes.Length} were available.");

        Buffer.BlockCopy(bytes, 0, array, 0, totalBytes);
        return array;
    }

    /// <summary>
    /// Reads an array of structures using a custom reader function.
    /// </summary>
    /// <typeparam name="T">The type of elements to read.</typeparam>
    /// <param name="reader">The BinaryReader instance.</param>
    /// <param name="count">The number of elements to read.</param>
    /// <param name="readFunc">Function to read a single element.</param>
    /// <returns>An array of T with the specified count.</returns>
    public static T[] ReadArray<T>(this BinaryReader reader, int count, Func<BinaryReader, T> readFunc)
    {
        if (count < 0)
            throw new ArgumentOutOfRangeException(nameof(count), "Count cannot be negative.");

        if (count == 0)
            return Array.Empty<T>();

        var array = new T[count];
        for (int i = 0; i < count; i++)
        {
            array[i] = readFunc(reader);
        }
        return array;
    }

    /// <summary>
    /// Reads a list of structures using a custom reader function.
    /// </summary>
    /// <typeparam name="T">The type of elements to read.</typeparam>
    /// <param name="reader">The BinaryReader instance.</param>
    /// <param name="count">The number of elements to read.</param>
    /// <param name="readFunc">Function to read a single element.</param>
    /// <returns>A list of T with the specified count.</returns>
    public static List<T> ReadList<T>(this BinaryReader reader, int count, Func<BinaryReader, T> readFunc)
    {
        if (count < 0)
            throw new ArgumentOutOfRangeException(nameof(count), "Count cannot be negative.");

        var list = new List<T>(count);
        for (int i = 0; i < count; i++)
        {
            list.Add(readFunc(reader));
        }
        return list;
    }

    /// <summary>
    /// Reads a Vector2 (2 floats).
    /// </summary>
    public static Vector2 ReadVector2(this BinaryReader reader)
    {
        return new Vector2(reader.ReadSingle(), reader.ReadSingle());
    }

    /// <summary>
    /// Reads a Vector3 (3 floats).
    /// </summary>
    public static Vector3 ReadVector3(this BinaryReader reader)
    {
        return new Vector3(reader.ReadSingle(), reader.ReadSingle(), reader.ReadSingle());
    }

    /// <summary>
    /// Reads a Vector4 (4 floats).
    /// </summary>
    public static Vector4 ReadVector4(this BinaryReader reader)
    {
        return new Vector4(reader.ReadSingle(), reader.ReadSingle(), reader.ReadSingle(), reader.ReadSingle());
    }

    /// <summary>
    /// Reads a Quaternion (4 floats: X, Y, Z, W).
    /// </summary>
    public static Quaternion ReadQuaternion(this BinaryReader reader)
    {
        return new Quaternion(reader.ReadSingle(), reader.ReadSingle(), reader.ReadSingle(), reader.ReadSingle());
    }

    /// <summary>
    /// Reads a Matrix4x4 (16 floats).
    /// </summary>
    public static Matrix4x4 ReadMatrix4x4(this BinaryReader reader)
    {
        return new Matrix4x4(reader.ReadBytes(64));
    }

    /// <summary>
    /// Reads a null-terminated string (C-style string).
    /// </summary>
    /// <param name="reader">The BinaryReader instance.</param>
    /// <param name="encoding">The encoding to use (default is UTF-8).</param>
    /// <returns>The string read from the stream.</returns>
    public static string ReadNullTerminatedString(this BinaryReader reader, System.Text.Encoding? encoding = null)
    {
        encoding ??= System.Text.Encoding.UTF8;
        var bytes = new List<byte>();

        while (true)
        {
            var b = reader.ReadByte();
            if (b == 0)
                break;
            bytes.Add(b);
        }

        return encoding.GetString(bytes.ToArray());
    }

    /// <summary>
    /// Reads a fixed-length string.
    /// </summary>
    /// <param name="reader">The BinaryReader instance.</param>
    /// <param name="length">The length of the string in bytes.</param>
    /// <param name="encoding">The encoding to use (default is UTF-8).</param>
    /// <returns>The string read from the stream, trimmed of null terminators.</returns>
    public static string ReadFixedString(
        this BinaryReader reader,
        int length,
        Encoding? encoding = null)
    {
        encoding ??= Encoding.Unicode; // UTF-16 LE

        var bytes = reader.ReadBytes(length);

        if (encoding == Encoding.Unicode || encoding == Encoding.BigEndianUnicode)
        {
            // Buscar terminador 0x00 0x00
            for (int i = 0; i < bytes.Length - 1; i += 2)
            {
                if (bytes[i] == 0 && bytes[i + 1] == 0)
                    return encoding.GetString(bytes, 0, i);
            }

            return encoding.GetString(bytes);
        }
        else
        {
            // UTF-8 / otros
            int nullIndex = Array.IndexOf(bytes, (byte)0);
            if (nullIndex >= 0)
                return encoding.GetString(bytes, 0, nullIndex);

            return encoding.GetString(bytes);
        }
    }


    /// <summary>
    /// Aligns the stream position to the specified boundary.
    /// </summary>
    /// <param name="reader">The BinaryReader instance.</param>
    /// <param name="alignment">The alignment boundary (must be a power of 2).</param>
    public static void Align(this BinaryReader reader, int alignment)
    {
        var position = reader.BaseStream.Position;
        var remainder = position % alignment;
        
        if (remainder != 0)
        {
            var padding = alignment - remainder;
            reader.BaseStream.Seek(padding, SeekOrigin.Current);
        }
    }

    /// <summary>
    /// Reads data at a specific offset and returns to the original position.
    /// </summary>
    /// <typeparam name="T">The type to read.</typeparam>
    /// <param name="reader">The BinaryReader instance.</param>
    /// <param name="offset">The offset to seek to.</param>
    /// <param name="readFunc">Function to read the data.</param>
    /// <returns>The data read at the offset.</returns>
    public static T ReadAt<T>(this BinaryReader reader, long offset, Func<BinaryReader, T> readFunc)
    {
        var originalPosition = reader.BaseStream.Position;
        try
        {
            reader.BaseStream.Seek(offset, SeekOrigin.Begin);
            return readFunc(reader);
        }
        finally
        {
            reader.BaseStream.Seek(originalPosition, SeekOrigin.Begin);
        }
    }

    /// <summary>
    /// Peeks at the next bytes without advancing the stream position.
    /// </summary>
    /// <param name="reader">The BinaryReader instance.</param>
    /// <param name="count">The number of bytes to peek.</param>
    /// <returns>The bytes peeked from the stream.</returns>
    public static byte[] Peek(this BinaryReader reader, int count)
    {
        var bytes = reader.ReadBytes(count);
        reader.BaseStream.Seek(-bytes.Length, SeekOrigin.Current);
        return bytes;
    }
}
