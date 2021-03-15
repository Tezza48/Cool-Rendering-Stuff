#include "common.hlsli"
#include "lightAccCommon.hlsli"

SamplerState defaultSampler;

texture2D positionTexture : register(t0);
texture2D normalTexture : register(t1);
texture2D albedoTexture : register(t2);

float4 main(VertToPixel i) : SV_TARGET
{
	//float2 uv = i.positionH.xy / g_screenDimensions;

	////return float4(0.1, 0.1, 0.1, 1.0);

	////return float4(uv, 0.0, 1.0);

	//float4 positionW = positionTexture.Sample(defaultSampler, uv);
	//float4 normal = normalTexture.Sample(defaultSampler, uv);
	//float4 albedo = albedoTexture.Sample(defaultSampler, uv);

	//float3 positionV = mul(g_view, positionW).xyz;

	//float3 lightPosV = mul(g_view, g_lightPosition); // TODO WT: Do this in VSHADER or on CPU.

	//float3 toLight = lightPosV - positionV;
	//float dist = length(toLight);

	//float lambert = Lambert(toLight, normal.xyz, dist);

	//float3 color = albedo.rgb * g_lightColor * lambert * g_lightIntensity;

	//color += albedo.rgb * g_lightAmbient.rgb;

	//return float4(color, 1.0);

	// World space lighting
	float2 uv = i.positionH.xy / g_screenDimensions;

	float4 positionW = positionTexture.Sample(defaultSampler, uv);
	float4 normal = normalTexture.Sample(defaultSampler, uv);
	float4 albedo = albedoTexture.Sample(defaultSampler, uv);

	float3 toLight = g_lightPosition - positionW;
	float dist = length(toLight);

	//float nDotL = dot(toLight, normal.xyz);
	//float power = 1.0 - min(dist, g_lightRadius) / g_lightRadius;

	float lambert = Lambert(toLight, normal.xyz, dist);

	float3 color = albedo.rgb * g_lightColor * lambert * g_lightIntensity;

	color += albedo.rgb * g_lightAmbient.rgb;

	//float percent = dist / g_lightRadius;

	return float4(color, 1.0);
}