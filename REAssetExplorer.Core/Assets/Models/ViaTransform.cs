using System.Numerics;
using REAssetExplorer.Core.Rsz;

namespace REAssetExplorer.Core.Assets.Models;

/// <summary>Typed representation of the via.Transform RSZ component.</summary>
public class ViaTransform
{
    public Vector3    Position { get; set; } = Vector3.Zero;
    public Quaternion Rotation { get; set; } = Quaternion.Identity;
    public Vector3    Scale    { get; set; } = Vector3.One;

    public static ViaTransform Parse(RszClass rszClass)
    {
        return new ViaTransform
        {
            Position = ReadVec3(rszClass, "v0"),
            Rotation = ReadQuat(rszClass, "v1"),
            Scale    = ReadVec3(rszClass, "v2"),
        };
    }

    private static Vector3 ReadVec3(RszClass c, string name)
    {
        // Typed path: the RSZ DB resolved the field to RszVec3.
        if (c.TryGet<RszVec3>(name, out var v3))
            return new Vector3(v3.X, v3.Y, v3.Z);

        // Raw-bytes path: the RSZ DB stored the field as Data (byte[]).
        // RE Engine stores vec3 as 16 bytes (3 floats + 4-byte padding).
        if (c.TryGet<byte[]>(name, out var raw) && raw?.Length >= 12)
            return new Vector3(
                BitConverter.ToSingle(raw, 0),
                BitConverter.ToSingle(raw, 4),
                BitConverter.ToSingle(raw, 8));

        return Vector3.Zero;
    }

    private static Quaternion ReadQuat(RszClass c, string name)
    {
        // Typed path.
        if (c.TryGet<RszQuaternion>(name, out var q))
            return new Quaternion(q.X, q.Y, q.Z, q.W);

        // Raw-bytes path: quaternion = 16 bytes.
        if (c.TryGet<byte[]>(name, out var raw) && raw?.Length >= 16)
            return new Quaternion(
                BitConverter.ToSingle(raw, 0),
                BitConverter.ToSingle(raw, 4),
                BitConverter.ToSingle(raw, 8),
                BitConverter.ToSingle(raw, 12));

        return Quaternion.Identity;
    }
}
