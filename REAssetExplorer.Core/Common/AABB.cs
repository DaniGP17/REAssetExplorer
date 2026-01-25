using System;
using System.IO;
using System.Runtime.InteropServices;

namespace REAssetExplorer.Core.Common;

public struct AABB
{
    public Vector3 Min;
    public Vector3 Max;

    public AABB(BinaryReader reader)
    {
        Min = new Vector3(reader);
        Max = new Vector3(reader);
    }
    
    public AABB(Vector3 min, Vector3 max)
    {
        Min = min;
        Max = max;
    }

    public bool IsEmpty =>
        Min.X > Max.X || Min.Y > Max.Y || Min.Z > Max.Z;

    public Vector3 Center =>
        (Min + Max) * 0.5f;

    public Vector3 Size =>
        Max - Min;

    public bool Contains(Vector3 p) =>
        p.X >= Min.X && p.Y >= Min.Y && p.Z >= Min.Z &&
        p.X <= Max.X && p.Y <= Max.Y && p.Z <= Max.Z;

    public override string ToString()
        => $"AABB(Min={Min}, Max={Max})";
}