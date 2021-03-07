struct VertToPixel {
	float4 position: SV_POSITION;
	float3 color: COLOR;
};

cbuffer PerFrameUniforms: register(b0) {
	float4x4 g_view;
	float4x4 g_viewProj;
};