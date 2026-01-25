namespace REAssetExplorer.Core.Common;

public class Matrix4x4
{
    public readonly float[,] M = new float[4, 4];

    public Matrix4x4(byte[] data)
    {
        if (data.Length != 64)
            throw new ArgumentException("Matrix data must be exactly 64 bytes.");

        for (int i = 0; i < 16; i++)
        {
            float value = BitConverter.ToSingle(data, i * 4);
            M[i / 4, i % 4] = value;
        }
    }

    public override string ToString()
    {
        return
            $"{M[0,0],8:0.###} {M[0,1],8:0.###} {M[0,2],8:0.###} {M[0,3],8:0.###}\n" +
            $"{M[1,0],8:0.###} {M[1,1],8:0.###} {M[1,2],8:0.###} {M[1,3],8:0.###}\n" +
            $"{M[2,0],8:0.###} {M[2,1],8:0.###} {M[2,2],8:0.###} {M[2,3],8:0.###}\n" +
            $"{M[3,0],8:0.###} {M[3,1],8:0.###} {M[3,2],8:0.###} {M[3,3],8:0.###}";
    }
}