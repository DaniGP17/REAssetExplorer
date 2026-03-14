namespace REAssetExplorer.Core.Common;

public class Vector3
{
    public float X { get; set; }
    public float Y { get; set; }
    public float Z { get; set; }

    public Vector3()
    {
        X = 0;
        Y = 0;
        Z = 0;
    }
    
    public Vector3(BinaryReader reader)
    {
        X = reader.ReadSingle();
        Y = reader.ReadSingle();
        Z = reader.ReadSingle();
    }

    public Vector3(float x, float y, float z)
    {
        X = x;
        Y = y;
        Z = z;
    }
    
    public static Vector3 UnitX => new Vector3(1, 0, 0);
    public static Vector3 UnitY => new Vector3(0, 1, 0);

    public float Magnitude()
    {
        return (float)Math.Sqrt(X * X + Y * Y + Z * Z);
    }

    public Vector3 Normalize()
    {
        float magnitude = Magnitude();
        if (magnitude == 0)
            return new Vector3(0, 0, 0);

        return new Vector3(X / magnitude, Y / magnitude, Z / magnitude);
    }

    public float Dot(Vector3 other)
    {
        return X * other.X + Y * other.Y + Z * other.Z;
    }

    public Vector3 Cross(Vector3 other)
    {
        return new Vector3(
            Y * other.Z - Z * other.Y,
            Z * other.X - X * other.Z,
            X * other.Y - Y * other.X
        );
    }
    
    public float Distance(Vector3 other)
    {
        return (float)Math.Sqrt(
            Math.Pow(other.X - X, 2) +
            Math.Pow(other.Y - Y, 2) +
            Math.Pow(other.Z - Z, 2)
        );
    }
    public override string ToString()
    {
        return $"Vector3({X}, {Y}, {Z})";
    }

    public static Vector3 operator +(Vector3 a, Vector3 b)
    {
        return new Vector3(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
    }

    public static Vector3 operator -(Vector3 a, Vector3 b)
    {
        return new Vector3(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
    }

    public static Vector3 operator *(Vector3 a, float scalar)
    {
        return new Vector3(a.X * scalar, a.Y * scalar, a.Z * scalar);
    }

    public static Vector3 operator /(Vector3 a, float scalar)
    {
        if (scalar == 0)
            throw new DivideByZeroException("Scalar can't be 0.");

        return new Vector3(a.X / scalar, a.Y / scalar, a.Z / scalar);
    }

    public static bool operator ==(Vector3 a, Vector3 b)
    {
        return a.X == b.X && a.Y == b.Y && a.Z == b.Z;
    }

    public static bool operator !=(Vector3 a, Vector3 b)
    {
        return !(a == b);
    }

    public override bool Equals(object? obj)
    {
        if (obj is Vector3 other)
        {
            return X == other.X && Y == other.Y && Z == other.Z;
        }
        return false;
    }

    public override int GetHashCode()
    {
        return HashCode.Combine(X, Y, Z);
    }
    
    public System.Numerics.Vector3 ToNumerics()
    {
        return new System.Numerics.Vector3(X, Y, Z);
    }
}