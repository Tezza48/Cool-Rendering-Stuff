#include "common.hlsli"

VertToPixel main(uint index: SV_VertexID)
{
    float2 positions[3] = {
        float2(-0.5, -0.5),
        float2(0.0, 0.5),
        float2(0.5, -0.5)
    };

    float3 colors[3] = {
        float3(1.0, 0.0, 0.0),
        float3(0.0, 0.0, 1.0),
        float3(0.0, 1.0, 0.0)
    };

    VertToPixel o;
    o.position = float4(positions[index], 0.0f, 1.0f);
    o.color = colors[index];

	return o;
}