#include "common.hlsli"
#include "lightAccCommon.hlsli"

SamplerState defaultSampler;

texture2D inputFrame : register(t0);

float4 main(VertToPixel i) : SV_TARGET
{
	float2 uv = i.positionH.xy / g_screenDimensions;

	return float4(inputFrame.Sample(defaultSampler, uv));
}