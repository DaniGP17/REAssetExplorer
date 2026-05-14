namespace REAssetExplorer.Core.Rsz;

public readonly record struct RszVec2(float X, float Y);

/// <summary>via.vec3 — stored as 16 bytes in RE Engine (4th float is padding).</summary>
public readonly record struct RszVec3(float X, float Y, float Z);

public readonly record struct RszVec4(float X, float Y, float Z, float W);

public readonly record struct RszQuaternion(float X, float Y, float Z, float W);

public readonly record struct RszMat4(
    float M00, float M01, float M02, float M03,
    float M10, float M11, float M12, float M13,
    float M20, float M21, float M22, float M23,
    float M30, float M31, float M32, float M33);

public readonly record struct RszColor(byte R, byte G, byte B, byte A);

public readonly record struct RszGuid(Guid Value);

/// <summary>Reference to another instance in the RSZ Classes list.</summary>
public readonly record struct RszObjectRef(uint InstanceIndex);
