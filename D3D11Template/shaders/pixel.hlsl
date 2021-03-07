#include "common.hlsli"

float4 main(VertToPixel i) : SV_TARGET
{
	return float4(i.color, 1.0);
}