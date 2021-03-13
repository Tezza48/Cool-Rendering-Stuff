struct VertToPixel {
	float4 positionH: SV_POSITION;
	float3 positionV: POSITION0;
	float3 positionW: POSITION1;
};

cbuffer Light: register(b1) {
	float3 g_lightPosition;
	float g_lightRadius;

	float3 g_lightColor;
	float g_lightIntensity;

	float4 g_lightAmbient;
};