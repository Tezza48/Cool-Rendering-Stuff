#pragma once
#include <d3d11.h>
#include <cstdint>
#include <DirectXMath.h>
#include <dxgi.h>

struct Light {
	DirectX::XMFLOAT3 position;
	float radius;

	DirectX::XMFLOAT3 color;
	float intensity;
	Light() {}
	Light(DirectX::XMFLOAT3 pos, float rad, DirectX::XMFLOAT3 col, float inten) : position(pos), radius(rad), color(col), intensity(inten) {}
};

class Lighting
{
	struct Mesh {
		ID3D11Buffer* vBuffer;
		ID3D11Buffer* iBuffer;
		uint32_t numindices;

		~Mesh() {
			vBuffer->Release();
			iBuffer->Release();
		}
	};

	Mesh sphereMesh;

	ID3D11Buffer* lightConstantBuffer;

public:
	Lighting(ID3D11Device* device);

	void DrawPointLight(ID3D11DeviceContext* context, Light& light);
};

