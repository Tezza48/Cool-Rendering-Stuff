#include "Lighting.h"

using namespace DirectX;

Lighting::Lighting(ID3D11Device* device)
{
	// Unit cube;
	{
		XMFLOAT3 positions[] = {
			{-1.0f, -1.0f,  1.0f},
			{-1.0f, -1.0f, -1.0f},
			{ 1.0f, -1.0f, -1.0f},
			{ 1.0f, -1.0f,  1.0f},
			{-1.0f,  1.0f, -1.0f},
			{-1.0f,  1.0f,  1.0f},
			{ 1.0f,  1.0f,  1.0f},
			{ 1.0f,  1.0f, -1.0f}
		};

		D3D11_BUFFER_DESC vDesc{};
		vDesc.Usage = D3D11_USAGE_DEFAULT;
		vDesc.ByteWidth = sizeof(XMFLOAT3) * 8;
		vDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vDesc.CPUAccessFlags = 0;
		vDesc.MiscFlags = 0;
		vDesc.StructureByteStride = 0;

		D3D11_SUBRESOURCE_DATA vData{};
		vData.pSysMem = positions;

		device->CreateBuffer(&vDesc, &vData, &sphereMesh.vBuffer);
	}

	{
		sphereMesh.numindices = 36;

		uint32_t indices[] = {
			0, 1, 2, 0, 2, 3,
			4, 5, 6, 4, 6, 7,

			0, 5, 4, 0, 4, 1,
			1, 4, 7, 1, 7, 2,
			2, 7, 6, 2, 6, 3,
			3, 6, 5, 3, 5, 0,
		};

		D3D11_BUFFER_DESC iDesc{};
		iDesc.Usage = D3D11_USAGE_DEFAULT;
		iDesc.ByteWidth = sizeof(uint32_t) * sphereMesh.numindices;
		iDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		iDesc.CPUAccessFlags = 0;
		iDesc.MiscFlags = 0;
		iDesc.StructureByteStride = 0;

		D3D11_SUBRESOURCE_DATA iData{};
		iData.pSysMem = indices;

		device->CreateBuffer(&iDesc, &iData, &sphereMesh.iBuffer);
	}

	D3D11_BUFFER_DESC cbDesc{};
	cbDesc.Usage = D3D11_USAGE_DYNAMIC;
	cbDesc.ByteWidth = sizeof(Light);
	cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	cbDesc.MiscFlags = 0;
	cbDesc.StructureByteStride = 0;

	device->CreateBuffer(&cbDesc, nullptr, &lightConstantBuffer);
}

// TODO WT: Take in all lights and draw instanced.
void Lighting::DrawPointLight(ID3D11DeviceContext* context, Light& light)
{
	D3D11_MAPPED_SUBRESOURCE mappedCbuffer{};
	context->Map(lightConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedCbuffer);
	memcpy(mappedCbuffer.pData, &light, sizeof(Light));
	context->Unmap(lightConstantBuffer, 0);

	context->VSSetConstantBuffers(1, 1, &lightConstantBuffer);
	context->PSSetConstantBuffers(1, 1, &lightConstantBuffer);

	uint32_t stride = sizeof(XMFLOAT3);
	uint32_t offset = 0;

	context->IASetVertexBuffers(0, 1, &sphereMesh.vBuffer, &stride, &offset);
	context->IASetIndexBuffer(sphereMesh.iBuffer, DXGI_FORMAT_R32_UINT, 0);
	context->DrawIndexed(sphereMesh.numindices, 0, 0);
	//context->Draw(4, 0);
}
