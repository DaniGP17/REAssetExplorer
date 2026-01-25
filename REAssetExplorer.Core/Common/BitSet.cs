using System;
using System.Numerics;
using System.Runtime.InteropServices;

namespace REAssetExplorer.Core.Common;

public class BitSet<T> where T : unmanaged, IBinaryInteger<T>
{
    private readonly T[] _data;
    public int BitCount { get; }
    public int ElementBitSize { get; }   // 8, 16, 32, 64
    public int ElementCount => _data.Length;

    public BitSet(int bitCount)
    {
        if (bitCount <= 0)
            throw new ArgumentOutOfRangeException(nameof(bitCount));

        BitCount = bitCount;
        ElementBitSize = Marshal.SizeOf<T>() * 8;
        _data = new T[(bitCount + ElementBitSize - 1) / ElementBitSize];
    }

    public BitSet(T[] rawData, int bitCount)
    {
        _data = rawData ?? throw new ArgumentNullException(nameof(rawData));
        BitCount = bitCount;
        ElementBitSize = Marshal.SizeOf<T>() * 8;
    }
    
    public void SetRaw(T value)
    {
        if (_data.Length == 0)
            return;

        _data[0] = value;

        for (int i = 1; i < _data.Length; i++)
            _data[i] = T.Zero;
    }

    public bool Get(int index)
    {
        CheckIndex(index);
        int elem = index / ElementBitSize;
        int bit = index % ElementBitSize;
        return ((_data[elem] >> bit) & T.One) != T.Zero;
    }

    public void Set(int index, bool value = true)
    {
        CheckIndex(index);
        int elem = index / ElementBitSize;
        int bit = index % ElementBitSize;

        if (value)
            _data[elem] |= (T.One << bit);
        else
            _data[elem] &= ~(T.One << bit);
    }

    public void Toggle(int index)
    {
        CheckIndex(index);
        int elem = index / ElementBitSize;
        int bit = index % ElementBitSize;
        _data[elem] ^= (T.One << bit);
    }

    public void Clear()
    {
        Array.Clear(_data, 0, _data.Length);
    }

    public T[] RawData => _data;

    private void CheckIndex(int index)
    {
        if ((uint)index >= BitCount)
            throw new IndexOutOfRangeException();
    }

    public override string ToString()
    {
        char[] chars = new char[BitCount];
        for (int i = 0; i < BitCount; i++)
            chars[BitCount - 1 - i] = Get(i) ? '1' : '0';
        return new string(chars);
    }
}