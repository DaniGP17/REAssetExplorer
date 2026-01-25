namespace REAssetExplorer.Core.Common;

public class BoneWeight
{
    public readonly uint[] boneIndices;
    public readonly float[] boneWeights;

    public BoneWeight(ulong indices, ulong weights)
    {
        boneIndices = new uint[8];
        boneWeights = new float[8];

        for (int i = 0; i < 8; i++)
        {
            boneIndices[i] = (uint)((indices >> (i * 8)) & 0xFF);

            byte w = (byte)((weights >> (i * 8)) & 0xFF);
            boneWeights[i] = w / 255.0f;
        }
    }
    
    public override string ToString()
        => $"Bone Indices: [{string.Join(", ", boneIndices)}], " +
           $"Bone Weights: [{string.Join(", ", boneWeights.Select(w => w.ToString("0.######")))}]";
}