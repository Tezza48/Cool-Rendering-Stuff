#include "lightAccCommon.hlsli"

SamplerState defaultSampler;

texture2D positionTexture : register(t0);
texture2D normalTexture : register(t1);
texture2D albedoTexture : register(t2);

float4 main(VertToPixel i) : SV_TARGET
{
	float3 normal = normalTexture.Sample(defaultSampler, i.uv).xyz;
	float nDotL = dot(normal, normalize(float3(1.0f, 1.0f, 1.0f)));
	return float4(nDotL, nDotL, nDotL, 1.0);
}