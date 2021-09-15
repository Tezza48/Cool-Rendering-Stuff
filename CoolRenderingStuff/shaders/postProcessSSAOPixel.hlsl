#include "common.hlsli"
#include "lightAccCommon.hlsli"

SamplerState defaultSampler;

texture2D inputFrame : register(t0);

float4 main(VertToPixel i) : SV_TARGET
{
	float2 uv = i.positionH.xy / g_screenDimensions;

	float3 hdrColor = inputFrame.Sample(defaultSampler, uv).rgb;

	const float gamma = 2.2;

	float3 mapped = hdrColor / (hdrColor + float3(1.0, 1.0, 1.0));
	float inverseGamma = 1.0 / gamma;
	//mapped = pow(mapped, float3(inverseGamma, inverseGamma, inverseGamma));

	return float4(mapped, 1.0);
}