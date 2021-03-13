#include "common.hlsli"
#include "deferredCommon.hlsli"

SamplerState diffuseSampler: register(s0);
texture2D diffuseTexture: register(t0);

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
	o.albedo = diffuseTexture.Sample(diffuseSampler, i.texcoord);
	if (o.albedo.a < 0.5) {
		discard;
	}

	return o;
}