#include "common.hlsli"
#include "lightAccCommon.hlsli"

SamplerState defaultSampler;

texture2D positionTexture : register(t0);
texture2D normalTexture : register(t1);
texture2D albedoTexture : register(t2);

float4 main(VertToPixel i) : SV_TARGET
{
	float2 uv = i.positionH.xy / g_screenDimensions;

	//return float4(0.1, 0.1, 0.1, 1.0);

	//return float4(uv, 0.0, 1.0);

	float4 positionW = positionTexture.Sample(defaultSampler, uv);
	float4 normal = normalTexture.Sample(defaultSampler, uv);
	float4 albedo = albedoTexture.Sample(defaultSampler, uv);

	float3 positionV = mul(g_view, positionW).xyz;

	float3 lightPosV = mul(g_view, g_lightPosition); // TODO WT: Do this in VSHADER or on CPU.

	float3 toLight = lightPosV - positionV;
	float dist = length(toLight);

	float lambert = Lambert(toLight, normal.xyz, dist);

	float linearFalloff = g_lightIntensity / dist;

	float attenuated = lambert / (dist * dist);

	float3 color = albedo.rgb * g_lightColor * attenuated * g_lightIntensity;

	return float4(color, 1.0);
}