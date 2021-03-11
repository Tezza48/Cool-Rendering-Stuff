#include "common.hlsli"
#include "deferredCommon.hlsli"

struct GBuffers {
	float4 position: SV_TARGET0;
	float4 normal: SV_TARGET1;
	float4 albedo: SV_TARGET2;
};

GBuffers main(VertToPixel i)
{
	GBuffers o;
	o.position = i.positionW;
	o.normal = float4(normalize(i.normalV), 1.0);
	//o.albedo = float4(i.color, 1.0);
	o.albedo = float4(i.color, 1.0);

	return o;
}