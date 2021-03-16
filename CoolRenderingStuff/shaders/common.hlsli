cbuffer PerFrameUniforms: register(b0) {
	float2 g_screenDimensions;
	float2 pad;
	float3 g_viewPosition;
	float pad2;
	float4x4 g_view;
	float4x4 g_viewProj;
};

float Lambert(float3 toLight, float3 normal, float distanceSquared) {
	float lambert = saturate(dot(normal, toLight));

	float atten = lambert / distanceSquared;

	return atten;
}