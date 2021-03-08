#include "GraphicsPipeline.h"

GraphicsPipeline::GraphicsPipeline(
	ID3D11Device* device, 
	std::vector<char> vertexShaderCode, 
	std::vector<char> pixelShaderCode, 
	D3D11_RASTERIZER_DESC rasterizer, 
	D3D11_PRIMITIVE_TOPOLOGY primitiveTopology, 
	D3D11_VIEWPORT viewport, 
	D3D11_RECT scissor)
{
	device->CreateVertexShader(vertexShaderCode.data(), vertexShaderCode.size(), nullptr, &vertexShader);
	device->CreatePixelShader(pixelShaderCode.data(), pixelShaderCode.size(), nullptr, &pixelShader);

	device->CreateRasterizerState(&rasterizer, &rasterizerState);

	this->primitiveTopology = primitiveTopology;

	this->viewport = viewport;
	this->scissor = scissor;
}

GraphicsPipeline::~GraphicsPipeline()
{
	vertexShader->Release();
	rasterizerState->Release();
	pixelShader->Release();
}

void GraphicsPipeline::bind(ID3D11DeviceContext* context)
{
	context->IASetPrimitiveTopology(primitiveTopology);

	if (vertexShader)
		context->VSSetShader(vertexShader, nullptr, 0);

	context->RSSetState(rasterizerState);
	context->RSSetViewports(1, &viewport);
	context->RSSetScissorRects(1, &scissor);

	if (pixelShader)
		context->PSSetShader(pixelShader, nullptr, 0);
}
