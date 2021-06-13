#pragma once

#include <string>
#include <d3d11.h>
#include <stdexcept>

struct Texture {
	std::string name;
	ID3D11Texture2D* texture;
	ID3D11ShaderResourceView* textureSRV;
	ID3D11SamplerState* sampler;

	Texture(ID3D11Device* device, ID3D11DeviceContext* context, std::string name, int width, int height, int bpp, unsigned char* data);

	~Texture();
};