#include "common.hlsli"
#include "deferredCommon.hlsli"

struct AppData {
    float3 position: POSITION;
    float3 normal: NORMAL;
    float3 tangent: TANGENT;
    float3 bitangent: BINORMAL;
    float2 texcoord: TEXCOORD;
};

VertToPixel main(AppData i)
{
    //float2 positions[3] = {
    //    float2(-0.5, -0.5),
    //    float2(0.0, 0.5),
    //    float2(0.5, -0.5)
    //};

    //float3 colors[3] = {
    //    float3(1.0, 0.0, 0.0),
    //    float3(0.0, 0.0, 1.0),
    //    float3(0.0, 1.0, 0.0)
    //};

    //float3 normal = mul(g_view, float3(0.0f, 0.0f, -1.0f));

    // TODO WT: Need to be multiplied by model matrix

    VertToPixel o;
    o.positionW = float4(i.position, 1.0f);
    o.position = mul(g_viewProj, o.positionW);
    o.color = float3(1.0f, 1.0f, 1.0f);
    o.normalV = mul(g_view, float4(i.normal, 1.0)).xyz;
    o.normalW = i.normal;
    o.texcoord = i.texcoord;
    o.normal = i.normal;
    o.tangent = i.tangent;
    o.bitangent = i.bitangent;

	return o;
}