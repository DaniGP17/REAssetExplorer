// Deferred Lighting Pass Vertex Shader
// Generates a fullscreen triangle without vertex buffers

struct VS_OUTPUT
{
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

VS_OUTPUT main(uint vertexID : SV_VertexID)
{
    VS_OUTPUT output;
    
    // Generate fullscreen triangle
    // Vertex 0: (-1, -1) -> UV (0, 1)
    // Vertex 1: (-1,  3) -> UV (0, -1)
    // Vertex 2: ( 3, -1) -> UV (2, 1)
    float2 texCoord = float2((vertexID << 1) & 2, vertexID & 2);
    output.position = float4(texCoord * float2(2, -2) + float2(-1, 1), 0, 1);
    output.texCoord = texCoord;
    
    return output;
}
