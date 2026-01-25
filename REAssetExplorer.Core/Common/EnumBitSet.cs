using System.Numerics;

namespace REAssetExplorer.Core.Common;

public class EnumBitSet<TEnum, TStorage>
    where TEnum : Enum
    where TStorage : unmanaged, IBinaryInteger<TStorage>
{
    private readonly BitSet<TStorage> _bits;

    public EnumBitSet(int bitCount)
    {
        _bits = new BitSet<TStorage>(bitCount);
    }
    
    public EnumBitSet(int bitCount, TStorage rawValue)
    {
        _bits = new BitSet<TStorage>(bitCount);
        _bits.SetRaw(rawValue);
    }
    
    public string Bits => _bits.ToString();

    public TStorage RawValue =>
        _bits.RawData.Length > 0 ? _bits.RawData[0] : TStorage.Zero;

    public bool Get(TEnum value)
        => _bits.Get(Convert.ToInt32(value));

    public void Set(TEnum value, bool enabled = true)
        => _bits.Set(Convert.ToInt32(value), enabled);
    
    public override string ToString()
    {
        return _bits.ToString();
    }
}