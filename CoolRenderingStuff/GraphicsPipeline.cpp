#include "GraphicsPipeline.h"

GraphicsPipeline::GraphicsPipeline(
	ID3D11Device* device, 
	std::vector<char> vertexShaderCode, 
	std::vector<char> pixelShaderCode,
	std::optional<std::vector<D3D11_INPUT_ELEMENT_DESC>> inputElementDescs,
	D3D11_RASTERIZER_DESC rasterizer,
	D3D11_DEPTH_STENCIL_DESC depthStencil,
	D3D11_BLEND_DESC blend,
	D3D11_PRIMITIVE_TOPOLOGY primitiveTopology, 
	D3D11_VIEWPORT viewport, 
	D3D11_RECT scissor)
{
	device->CreateVertexShader(vertexShaderCode.data(), vertexShaderCode.size(), nullptr, &vertexShader);
	device->CreatePixelShader(pixelShaderCode.data(), pixelShaderCode.size(), nullptr, &pixelShader);

	if (inputElementDescs.has_value())
		device->CreateInputLayout(inputElementDescs.value().data(), inputElementDescs.value().size(), vertexShaderCode.data(), vertexShaderCode.size(), &inputLayout);
	else inputLayout = nullptr;

	device->CreateRasterizerState(&rasterizer, &rasterizerState);

	this->primitiveTopology = primitiveTopology;

	this->viewport = viewport;
	this->scissor = scissor;

	device->CreateDepthStencilState(&depthStencil, &depthStencilState);

	device->CreateBlendState(&blend, &blendState);
}

GraphicsPipeline::~GraphicsPipeline()
{
	if (inputLayout)
		inputLayout->Release();
	vertexShader->Release();
	rasterizerState->Release();
	pixelShader->Release();
	depthStencilState->Release();
	blendState->Release();
}

void GraphicsPipeline::bind(ID3D11DeviceContext* context)
{
	context->IASetPrimitiveTopology(primitiveTopology);

	context->VSSetShader(vertexShader, nullptr, 0);

	// No need to check cos nullptr is valid here.
	context->IASetInputLayout(inputLayout);

	context->RSSetState(rasterizerState);
	context->RSSetViewports(1, &viewport);
	context->RSSetScissorRects(1, &scissor);

	context->PSSetShader(pixelShader, nullptr, 0);

	float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	UINT sampleMask = 0xffffffff;
	context->OMSetBlendState(blendState, blendFactor, sampleMask);
	context->OMSetDepthStencilState(depthStencilState, 0);
}
