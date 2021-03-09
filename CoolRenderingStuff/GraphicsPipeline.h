#pragma once
#include <d3d11.h>
#include <vector>
#include <optional>

struct GraphicsPipeline
{
	D3D_PRIMITIVE_TOPOLOGY primitiveTopology;

	ID3D11InputLayout* inputLayout;

	ID3D11VertexShader* vertexShader;
	ID3D11RasterizerState* rasterizerState;
	ID3D11DepthStencilState* depthStencilState;
	ID3D11PixelShader* pixelShader;

	D3D11_VIEWPORT viewport;
	D3D11_RECT scissor;

	GraphicsPipeline(
		ID3D11Device* device,
		std::vector<char> vertexShaderCode,
		std::vector<char> pixelShaderCode,
		std::optional<std::vector<D3D11_INPUT_ELEMENT_DESC>> inputElementDescs,
		D3D11_RASTERIZER_DESC rasterizer,
		D3D11_DEPTH_STENCIL_DESC depthStencil,
		D3D11_PRIMITIVE_TOPOLOGY primitiveTopology,
		D3D11_VIEWPORT viewport,
		D3D11_RECT scissor = { 0 }
	);

	~GraphicsPipeline();

	void bind(ID3D11DeviceContext* context);
};

