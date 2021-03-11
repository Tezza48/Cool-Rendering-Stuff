#include "common.hlsli"
#include "lightAccCommon.hlsli"

struct AppData {
    float3 position: POSITION;
};

VertToPixel main(AppData i)
{
    float4 posW = float4((i.position * g_lightRadius * 2.0) + g_lightPosition, 1.0);
    //float4 posW = float4(i.position, 1.0);
    VertToPixel o;
    o.positionH = mul(g_viewProj, posW);
    o.positionV = mul(g_view, posW).xyz;
    o.positionW = posW;

    return o;
}