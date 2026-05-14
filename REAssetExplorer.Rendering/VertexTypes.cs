using System.Runtime.InteropServices;
using System.Numerics;

namespace REAssetExplorer.Rendering;

[StructLayout(LayoutKind.Sequential)]
public struct VertexPosition
{
    public Vector3 Position;
    public Vector4 Normal;
    public Vector4 Tangent;
    public Vector2 TexCoord;
    public Vector2 TexCoord2;

    public VertexPosition(Vector3 position, Vector4 normal, Vector4 tangent, Vector2 texCoord, Vector2 texCoord2)
    {
        Position = position;
        Normal = normal;
        Tangent = tangent;
        TexCoord = texCoord;
        TexCoord2 = texCoord2;
    }

    public static int SizeInBytes => Marshal.SizeOf<VertexPosition>();
}
