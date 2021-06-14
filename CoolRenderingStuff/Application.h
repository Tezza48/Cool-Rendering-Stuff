#pragma once
#include <vector>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <fstream>
#include "GraphicsPipeline.h"

#include "ECS.h"
#include "Lighting.h"
#include <GLFW\glfw3.h>

#include <iostream>
#include <iomanip>

#include "vendor\stb\stb_image.h"
#include <filesystem>

#include <assimp\material.h>
#include <assimp\mesh.h>

// TODO WT: This should be resource
struct PerFrameUniforms {
	DirectX::XMFLOAT2 screenDimensions;
	float pad[2];
	DirectX::XMFLOAT3 eyePos;
	float pad3;
	DirectX::XMMATRIX view;
	DirectX::XMMATRIX viewProj;
};

struct GeometryBuffer {
	enum Buffer {
		POSITION,
		NORMAL,
		ALBEDO,
		SPECULAR,
		MAX_BUFFER,
	};

	const DXGI_FORMAT formats[MAX_BUFFER] = {
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		DXGI_FORMAT_R16G16B16A16_SNORM,
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
		DXGI_FORMAT_R16G16B16A16_FLOAT,
	};

	ID3D11Texture2D* textures[MAX_BUFFER];
	ID3D11RenderTargetView* textureViews[MAX_BUFFER];
	ID3D11ShaderResourceView* textureResourceViews[MAX_BUFFER];

	ID3D11SamplerState* sampler;

	~GeometryBuffer() {
		sampler->Release();

		for (size_t i = 0; i < MAX_BUFFER; i++) {
			if (textureResourceViews[i])
				textureResourceViews[i]->Release();

			if (textureViews[i])
				textureViews[i]->Release();

			if (textures[i])
				textures[i]->Release();
		}
	}

	void bindBuffers(ID3D11DeviceContext* context, ID3D11DepthStencilView* depthStencilView = nullptr) {
		context->OMSetRenderTargets(MAX_BUFFER, textureViews, depthStencilView);
	}
};

struct Vertex {
	DirectX::XMFLOAT3 position;
	DirectX::XMFLOAT3 normal;
	DirectX::XMFLOAT3 tangent;
	DirectX::XMFLOAT3 bitangent;
	DirectX::XMFLOAT2 texcoord;
};

struct Application {
	static std::vector<char> readFile(const std::string& filename);

	ID3D11Buffer* perFrameUniformsBuffer;
	PerFrameUniforms perFrameUniforms;

	ID3D11Buffer* perMaterialUniformsBuffer;

	GeometryBuffer geometryBuffers;

	void Init(ECS* ecs);

	void createConstantBuffers(ECS* ecs);

	void createGbuffers(ECS* ecs);

	void loadModel(ECS* ecs);

	void loadTexture(ECS* ecs, aiTextureType type, aiMaterial* materialData, const std::string& baseAssetPath, std::string& outPath, bool& outEnabled);

	void processVertices(const aiMesh* meshData, std::vector<Vertex>& vertices);

	void processIndices(const aiMesh* meshData, std::vector<uint32_t>& indices);

	// TODO WT: make events handleable in ecs
	void RecompileShaders(ECS* ecs);

	void OnWindowResized(ECS* ecs, uint32_t width, uint32_t height);
};