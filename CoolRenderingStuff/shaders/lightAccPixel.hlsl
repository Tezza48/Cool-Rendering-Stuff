#include "lightAccCommon.hlsli"

SamplerState defaultSampler;

texture2D positionTexture : register(t0);
texture2D normalTexture : register(t1);
texture2D albedoTexture : register(t2);

float4 main(VertToPixel i) : SV_TARGET
{
	return albedoTexture.Sample(defaultSampler, i.uv);
}