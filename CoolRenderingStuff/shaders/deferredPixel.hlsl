#include "common.hlsli"
#include "deferredCommon.hlsli"

SamplerState diffuseSampler: register(s0);
texture2D diffuseTexture: register(t0);

SamplerState normalSampler: register(s1);
texture2D normalTexture: register(t1);

struct GBuffers {
	float4 position: SV_TARGET0;
	float4 normal: SV_TARGET1;
	float4 albedo: SV_TARGET2;
};

GBuffers main(VertToPixel i)
{
	float3 T = normalize(i.tangent);
	float3 B = normalize(i.bitangent);
	float3 N = normalize(i.normal);
	float3x3 TBN = float3x3(T, B, N);

	float4 diffuse = diffuseTexture.Sample(diffuseSampler, i.texcoord);
	float3 normalT = normalTexture.Sample(normalSampler, i.texcoord).xyz * 2.0 - 1.0;
	normalT.z = -normalT.z;

	float3 normalW = normalize(mul(TBN, normalT));
	
	GBuffers o;

	o.albedo = (g_matUseDiffuse) ? diffuse : float4(1.0f, 1.0f, 1.0f, 1.0f);

	o.position = i.positionW;
	o.normal = (g_matUseNormal) ? float4(normalW, 1.0) : float4(i.normalW, 1.0);

	//o.normal = o.normal / 2.0 + 0.5;

	if (o.albedo.a < 0.5) {
		discard;
	}
	//o.normal = normalV;
	//o.normal = float4(normalize(i.normalV), 1.0);


	return o;
}