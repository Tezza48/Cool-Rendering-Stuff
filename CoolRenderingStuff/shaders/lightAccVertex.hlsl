#include "lightAccCommon.hlsli"

VertToPixel main(uint index: SV_VertexID)
{
    // TODO WT: should be light mesh
    float2 positions[4] = {
        float2(-1.0, -1.0),
        float2(-1.0, 1.0),
        float2(1.0, -1.0),
        float2(1.0, 1.0)
    };

    VertToPixel o;
    o.position = float4(positions[index], 0.0f, 1.0f);
    o.uv = positions[index] * 0.5 + 0.5;
    o.uv.y = 1.0 - o.uv.y;

    return o;
}