namespace REAssetExplorer.Core.Common;

public class NormalTangentPacked
{
    // Normal (snorm8)
    public sbyte Nx;
    public sbyte Ny;
    public sbyte Nz;
    public sbyte Nw;

    // Tangent (snorm8)
    public sbyte Tx;
    public sbyte Ty;
    public sbyte Tz;
    public sbyte Tw;

    public NormalTangentPacked(Int64 packedData)
    {
        Nx = (sbyte)((packedData >> 0) & 0xFF);
        Ny = (sbyte)((packedData >> 8) & 0xFF);
        Nz = (sbyte)((packedData >> 16) & 0xFF);
        Nw = (sbyte)((packedData >> 24) & 0xFF);
        Tx = (sbyte)((packedData >> 32) & 0xFF);
        Ty = (sbyte)((packedData >> 40) & 0xFF);
        Tz = (sbyte)((packedData >> 48) & 0xFF);
        Tw = (sbyte)((packedData >> 56) & 0xFF);
    }
    
    static float SNORM8(sbyte v)
    {
        return Math.Max(v / 127.0f, -1.0f);
    }
    
    public string ToStringNormalized()
    {
        return $"Normal: ({SNORM8(Nx):F3}, {SNORM8(Ny):F3}, {SNORM8(Nz):F3}, {SNORM8(Nw):F3}), " +
               $"Tangent: ({SNORM8(Tx):F3}, {SNORM8(Ty):F3}, {SNORM8(Tz):F3}, {SNORM8(Tw):F3})";
    }
    
    public System.Numerics.Vector3 GetNormal()
    {
        return new System.Numerics.Vector3(SNORM8(Nx), SNORM8(Ny), SNORM8(Nz));
    }
    
    public System.Numerics.Vector4 GetTangent()
    {
        return new System.Numerics.Vector4(SNORM8(Tx), SNORM8(Ty), SNORM8(Tz), SNORM8(Tw));
    }
}