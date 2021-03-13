struct VertToPixel {
	float4 position: SV_POSITION;
	float4 positionW: POSITION;
	float3 normalV: NORMAL0;
	float3 normalW: NORMAL1;
	float3 color: COLOR;
	float2 texcoord: TEXCOORD0;

	float3 normal: NORMAL2;
	float3 tangent: TANGENT;
	float3 bitangent: BINORMAL;
};

cbuffer MaterialSettings: register(b1) {
	int g_matUseDiffuse;
	int g_matUseNormal;
	int __g_matPad[2];
}