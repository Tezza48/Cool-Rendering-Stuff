#include "common.hlsli"

struct GBuffers {
	float4 position: SV_TARGET0;
	float4 normal: SV_TARGET1;
	float4 albedo: SV_TARGET2;
};

GBuffers main(VertToPixel i)
{
	GBuffers o;
	o.position = i.position;
	o.normal = float4(i.normalV, 0.0);
	o.albedo = float4(i.color, 0.0);

	return o;
}