// Unlit Pass Vertex Shader
// Simple fullscreen quad vertex shader

struct VS_OUTPUT
{
    float4 Position : SV_Position;
    float2 TexCoord : TEXCOORD0;
};

VS_OUTPUT main(uint vertexID : SV_VertexID)
{
    VS_OUTPUT output;
    
    // Generate fullscreen triangle
    // VertexID: 0, 1, 2
    // Position: (-1, -1), (3, -1), (-1, 3)
    // TexCoord: (0, 1), (2, 1), (0, -1)
    
    output.TexCoord = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(output.TexCoord * float2(2, -2) + float2(-1, 1), 0, 1);
    
    return output;
}
