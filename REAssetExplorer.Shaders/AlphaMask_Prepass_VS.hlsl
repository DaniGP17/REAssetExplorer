// Vertex Shader para Alpha Mask Pre-pass (Depth-Only)
// Este shader solo transforma las posiciones, sin procesar otros atributos

cbuffer ObjectInfo : register(b0)
{
    float4x4 worldMat;              // Matriz world
    float4x4 prevWorldMat;          // Matriz world del frame anterior
    float    AlphaRef;              // Referencia para alpha test
    float3   _objInfoPad;           // Padding
};

cbuffer SceneInfo : register(b1)
{
    float4x4 viewProjMat;           // Matriz view-projection
    float4x4 viewMat;               // Matriz view
    float4x4 viewInvMat;            // Inversa de view
    float4x4 projMat;               // Matriz projection
    float4x4 projInvMat;            // Inversa de projection
    float4x4 viewProjInvMat;        // Inversa de view-projection
    float4x4 prevViewProjMat;       // View-projection del frame anterior
    float3   prevCameraPos;         // Posición de cámara anterior
    float    cameraNearPlane;       // Plano cercano de la cámara
    float3   prevCameraDir;         // Dirección de cámara anterior
    float    cameraFarPlane;        // Plano lejano de la cámara
    float4   viewFrustum[6];        // Planos del frustum (6 planos)
    float3   ZToLinear;             // Parámetros para convertir Z a lineal
    float    subdivisionLevel;      // Nivel de subdivisión (LOD)
    float2   screenSize;            // Tamaño de pantalla en píxeles
    float2   screenInverseSize;     // Inversa del tamaño (1/width, 1/height)
};

struct InputStruct
{
    float3 position  : POSITION;
    float3 normal    : NORMAL;
    float4 tangent   : TANGENT;
    float2 texcoord0 : TEXCOORD0;
    float2 texcoord1 : TEXCOORD1;
};

struct OutputStruct
{
    float4 Position : SV_Position;
    float2 UV       : TEXCOORD0;
};

OutputStruct main(in InputStruct IN)
{
    OutputStruct OUT;
    
    // Transformar posición a world space
    float4 worldPos = mul(float4(IN.position, 1.0), worldMat);
    
    // Transformar a clip space
    OUT.Position = mul(worldPos, viewProjMat);
    
    // Pasar UV para samplear alpha en pixel shader
    OUT.UV = IN.texcoord0;
    
    return OUT;
}
