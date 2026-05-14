// Fullscreen triangle (no vertex buffer required). Draw 3 vertices.
float4 main(uint vid : SV_VertexID) : SV_POSITION
{
    float2 ndc;
    ndc.x = (vid == 2) ?  3.0f : -1.0f;
    ndc.y = (vid == 1) ? -3.0f :  1.0f;
    return float4(ndc, 0.0f, 1.0f);
}
