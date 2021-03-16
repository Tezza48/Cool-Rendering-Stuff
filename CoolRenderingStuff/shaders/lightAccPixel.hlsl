#include "common.hlsli"
#include "lightAccCommon.hlsli"

SamplerState defaultSampler;

texture2D positionTexture : register(t0);
texture2D normalTexture : register(t1);
texture2D albedoTexture : register(t2);
texture2D specularTexture : register(t3);

float4 main(VertToPixel i) : SV_TARGET
{
	// World space lighting
	float2 uv = i.positionH.xy / g_screenDimensions;

	float4 positionW = positionTexture.Sample(defaultSampler, uv);
	float4 normal = normalTexture.Sample(defaultSampler, uv);
	float4 albedo = albedoTexture.Sample(defaultSampler, uv);
	float4 specular = specularTexture.Sample(defaultSampler, uv);

	float3 toLight = g_lightPosition - positionW;
	float distSquared = dot(toLight, toLight);

	toLight = normalize(toLight);
	float3 toEye = normalize(g_viewPosition - positionW);
	float3 halfwayDir = normalize(toLight + toEye);

	float spec = pow(max(dot(normal, halfwayDir), 0.0), specular.a * 100.0) / distSquared;

	float lambert = Lambert(toLight, normal.xyz, distSquared);

	float3 color = albedo.rgb * g_lightColor * lambert * g_lightIntensity;
	color += specular.rgb * g_lightColor * spec * g_lightIntensity;

	color += albedo.rgb * g_lightAmbient.rgb;

	//float percent = dist / g_lightRadius;

	return float4(color, 1.0);
}