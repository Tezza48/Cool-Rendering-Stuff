#include "Texture.h"
Texture::Texture(ID3D11Device* device, ID3D11DeviceContext* context, std::string name, int width, int height, int bpp, unsigned char* data) : name(name) {
	auto format = DXGI_FORMAT_R8G8B8A8_UNORM;

	D3D11_TEXTURE2D_DESC desc;
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 0;
	desc.ArraySize = 1;
	desc.Format = format;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	desc.CPUAccessFlags = 0;
	desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

	auto hr = device->CreateTexture2D(&desc, nullptr, &texture);
	if (FAILED(hr)) {
		throw std::runtime_error("Failed to create texture2D!");
	}

	texture->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)name.size(), name.c_str());

	context->UpdateSubresource(texture, 0, NULL, data, width * 4 * sizeof(unsigned char), 0);

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = -1;
	srvDesc.Texture2D.MostDetailedMip = 0;

	hr = device->CreateShaderResourceView(texture, &srvDesc, &textureSRV);
	if (FAILED(hr)) {
		throw std::runtime_error("Failed to create SRV to texture2D!");
	}

	context->GenerateMips(textureSRV);

	D3D11_SAMPLER_DESC samplerDesc{};
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.MipLODBias = 0.0f;
	samplerDesc.MaxAnisotropy = 1;
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
	samplerDesc.BorderColor[0] = 0.0f;
	samplerDesc.BorderColor[1] = 0.0f;
	samplerDesc.BorderColor[2] = 0.0f;
	samplerDesc.BorderColor[3] = 0.0f;
	samplerDesc.MinLOD = 0.0f;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

	if (FAILED(device->CreateSamplerState(&samplerDesc, &sampler))) {
		throw std::runtime_error("Failed to create gbuffer sampler");
	}
}

Texture::~Texture() {
	textureSRV->Release();
	texture->Release();
	sampler->Release();
}
