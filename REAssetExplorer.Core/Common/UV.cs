namespace REAssetExplorer.Core.Common;

public class UV
{
    public readonly float U;
    public readonly float V;

    public UV(int packed)
    {
        ushort uHalf = (ushort)(packed & 0xFFFF);
        ushort vHalf = (ushort)((packed >> 16) & 0xFFFF);

        U = (float)BitConverter.UInt16BitsToHalf(uHalf);
        V = (float)BitConverter.UInt16BitsToHalf(vHalf);
    }

    public override string ToString()
        => $"U={U}, V={V}";
}