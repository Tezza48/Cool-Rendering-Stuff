#pragma once
#include <d3d11.h>
#include <vector>
#include <optional>

#include <unordered_map>
#include <string>
#include "MiscUtility.h"
#include <d3dcompiler.h>
#include <iostream>

struct Shader {
	std::string vertexPath;
	std::vector<char> initVertexSource;
	ID3D11VertexShader* vertexShader;
	std::string pixelPath;
	ID3D11PixelShader* pixelShader;

	Shader(std::string vertexPath, std::vector<char> vertexSource, ID3D11VertexShader* vertexShader, std::string pixelPath, ID3D11PixelShader* pixelShader)
		: vertexPath(vertexPath), initVertexSource(vertexSource), vertexShader(vertexShader), pixelPath(pixelPath), pixelShader(pixelShader) {}

	Shader(Shader&) = delete;

	~Shader() {
		std::cout << "Releasing shader " << vertexPath << "\n";
		vertexShader->Release();
		std::cout << "Releasing shader " << pixelPath << "\n";
		pixelShader->Release();
	}
};

struct ShaderManager {
	std::unordered_map<std::string, Shader*> cache;

public:
	~ShaderManager() {
		for (auto& [_, shader] : cache) {
			delete shader;
			shader = nullptr;
		}
	}

	Shader* registerShader(ID3D11Device* device, const std::string& name, const std::string& vertexPath, const std::string& pixelPath) {
		ID3D11VertexShader* vShader;
		ID3D11PixelShader* pShader;
		auto vSource = readFile(vertexPath + ".cso");
		auto pSource = readFile(pixelPath + ".cso");
		device->CreateVertexShader(vSource.data(), vSource.size(), nullptr, &vShader);
		device->CreatePixelShader(pSource.data(), pSource.size(), nullptr, &pShader);

		cache[name] = new Shader(
			vertexPath,
			vSource,
			vShader,
			pixelPath,
			pShader
		);

		return cache.at(name);
	}

	void recompile(ID3D11Device* device) {
		ID3D10Blob* bytecode;
		ID3D10Blob* errors;

		std::cout << "Starting shader recompile\n";

		for (auto& [name, shader] : cache) {
			ID3D11VertexShader* newVertexShader = nullptr;
			ID3D11PixelShader* newPixelShader = nullptr;

			bool success = true;

			// Graphics
			auto vPath = shader->vertexPath + ".hlsl";
			auto wvPath = std::wstring(vPath.begin(), vPath.end());
			if (FAILED(D3DCompileFromFile(wvPath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "vs_5_0", 0, 0, &bytecode, &errors))) {
				std::wcout << wvPath << L" error " << (char*)errors->GetBufferPointer() << std::endl;
				success = false;
				if (errors)
					errors->Release();
				return;
			}

			device->CreateVertexShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &newVertexShader);
			if (errors)
				errors->Release();
			bytecode->Release();

			auto pPath = shader->pixelPath + ".hlsl";
			auto wpPath = std::wstring(pPath.begin(), pPath.end());
			if (FAILED(D3DCompileFromFile(wpPath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "ps_5_0", 0, 0, &bytecode, &errors))) {
				std::wcout << wpPath << L" error " << (char*)errors->GetBufferPointer() << std::endl;
				success = false;
				if (errors)
					errors->Release();
				return;
			}

			device->CreatePixelShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &newPixelShader);
			if (errors)
				errors->Release();
			bytecode->Release();

			if (success && newVertexShader && newPixelShader) {
				shader->vertexShader->Release();
				shader->pixelShader->Release();
				shader->vertexShader = newVertexShader;
				shader->pixelShader = newPixelShader;

				std::cout << "Successfully hot reloaded " << name << " Shaders\n";
			}

		}

		std::cout << "Shader recompile complete" << std::endl;
	}
};

struct GraphicsPipeline
{
	D3D_PRIMITIVE_TOPOLOGY primitiveTopology;

	ID3D11InputLayout* inputLayout;

	Shader* shaderRef;
	ID3D11RasterizerState* rasterizerState;
	ID3D11DepthStencilState* depthStencilState;
	ID3D11BlendState* blendState;

	D3D11_VIEWPORT viewport;
	D3D11_RECT scissor;

	GraphicsPipeline(
		ID3D11Device* device,
		Shader* shader,
		std::optional<std::vector<D3D11_INPUT_ELEMENT_DESC>> inputElementDescs,
		D3D11_RASTERIZER_DESC rasterizer,
		D3D11_DEPTH_STENCIL_DESC depthStencil,
		D3D11_BLEND_DESC blend,
		D3D11_PRIMITIVE_TOPOLOGY primitiveTopology,
		D3D11_VIEWPORT viewport,
		D3D11_RECT scissor = { 0 }
	);

	~GraphicsPipeline();

	void bind(ID3D11DeviceContext* context);
};

