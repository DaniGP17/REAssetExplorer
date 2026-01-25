namespace REAssetExplorer.Core.Common;

public class Vector2
{
    public float X { get; set; }
    public float Y { get; set; }

    public Vector2()
    {
        X = 0;
        Y = 0;
    }

    public Vector2(float x, float y)
    {
        X = x;
        Y = y;
    }

    public float Magnitude()
    {
        return (float)Math.Sqrt(X * X + Y * Y);
    }

    public override string ToString()
    {
        return $"Vector2({X}, {Y})";
    }

    public static Vector2 operator +(Vector2 a, Vector2 b)
    {
        return new Vector2(a.X + b.X, a.Y + b.Y);
    }

    public static Vector2 operator -(Vector2 a, Vector2 b)
    {
        return new Vector2(a.X - b.X, a.Y - b.Y);
    }

    public static Vector2 operator *(Vector2 a, float scalar)
    {
        return new Vector2(a.X * scalar, a.Y * scalar);
    }

    public static Vector2 operator /(Vector2 a, float scalar)
    {
        if (scalar == 0)
            throw new DivideByZeroException("Scalar can't be 0.");

        return new Vector2(a.X / scalar, a.Y / scalar);
    }

    public static bool operator ==(Vector2 a, Vector2 b)
    {
        return a.X == b.X && a.Y == b.Y;
    }

    public static bool operator !=(Vector2 a, Vector2 b)
    {
        return !(a == b);
    }

    public override bool Equals(object? obj)
    {
        if (obj is Vector3 other)
        {
            return X == other.X && Y == other.Y;
        }
        return false;
    }

    public override int GetHashCode()
    {
        return HashCode.Combine(X, Y);
    }
}