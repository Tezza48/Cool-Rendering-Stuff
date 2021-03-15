#include "common.hlsli"
#include "lightAccCommon.hlsli"

struct AppData {
    float3 position: POSITION;
    uint index: SV_VertexID;
};

VertToPixel main(AppData i)
{
    float4 positions[4] = {
        float4(-1.0, -1.0, 0.0, 1.0),
        float4(-1.0, 1.0, 0.0, 1.0),
        float4(1.0, -1.0, 0.0, 1.0),
        float4(1.0, 1.0, 0.0, 1.0),
    };

    float4 posW = positions[i.index];
    VertToPixel o;
    o.positionH = posW;
    o.positionV = posW;
    o.positionW = posW.xyz;

    //float4 posW = float4((i.position * g_lightRadius) + g_lightPosition, 1.0);
    //VertToPixel o;
    //o.positionH = mul(g_viewProj, posW);
    //o.positionV = mul(g_view, posW).xyz;
    //o.positionW = posW.xyz;

    return o;
}