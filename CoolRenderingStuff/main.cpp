#define NOMINMAX
#include <d3d11_4.h>
#include <dxgi.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#define STB_IMAGE_IMPLEMENTATION
#include "vendor\stb\stb_image.h"

#include <stdexcept>
#include <iostream>
#include <sstream>
#include <vector>
#include <fstream>
#include <cassert>
#include <iomanip>
#include <thread>
#include <unordered_map>
#include <filesystem>
#include <set>

#include <DirectXMath.h>
#include <DirectXColors.h>
#include <d3dcompiler.h>

#include "GraphicsPipeline.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/ProgressHandler.hpp>

#include "vendor/imgui/imgui.h"
#include "vendor/imgui/imgui_impl_glfw.h"
#include "vendor/imgui/imgui_impl_dx11.h"

#include "Lighting.h"

using namespace DirectX;

#define PI 3.1415927f

class AssimpProgressHandler : public Assimp::ProgressHandler {
	virtual bool Update(float percentage) {
		std::cout << "\rAssimp: " << std::fixed << std::setprecision(1) << percentage * 100.0f << std::defaultfloat << "%\tloaded.";
		return true;
	}
};

struct Vertex {
	XMFLOAT3 position;
	XMFLOAT3 normal;
	XMFLOAT3 tangent;
	XMFLOAT3 bitangent;
	XMFLOAT2 texcoord;
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

	~GeometryBuffer() {
		for (size_t i = 0; i < MAX_BUFFER; i++) {
			if (textureResourceViews[i])
				textureResourceViews[i]->Release();

			if (textureViews[i])
				textureViews[i]->Release();

			if (textures[i])
				textures[i]->Release();
		}
	}

	void bind(ID3D11DeviceContext* context, ID3D11DepthStencilView* depthStencilView = nullptr) {
		context->OMSetRenderTargets(MAX_BUFFER, textureViews, depthStencilView);
	}
};

struct Mesh {
	uint32_t materialId;

	size_t numVertices;
	ID3D11Buffer* vertices;

	uint32_t indexCount;
	ID3D11Buffer* indices;
};

struct MaterialCbuffer {
	// TODO WT: bit flags (though 16 byte alignment makes that redundant right now).
	int useDiffuseTexture = false;
	int useNormalTexture = false;
	int useAlphaCutoutTexture = false;
	int useSpecularTexture = false;
};

struct Material {
	std::string name;
	std::string diffuseTexture;
	std::string normalTexture;
	std::string alphaCutoutTexture;
	std::string specularTexture;
	MaterialCbuffer settings;
};

struct Texture {
	std::string name;
	ID3D11Texture2D* texture;
	ID3D11ShaderResourceView* textureSRV;
	ID3D11SamplerState* sampler;

	Texture(ID3D11Device* device, ID3D11DeviceContext* context, std::string name,  int width, int height, int bpp, unsigned char* data): name(name) {
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

		texture->SetPrivateData(WKPDID_D3DDebugObjectName, name.size(), name.c_str());

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

	~Texture() {
		textureSRV->Release();
		texture->Release();
		sampler->Release();
	}
};

struct PerFrameUniforms {
	XMFLOAT2 screenDimensions;
	float pad[2];
	XMFLOAT3 eyePos;
	float pad3;
	DirectX::XMMATRIX view;
	DirectX::XMMATRIX viewProj;
};

struct ComponentMovesInCircle {
	XMFLOAT3 origin;
	float radius = 1.0f;
};

class Application;

struct ComponentRenderModel {
	std::string path;
};

struct ECS;
bool system_setup_window(ECS* ecs);
bool system_setup_graphics_core(ECS* ecs);
bool system_init_application(ECS* ecs);
bool system_setup_imgui(ECS* ecs);
bool system_start(ECS* ecs);
bool system_flycam(ECS* ecs);
bool system_move_in_circle(ECS* ecs);
bool system_pre_draw(ECS* ecs) {
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	return true;
}
bool system_draw_imgui_debug_ui(ECS* ecs);
bool system_draw_loaded_model(ECS* ecs);
bool system_draw_lights(ECS* ecs);
bool system_post_draw(ECS* ecs);

struct ResourceImguiLifetime{
	void Cleanup() {
		ImGui_ImplDX11_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
	}
};

struct ResourceFlycam{
	float yaw, pitch;
	XMFLOAT3 position;

	void Cleanup() {}
};

struct ResourceGraphicsCore {
	ID3D11Device* device;
	ID3D11DeviceContext* context;
	uint32_t numSwapChainBuffers;
	IDXGISwapChain* swapChain;
	DXGI_FORMAT swapChainFormat = DXGI_FORMAT_UNKNOWN;

	ID3D11Texture2D* depthTexture;
	ID3D11DepthStencilView* depthStencilView;

	void Cleanup() {
		depthTexture->Release();
		depthStencilView->Release();

		swapChain->Release();
		context->Release();
		device->Release();
	}
};

struct ResourceWindow {
	static void GlfwErrorCallback(int error, const char* description) {
		std::cout << "Glfw error " << std::hex << error << std::dec << ": " << description << "\n";
	}

	static void GlfwWindowSizeCallback(GLFWwindow* window, int width, int height);

	static void GlfwCursorPosCallback(GLFWwindow* window, double x, double y);

	static void GlfwKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

	static void GlfwMouseButtonCallback(GLFWwindow* window, int button, int action, int mods);


	GLFWwindow* window;

	void Cleanup() {
		glfwDestroyWindow(window);
		glfwTerminate();
	}
};

struct ECS {
	using System = bool(ECS* ecs);
	std::vector<System*> systems = {
		// Initialization
		system_setup_window,
		system_setup_graphics_core,
		system_setup_imgui,
		system_init_application,
		system_start,

		// Updates
		system_move_in_circle,
		system_flycam,

		// Draws
		system_pre_draw,
		system_draw_imgui_debug_ui,
		system_draw_loaded_model,
		system_draw_lights,
		system_post_draw
	};

	int nextEntity = 0;

	// Resources
	// TODO WT: would probably make sense to make these std::optional.
	Application* application;

	ResourceWindow window;
	ResourceImguiLifetime imguiLifetime;
	ResourceGraphicsCore graphicsCore;
	std::unordered_map<std::string, std::vector<Mesh>> modelCache;
	std::unordered_map<std::string, std::vector<Material>> materialCache;

	std::unordered_map<std::string, Texture*> textureCache;
	ResourceFlycam flyCam;

	// Components
	std::vector<std::optional<Light>> lights;
	std::vector<std::optional<ComponentMovesInCircle>> movesInCircle;

	void Cleanup();

	int addEntity() {
		lights.emplace_back();
		movesInCircle.emplace_back();

		return nextEntity++;
	}

	void run() {
		//std::vector<std::vector<System*>::iterator> toRemove;

		std::vector<System*> nextSystems;
		for (const auto system : systems) {
			if (system(this)) {
				nextSystems.push_back(system);
			}
		}

		systems = nextSystems;

		//for (auto systemIt = systems.begin(); systemIt != systems.end(); systemIt++) {
		//	if (!(*systemIt)(this)) {
		//		toRemove.push_back(systemIt);
		//	}
		//}

		//for (const auto& it : toRemove) {
		//	systems.erase(it);
		//}
	}
};


class Application {
public:
	static std::vector<char> readFile(const std::string& filename) {
		std::ifstream file(filename, std::ios::ate | std::ios::binary);

		if (!file.is_open()) {
			throw std::runtime_error("Failed to open file!");
		}

		size_t fileSize = (size_t)file.tellg();
		std::vector<char> buffer(fileSize);

		file.seekg(0);
		file.read(buffer.data(), fileSize);

		file.close();

		return buffer;
	}

public:
//protected:
//private:

	GraphicsPipeline *deferredGraphicsPipeline;
	GraphicsPipeline *lightingGraphicsPipeline;

	ID3D11Buffer* perFrameUniformsBuffer;
	PerFrameUniforms perFrameUniforms;

	ID3D11Buffer* perMaterialUniformsBuffer;

	ID3D11SamplerState* gbufferSampler;
	GeometryBuffer geometryBuffer;

	Lighting* lighting;

	std::vector<Light> lights;

	// TODO WT: Remove referance to ecs;

public:
	void Init(ECS* ecs) {
		createDeferredGraphicsPipeline(ecs);
		createLightingGraphicsPipeline(ecs);
		createConstantBuffers(ecs);
		createGbuffers(ecs);

		loadModel(ecs);

		lighting = new Lighting(ecs->graphicsCore.device);
	}

	void Cleanup() {
		delete lighting;

		delete lightingGraphicsPipeline;
		delete deferredGraphicsPipeline;
	}

public:
//protected:
//private:
	void createDeferredGraphicsPipeline(ECS* ecs) {
		std::vector<char> vertexShaderCode = readFile("shaders/deferredVertex.cso");
		std::vector<char> pixelShaderCode = readFile("shaders/deferredPixel.cso");

		D3D11_RASTERIZER_DESC rasterizerDesc{};
		rasterizerDesc.FillMode = D3D11_FILL_SOLID;
		rasterizerDesc.CullMode = D3D11_CULL_BACK;
		rasterizerDesc.FrontCounterClockwise = false;
		rasterizerDesc.DepthBias = 0;
		rasterizerDesc.DepthBiasClamp = 0;
		rasterizerDesc.SlopeScaledDepthBias = 0;
		rasterizerDesc.DepthClipEnable = false;
		rasterizerDesc.ScissorEnable = true;
		rasterizerDesc.MultisampleEnable = false; //useMultisampling;
		rasterizerDesc.AntialiasedLineEnable = false;

		int width, height;
		glfwGetWindowSize(ecs->window.window, &width, &height);

		D3D11_VIEWPORT viewport{};
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = 0.0f;
		viewport.Width = static_cast<float>(width);
		viewport.Height = static_cast<float>(height);
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;

		D3D11_RECT scissor{};
		scissor.left = 0;
		scissor.top = 0;
		scissor.right = width;
		scissor.bottom = height;

		std::vector<D3D11_INPUT_ELEMENT_DESC> inputs(5);
		inputs[0] = { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0 };
		inputs[1] = { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, normal), D3D11_INPUT_PER_VERTEX_DATA, 0 };
		inputs[2] = { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, tangent), D3D11_INPUT_PER_VERTEX_DATA, 0 };
		inputs[3] = { "BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, bitangent), D3D11_INPUT_PER_VERTEX_DATA, 0 };
		inputs[4] = { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, texcoord), D3D11_INPUT_PER_VERTEX_DATA, 0 };

		D3D11_DEPTH_STENCIL_DESC depthStencilDesc{};
		depthStencilDesc.DepthEnable = true;
		depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS;
		depthStencilDesc.StencilEnable = false;
		depthStencilDesc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
		depthStencilDesc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;

		depthStencilDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		depthStencilDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR;
		depthStencilDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
		depthStencilDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

		depthStencilDesc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		depthStencilDesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR;
		depthStencilDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
		depthStencilDesc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

		D3D11_BLEND_DESC blendDesc{};
		blendDesc.AlphaToCoverageEnable = false;
		blendDesc.IndependentBlendEnable = false;
		for (size_t i = 0; i < GeometryBuffer::MAX_BUFFER; i++) {
			blendDesc.RenderTarget[i].BlendEnable = false;
			blendDesc.RenderTarget[i].SrcBlend = D3D11_BLEND_ONE;
			blendDesc.RenderTarget[i].DestBlend = D3D11_BLEND_ZERO;
			blendDesc.RenderTarget[i].BlendOp = D3D11_BLEND_OP_ADD;
			blendDesc.RenderTarget[i].SrcBlendAlpha = D3D11_BLEND_ONE;
			blendDesc.RenderTarget[i].DestBlendAlpha = D3D11_BLEND_ZERO;
			blendDesc.RenderTarget[i].BlendOpAlpha = D3D11_BLEND_OP_ADD;
			blendDesc.RenderTarget[i].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		}

		deferredGraphicsPipeline = new GraphicsPipeline(
			ecs->graphicsCore.device,
			vertexShaderCode,
			pixelShaderCode,
			std::make_optional(inputs),
			rasterizerDesc,
			depthStencilDesc,
			blendDesc,
			D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
			viewport,
			scissor
		);
	}

	void createLightingGraphicsPipeline(ECS* ecs) {
		std::vector<char> vertexShaderCode = readFile("shaders/lightAccVertex.cso");
		std::vector<char> pixelShaderCode = readFile("shaders/lightAccPixel.cso");

		D3D11_RASTERIZER_DESC rasterizerDesc{};
		rasterizerDesc.FillMode = D3D11_FILL_SOLID;
		rasterizerDesc.CullMode = D3D11_CULL_NONE;
		rasterizerDesc.FrontCounterClockwise = false;
		rasterizerDesc.DepthBias = 0;
		rasterizerDesc.DepthBiasClamp = 0;
		rasterizerDesc.SlopeScaledDepthBias = 0;
		rasterizerDesc.DepthClipEnable = false;
		rasterizerDesc.ScissorEnable = true;
		rasterizerDesc.MultisampleEnable = false; //useMultisampling;
		rasterizerDesc.AntialiasedLineEnable = false;

		int width, height;
		glfwGetWindowSize(ecs->window.window, &width, &height);

		D3D11_VIEWPORT viewport{};
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = 0.0f;
		viewport.Width = static_cast<float>(width);
		viewport.Height = static_cast<float>(height);
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;

		D3D11_RECT scissor{};
		scissor.left = 0;
		scissor.top = 0;
		scissor.right = width;
		scissor.bottom = height;

		D3D11_DEPTH_STENCIL_DESC depthStencilDesc{};
		depthStencilDesc.DepthEnable = true;
		depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		depthStencilDesc.DepthFunc = D3D11_COMPARISON_NEVER;
		depthStencilDesc.StencilEnable = false;
		depthStencilDesc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
		depthStencilDesc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;

		depthStencilDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		depthStencilDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR;
		depthStencilDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
		depthStencilDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

		depthStencilDesc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		depthStencilDesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR;
		depthStencilDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
		depthStencilDesc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

		//std::vector<D3D11_INPUT_ELEMENT_DESC> inputs(1);
		//inputs[0] = { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 };

		D3D11_BLEND_DESC blendDesc{};
		blendDesc.AlphaToCoverageEnable = false;
		blendDesc.IndependentBlendEnable = false;
		blendDesc.RenderTarget[0].BlendEnable = true;
		blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
		blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		lightingGraphicsPipeline = new GraphicsPipeline(
			ecs->graphicsCore.device,
			vertexShaderCode,
			pixelShaderCode,
			//std::make_optional(inputs),
			std::nullopt,
			rasterizerDesc,
			depthStencilDesc,
			blendDesc,
			//D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
			D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
			viewport,
			scissor
		);

		D3D11_SAMPLER_DESC samplerDesc{};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.MipLODBias = 0.0f;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
		samplerDesc.BorderColor[0] = 0.0f;
		samplerDesc.BorderColor[1] = 0.0f;
		samplerDesc.BorderColor[2] = 0.0f;
		samplerDesc.BorderColor[3] = 0.0f;
		samplerDesc.MinLOD = 0.0f;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

		if (FAILED(ecs->graphicsCore.device->CreateSamplerState(&samplerDesc, &gbufferSampler))) {
			throw std::runtime_error("Failed to create gbuffer sampler");
		}
	}

	void createConstantBuffers(ECS* ecs) {
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = sizeof(PerFrameUniforms);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = 0;
		desc.StructureByteStride = 0;

		if (FAILED(ecs->graphicsCore.device->CreateBuffer(&desc, nullptr, &perFrameUniformsBuffer))) {
			throw std::runtime_error("Failed to create cbuffer!");
		}

		D3D11_BUFFER_DESC matDesc{};
		matDesc.ByteWidth = sizeof(MaterialCbuffer);
		matDesc.Usage = D3D11_USAGE_DYNAMIC;
		matDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		matDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		matDesc.MiscFlags = 0;
		matDesc.StructureByteStride = 0;

		if (FAILED(ecs->graphicsCore.device->CreateBuffer(&matDesc, nullptr, &perMaterialUniformsBuffer))) {
			throw std::runtime_error("Failed to create material settings cbuffer!");
		}
	}

	void createGbuffers(ECS* ecs) {
		int32_t width, height;
		glfwGetWindowSize(ecs->window.window, &width, &height);

		for (size_t i = 0; i < GeometryBuffer::MAX_BUFFER; i++) {
			D3D11_TEXTURE2D_DESC textureDesc{};
			textureDesc.Width = width;
			textureDesc.Height = height;
			textureDesc.MipLevels = 1;
			textureDesc.ArraySize = 1;
			textureDesc.Format = geometryBuffer.formats[i];
			textureDesc.SampleDesc.Count = 1;
			textureDesc.SampleDesc.Quality = 0;

			textureDesc.Usage = D3D11_USAGE_DEFAULT;
			textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
			textureDesc.CPUAccessFlags = 0;
			textureDesc.MiscFlags = 0;

			if (FAILED(ecs->graphicsCore.device->CreateTexture2D(&textureDesc, nullptr, &geometryBuffer.textures[i]))) {
				throw std::runtime_error("Failed to create gbuffer texture!");
			}

			D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
			rtvDesc.Format = geometryBuffer.formats[i];
			rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
			rtvDesc.Texture2D.MipSlice = 0;

			if (FAILED(ecs->graphicsCore.device->CreateRenderTargetView(geometryBuffer.textures[i], &rtvDesc, &geometryBuffer.textureViews[i]))) {
				throw std::runtime_error("Failed to create gbuffer RTV!");
			}

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = geometryBuffer.formats[i];
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = 1;
			srvDesc.Texture2D.MostDetailedMip = 0;

			if (FAILED(ecs->graphicsCore.device->CreateShaderResourceView(geometryBuffer.textures[i], &srvDesc, &geometryBuffer.textureResourceViews[i]))) {
				throw std::runtime_error("Failed to create gbuffer SRV!");
			}
		}
	}

	void loadModel(ECS* ecs) {
		Assimp::Importer* importer = new Assimp::Importer();

		AssimpProgressHandler* handler = new AssimpProgressHandler();
		importer->SetProgressHandler(handler); // Taken ownership of handler

		stbi_set_flip_vertically_on_load(true);

		std::string basePath = "assets/crytekSponza_fbx/";
		std::string fullPath = basePath + "sponza.fbx";

		const aiScene* scene = importer->ReadFile(fullPath, aiProcess_CalcTangentSpace | aiProcess_Triangulate | aiProcess_JoinIdenticalVertices);

		//std::cout << "\n Loaded\n";

		ecs->modelCache[fullPath] =  std::vector<Mesh>();//  = std::vector();
		ecs->materialCache[fullPath] = std::vector<Material>();

		ecs->modelCache[fullPath].reserve(scene->mNumMaterials);

		for (size_t i = 0; i < scene->mNumMaterials; i++) {
			Material mat;
			aiMaterial* data = scene->mMaterials[i];

			std::vector<aiMaterialProperty*> properties(data->mProperties, data->mProperties + data->mNumProperties);

			aiString name;
			if (data->Get(AI_MATKEY_NAME, name) == AI_SUCCESS) {
				//std::cout << "Loading material " << name.C_Str() << std::endl;
				mat.name = name.C_Str();
			}

			loadTexture(ecs, aiTextureType_DIFFUSE, data, basePath, mat.diffuseTexture, (bool&)mat.settings.useDiffuseTexture);
			loadTexture(ecs, aiTextureType_NORMALS, data, basePath, mat.normalTexture, (bool&)mat.settings.useNormalTexture);
			loadTexture(ecs, aiTextureType_OPACITY, data, basePath, mat.alphaCutoutTexture, (bool&)mat.settings.useAlphaCutoutTexture);
			loadTexture(ecs, aiTextureType_SPECULAR, data, basePath, mat.specularTexture, (bool&)mat.settings.useSpecularTexture);
			if (!mat.settings.useSpecularTexture)
				loadTexture(ecs, aiTextureType_SHININESS, data, basePath, mat.specularTexture, (bool&)mat.settings.useSpecularTexture);
			if (!mat.settings.useSpecularTexture)
				loadTexture(ecs, aiTextureType_DIFFUSE_ROUGHNESS, data, basePath, mat.specularTexture, (bool&)mat.settings.useSpecularTexture);

			ecs->materialCache[fullPath].push_back(mat);
		}

		ecs->modelCache[fullPath].reserve(scene->mNumMeshes);

		for (size_t i = 0; i < scene->mNumMeshes; i++) {
			Mesh mesh;
			aiMesh* data = scene->mMeshes[i];

			mesh.materialId = data->mMaterialIndex;

			std::vector<Vertex> vertices(data->mNumVertices);
			processVertices(data, vertices);

			auto numIndices = data->mNumFaces * 3u;
			std::vector<uint32_t> indices(numIndices);
			processIndices(data, indices);

			D3D11_BUFFER_DESC vDesc = {};
			vDesc.Usage = D3D11_USAGE_DEFAULT;
			vDesc.ByteWidth = sizeof(Vertex) * data->mNumVertices;
			vDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			vDesc.CPUAccessFlags = 0;
			vDesc.MiscFlags = 0;
			vDesc.StructureByteStride = 0;

			D3D11_SUBRESOURCE_DATA vertData{};
			vertData.pSysMem = vertices.data();

			mesh.numVertices = data->mNumVertices;
			auto vbHR = ecs->graphicsCore.device->CreateBuffer(&vDesc, &vertData, &mesh.vertices);

			std::string vbufferName(data->mName.C_Str());
			vbufferName += "_VertexBuffer";
			mesh.vertices->SetPrivateData(WKPDID_D3DDebugObjectName, vbufferName.size(), vbufferName.c_str());

			assert(SUCCEEDED(vbHR));

			D3D11_BUFFER_DESC iDesc = {};
			iDesc.Usage = D3D11_USAGE_DEFAULT;
			iDesc.ByteWidth = sizeof(unsigned int) * numIndices;
			iDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
			iDesc.CPUAccessFlags = 0;
			iDesc.MiscFlags = 0;
			iDesc.StructureByteStride = 0;

			D3D11_SUBRESOURCE_DATA indexData{};
			indexData.pSysMem = indices.data();

			mesh.indexCount = numIndices;
			auto ibHF = ecs->graphicsCore.device->CreateBuffer(&iDesc, &indexData, &mesh.indices);

			std::string ibufferName(data->mName.C_Str());
			ibufferName += "_IndexBuffer";
			mesh.indices->SetPrivateData(WKPDID_D3DDebugObjectName, ibufferName.size(), ibufferName.c_str());

			assert(SUCCEEDED(vbHR));

			mesh.materialId = data->mMaterialIndex;

			ecs->modelCache[fullPath].push_back(mesh);
		}

		importer->FreeScene();

		delete importer;
	}

	void loadTexture(ECS* ecs, aiTextureType type, aiMaterial* materialData, const std::string& baseAssetPath, std::string& outPath, bool& outEnabled) {
		outEnabled = false;
		outPath.clear();
		
		aiString path;
		if (materialData->Get(AI_MATKEY_TEXTURE(type, 0), path) == AI_SUCCESS) {
			outPath += baseAssetPath;
			outPath += path.C_Str();

			//std::cout << outPath << std::endl;

			std::filesystem::path asPath(outPath);
			auto extension = asPath.extension();
			if (extension == ".dds") {
				std::cout << "Cant support DDS just yet." << outPath << std::endl;
				outPath.clear();
				outEnabled = false;
				return;
			}
			else {
				int width, height, bpp;
				byte* textureData = stbi_load(outPath.c_str(), &width, &height, &bpp, STBI_rgb_alpha);
				if (!textureData) {
					std::stringstream errorString("Failed to load texture ");
					errorString << outPath << " because " << stbi_failure_reason();
					throw std::runtime_error(errorString.str());
				}

				ecs->textureCache[outPath] = new Texture(ecs->graphicsCore.device, ecs->graphicsCore.context, outPath, width, height, bpp, textureData);
				stbi_image_free(textureData);
			}

			outEnabled = true;
		}
	}

	void processVertices(const aiMesh* meshData, std::vector<Vertex>& vertices) {
		for (size_t v = 0; v < meshData->mNumVertices; v++) {
			auto pos = meshData->mVertices[v];
			auto normal = meshData->mNormals[v];
			auto tangent = meshData->mTangents[v];
			auto bitangent = meshData->mBitangents[v];
			aiVector3D uv;
			if (meshData->mTextureCoords) {
				uv = meshData->mTextureCoords[0][v];
			}

			vertices[v] = {
				{ pos.x / 100.0f, pos.y / 100.0f, pos.z / 100.0f },
				{ normal.x, normal.y, normal.z },
				{ tangent.x, tangent.y, tangent.z },
				{ bitangent.x, bitangent.y, bitangent.z },
				{ uv.x, uv.y }
			};
		}
	}

	void processIndices(const aiMesh* meshData, std::vector<uint32_t>& indices) {
		for (size_t face = 0, index = 0; face < meshData->mNumFaces; face++)
		{
			indices[index++] = meshData->mFaces[face].mIndices[0];
			indices[index++] = meshData->mFaces[face].mIndices[1];
			indices[index++] = meshData->mFaces[face].mIndices[2];
		}
	}

	// TODO WT: make events handleable in ecs
	void RecompileShaders(ECS* ecs) {
		// TODO WT: Clean up this memory leak heaven!
		ID3D11VertexShader* newVertShader;
		ID3D11PixelShader* newPixelShader;
		ID3D10Blob* bytecode;
		ID3D10Blob* errors;

		// Graphics
		if (FAILED(D3DCompileFromFile(L"shaders/deferredVertex.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "vs_5_0", 0, 0, &bytecode, &errors))) {
			std::wcout << L"deferred vshader error " << (char*)errors->GetBufferPointer() << std::endl;
			if (errors)
				errors->Release();
			return;
		}

		ecs->graphicsCore.device->CreateVertexShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &newVertShader);
		if (errors)
			errors->Release();
		bytecode->Release();

		if (FAILED(D3DCompileFromFile(L"shaders/deferredPixel.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "ps_5_0", 0, 0, &bytecode, &errors))) {
			std::wcout << L"deferred pshader error" << (char*)errors->GetBufferPointer() << std::endl;
			if (errors)
				errors->Release();
			return;
		}

		ecs->graphicsCore.device->CreatePixelShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &newPixelShader);
		if (errors)
			errors->Release();
		bytecode->Release();

		deferredGraphicsPipeline->vertexShader->Release();
		deferredGraphicsPipeline->pixelShader->Release();
		deferredGraphicsPipeline->vertexShader = newVertShader;
		deferredGraphicsPipeline->pixelShader = newPixelShader;

		// Lighting
		if (FAILED(D3DCompileFromFile(L"shaders/lightAccVertex.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "vs_5_0", 0, 0, &bytecode, &errors))) {
			std::wcout << L"lightAcc vshader error " << (char*)errors->GetBufferPointer() << std::endl;
			if (errors)
				errors->Release();
			return;
		}

		ecs->graphicsCore.device->CreateVertexShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &newVertShader);
		if (errors)
			errors->Release();
		bytecode->Release();

		if (FAILED(D3DCompileFromFile(L"shaders/lightAccPixel.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "ps_5_0", 0, 0, &bytecode, &errors))) {
			std::wcout << L"lightAcc pshader error: " << (char*)errors->GetBufferPointer() << std::endl;
			if (errors)
				errors->Release();
			return;
		}

		ecs->graphicsCore.device->CreatePixelShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &newPixelShader);
		if (errors)
			errors->Release();
		bytecode->Release();

		lightingGraphicsPipeline->vertexShader->Release();
		lightingGraphicsPipeline->pixelShader->Release();
		lightingGraphicsPipeline->vertexShader = newVertShader;
		lightingGraphicsPipeline->pixelShader = newPixelShader;

		std::cout << "Successfully hot reloader lighting pass shaders" << std::endl;
	}

	void OnWindowResized(ECS* ecs, uint32_t width, uint32_t height) {
		ecs->graphicsCore.swapChain->ResizeBuffers(ecs->graphicsCore.numSwapChainBuffers, width, height, ecs->graphicsCore.swapChainFormat, 0);

		D3D11_TEXTURE2D_DESC dstDesc;
		D3D11_DEPTH_STENCIL_VIEW_DESC dstViewDesc;
		ecs->graphicsCore.depthTexture->GetDesc(&dstDesc);
		ecs->graphicsCore.depthStencilView->GetDesc(&dstViewDesc);

		ecs->graphicsCore.depthTexture->Release();
		ecs->graphicsCore.depthStencilView->Release();

		dstDesc.Width = width;
		dstDesc.Height = height;

		HRESULT hr;
		hr = ecs->graphicsCore.device->CreateTexture2D(&dstDesc, nullptr, &ecs->graphicsCore.depthTexture);
		if (FAILED(hr) || !ecs->graphicsCore.depthTexture) {
			throw std::runtime_error("Failed to create resized Depth Stencil texture!");
		}
		
		hr = ecs->graphicsCore.device->CreateDepthStencilView(ecs->graphicsCore.depthTexture, &dstViewDesc, &ecs->graphicsCore.depthStencilView);
		if (FAILED(hr) || !ecs->graphicsCore.depthStencilView) {
			throw std::runtime_error("Failed to create resized Depth Stencil View!");
		}

		deferredGraphicsPipeline->viewport.Width = static_cast<float>(width);
		deferredGraphicsPipeline->viewport.Height = static_cast<float>(height);

		deferredGraphicsPipeline->scissor.right = width;
		deferredGraphicsPipeline->scissor.bottom = height;

		for (size_t i = 0; i < GeometryBuffer::MAX_BUFFER; i++)
		{
			D3D11_TEXTURE2D_DESC texDesc;
			D3D11_RENDER_TARGET_VIEW_DESC rtvDesc;
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
			geometryBuffer.textures[i]->GetDesc(&texDesc);
			geometryBuffer.textureViews[i]->GetDesc(&rtvDesc);
			geometryBuffer.textureResourceViews[i]->GetDesc(&srvDesc);

			geometryBuffer.textures[i]->Release();
			geometryBuffer.textureViews[i]->Release();
			geometryBuffer.textureResourceViews[i]->Release();

			texDesc.Width = width;
			texDesc.Height = height;

			hr = ecs->graphicsCore.device->CreateTexture2D(&texDesc, nullptr, &geometryBuffer.textures[i]);
			if (FAILED(hr) || !geometryBuffer.textures[i]) {
				throw std::runtime_error("Failed to create resized GBuffer texture!");
			}

			hr = ecs->graphicsCore.device->CreateRenderTargetView(geometryBuffer.textures[i], &rtvDesc, &geometryBuffer.textureViews[i]);
			if (FAILED(hr) || !geometryBuffer.textureViews[i]) {
				throw std::runtime_error("Failed to create resized GBuffer texture RTV!");
			}

			hr = ecs->graphicsCore.device->CreateShaderResourceView(geometryBuffer.textures[i], &srvDesc, &geometryBuffer.textureResourceViews[i]);
			if (FAILED(hr) || !geometryBuffer.textureResourceViews[i]) {
				throw std::runtime_error("Failed to create resized GBuffer texture! SRV");
			}

		}

		lightingGraphicsPipeline->viewport.Width = static_cast<float>(width);
		lightingGraphicsPipeline->viewport.Height = static_cast<float>(height);

		lightingGraphicsPipeline->scissor.right = width;
		lightingGraphicsPipeline->scissor.bottom = height;
	}
};

bool system_setup_graphics_core(ECS* ecs) {
	auto& graphics = ecs->graphicsCore;

	uint32_t flags = D3D11_CREATE_DEVICE_SINGLETHREADED;

#if DEBUG || _DEBUG
	flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0
	};

	int32_t width, height;
	glfwGetWindowSize(ecs->window.window, &width, &height);

	ecs->graphicsCore.numSwapChainBuffers = 2;
	ecs->graphicsCore.swapChainFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
	//multisampleFormat = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;

	HWND hwnd = glfwGetWin32Window(ecs->window.window);

	DXGI_SWAP_CHAIN_DESC swapChainDesc{};
	swapChainDesc.BufferDesc.Width = static_cast<uint32_t>(width);
	swapChainDesc.BufferDesc.Height = static_cast<uint32_t>(height);
	swapChainDesc.BufferDesc.RefreshRate.Numerator = 1;
	swapChainDesc.BufferDesc.RefreshRate.Denominator = 60;
	swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	swapChainDesc.BufferDesc.Format = ecs->graphicsCore.swapChainFormat;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = ecs->graphicsCore.numSwapChainBuffers; // TODO WT: 3 buffers for mailbox?
	swapChainDesc.OutputWindow = hwnd;
	swapChainDesc.Windowed = true;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.Flags = 0;

	auto hr = D3D11CreateDeviceAndSwapChain(
		nullptr,
		D3D_DRIVER_TYPE_HARDWARE,
		nullptr,
		flags,
		featureLevels,
		ARRAYSIZE(featureLevels),
		D3D11_SDK_VERSION,
		&swapChainDesc,
		&ecs->graphicsCore.swapChain,
		&ecs->graphicsCore.device,
		nullptr,
		&ecs->graphicsCore.context);

	if (FAILED(hr)) {
		std::stringstream error("Failed to create device and swap chain! ");
		error << std::hex << hr << std::endl;

		throw std::runtime_error(error.str());
	}

	// TODO WT: Resize depth texture
	D3D11_TEXTURE2D_DESC depthTextureDesc;
	depthTextureDesc.Width = width;
	depthTextureDesc.Height = height;
	depthTextureDesc.MipLevels = 1;
	depthTextureDesc.ArraySize = 1;
	depthTextureDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	depthTextureDesc.SampleDesc.Count = 1;
	depthTextureDesc.SampleDesc.Quality = 0;
	depthTextureDesc.Usage = D3D11_USAGE_DEFAULT;
	depthTextureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	depthTextureDesc.CPUAccessFlags = 0;
	depthTextureDesc.MiscFlags = 0;

	hr = ecs->graphicsCore.device->CreateTexture2D(&depthTextureDesc, nullptr, &ecs->graphicsCore.depthTexture);
	if (FAILED(hr)) {
		throw std::runtime_error("Failed to create depth texture!");
	}

	D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Texture2D.MipSlice = 0;

	hr = ecs->graphicsCore.device->CreateDepthStencilView(ecs->graphicsCore.depthTexture, &dsvDesc, &ecs->graphicsCore.depthStencilView);
	if (FAILED(hr)) {
		throw std::runtime_error("Failed to create depth texture RTV!");
	}

	return false;
}

bool system_setup_imgui(ECS* ecs) {
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;

	ImGui::StyleColorsDark();

	ImGui_ImplGlfw_InitForOther(ecs->window.window, true);
	ImGui_ImplDX11_Init(ecs->graphicsCore.device, ecs->graphicsCore.context);

	return false;
}

bool system_init_application(ECS* ecs) {
	ecs->application = new Application();
	ecs->application->Init(ecs);

	return false;
}

bool system_start(ECS* ecs) {
	auto light = ecs->addEntity();
	ecs->lights[light].emplace(XMFLOAT3(0.0f, 1.0f, 0.0f), 2.0f, XMFLOAT3(1.0f, 1.0f, 1.0f), 1.0f, XMFLOAT4(0.1f, 0.1f, 0.1f, 1.0f));
	ecs->movesInCircle[light].emplace();

	XMVECTOR colors[] = {
		Colors::Red,
		Colors::Green,
		Colors::Blue,
		Colors::Cyan,
		Colors::Magenta,
		Colors::Yellow,
	};

	for (size_t i = 1; i < 50; i++)
	{
		light = ecs->addEntity();
		float rand0 = (float)rand() / RAND_MAX;
		float rand1 = (float)rand() / RAND_MAX;
		float rand2 = (float)rand() / RAND_MAX;
		ecs->lights[light].emplace(
			XMFLOAT3{ rand0 * 30.0f - 15.0f, rand2 * 10.0f, rand1 * 20.0f - 10.0f },
			5.0f,
			XMFLOAT3{ 1.0f, 1.0f, 1.0f },
			1.0f,
			XMFLOAT4{ 0.0f, 0.0f, 0.0f, 0.0f }
		);
		ecs->movesInCircle[light].emplace(ComponentMovesInCircle{ ecs->lights[light].value().position, 1.0f });
		XMStoreFloat3(&ecs->lights[light].value().color, colors[i % 6]);
	}

	return false;
}

bool system_move_in_circle(ECS* ecs) {
	float time = static_cast<float>(glfwGetTime());

	for (int i = -0; i < ecs->nextEntity; i++) {
		if (ecs->lights[i].has_value() && ecs->movesInCircle[i].has_value()) {
			auto& light = ecs->lights[i].value();
			auto& moves = ecs->movesInCircle[i].value();

			light.position.x = moves.origin.x + std::cosf(time) * 2.0f;
			light.position.z = moves.origin.z - std::sin(time) * 2.0f;
		}
	}

	return true;
}

bool system_flycam(ECS* ecs) {
	static double lastTime = 0.0f;
	double thisTime = glfwGetTime();
	float delta = static_cast<float>(thisTime - lastTime);
	lastTime = thisTime;

	auto look = XMMatrixRotationRollPitchYaw(ecs->flyCam.pitch, ecs->flyCam.yaw, 0.0f);

	float x = 0;
	x += glfwGetKey(ecs->window.window, GLFW_KEY_D) == GLFW_PRESS ? 1.0f : 0.0f;
	x -= glfwGetKey(ecs->window.window, GLFW_KEY_A) == GLFW_PRESS ? 1.0f : 0.0f;

	float y = 0;
	y += glfwGetKey(ecs->window.window, GLFW_KEY_W) == GLFW_PRESS ? 1.0f : 0.0f;
	y -= glfwGetKey(ecs->window.window, GLFW_KEY_S) == GLFW_PRESS ? 1.0f : 0.0f;

	auto moveDir = XMVectorSet(x, 0.0f, y, 0.0f);
	moveDir = XMVector3Normalize(moveDir);
	moveDir = XMVector3Transform(moveDir, look);

	auto moveDelta = XMVectorScale(moveDir, delta * 2.0f);

	auto newPosition = XMVectorAdd(XMLoadFloat3(&ecs->flyCam.position), moveDelta);
	XMStoreFloat3(&ecs->flyCam.position, newPosition);

	ecs->application->perFrameUniforms.eyePos = ecs->flyCam.position;

	auto trans = XMMatrixTranslation(ecs->flyCam.position.x, ecs->flyCam.position.y, ecs->flyCam.position.z);

	auto camera = look * trans;
	auto det = XMMatrixDeterminant(camera);
	auto view = XMMatrixInverse(&det, camera);

	int width, height;
	glfwGetWindowSize(ecs->window.window, &width, &height);
	ecs->application->perFrameUniforms.screenDimensions = { static_cast<float>(width), static_cast<float>(height) };
	ecs->application->perFrameUniforms.view = view;
	XMMATRIX proj;

	proj = XMMatrixPerspectiveFovLH(45.0f, static_cast<float>(width) / height, 0.1f, 1000.0f);
	ecs->application->perFrameUniforms.viewProj = ecs->application->perFrameUniforms.view * proj;

	return true;
}

bool system_draw_imgui_debug_ui(ECS* ecs)
{
	int width, height;
	glfwGetWindowSize(ecs->window.window, &width, &height);

	if (ImGui::BeginMainMenuBar()) {
		ImVec2 mainMenuSize = ImGui::GetWindowSize();

		ecs->application->deferredGraphicsPipeline->scissor.top = ecs->application->lightingGraphicsPipeline->scissor.top = static_cast<uint64_t>(mainMenuSize.y);

		static int currentVisualizedBuffer = -1;
		if (ImGui::BeginMenu("Visualize Buffer")) {
			if (ImGui::MenuItem("Position", nullptr, currentVisualizedBuffer == GeometryBuffer::Buffer::POSITION)) currentVisualizedBuffer = GeometryBuffer::Buffer::POSITION;
			if (ImGui::MenuItem("Normals", nullptr, currentVisualizedBuffer == GeometryBuffer::Buffer::NORMAL)) currentVisualizedBuffer = GeometryBuffer::Buffer::NORMAL;
			if (ImGui::MenuItem("Albedo", nullptr, currentVisualizedBuffer == GeometryBuffer::Buffer::ALBEDO)) currentVisualizedBuffer = GeometryBuffer::Buffer::ALBEDO;
			if (ImGui::MenuItem("Specular", nullptr, currentVisualizedBuffer == GeometryBuffer::Buffer::SPECULAR)) currentVisualizedBuffer = GeometryBuffer::Buffer::SPECULAR;
			if (ImGui::MenuItem("None", nullptr, currentVisualizedBuffer == -1)) currentVisualizedBuffer = -1;
			ImGui::EndMenu();
		}

		if (ImGui::MenuItem("Recompile Shaders")) {
			ecs->application->RecompileShaders(ecs);
		}

		ImGui::EndMainMenuBar();

		if (currentVisualizedBuffer >= 0) {
			ImGui::SetNextWindowPos({ 0.0f, 0.0f });
			ImGui::SetNextWindowSize({ (float)width, (float)height });
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2());
			ImGui::SetNextWindowContentSize({ (float)width, (float)height - mainMenuSize.y });
			if (ImGui::Begin("Visualize", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_NoBringToFrontOnFocus)) {
				ImGui::Image(ecs->application->geometryBuffer.textureResourceViews[currentVisualizedBuffer], ImGui::GetWindowSize());

				ImGui::End();
			}
		}
	}

	return true;
}

// System which just draws the sponza model
bool system_draw_loaded_model(ECS* ecs)
{
	int width, height;
	glfwGetWindowSize(ecs->window.window, &width, &height);

	ecs->application->deferredGraphicsPipeline->bind(ecs->graphicsCore.context);
	//context->ClearRenderTargetView(multisampleRTV, clearColor);
	//context->OMSetRenderTargets(1, &multisampleRTV, nullptr);

	D3D11_MAPPED_SUBRESOURCE mapped{};
	ecs->graphicsCore.context->Map(ecs->application->perFrameUniformsBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	memcpy(mapped.pData, &ecs->application->perFrameUniforms, sizeof(PerFrameUniforms));
	ecs->graphicsCore.context->Unmap(ecs->application->perFrameUniformsBuffer, 0);

	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	for (size_t i = 0; i < GeometryBuffer::MAX_BUFFER; i++)
	{
		ecs->graphicsCore.context->ClearRenderTargetView(ecs->application->geometryBuffer.textureViews[i], clearColor);
	}
	ecs->graphicsCore.context->ClearDepthStencilView(ecs->graphicsCore.depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0, 0);
	ecs->graphicsCore.context->OMSetRenderTargets(GeometryBuffer::MAX_BUFFER, ecs->application->geometryBuffer.textureViews, ecs->graphicsCore.depthStencilView);

	// Deferred passes to geometry buffer
	ecs->graphicsCore.context->VSSetConstantBuffers(0, 1, &ecs->application->perFrameUniformsBuffer);
	ecs->graphicsCore.context->PSSetConstantBuffers(0, 1, &ecs->application->perFrameUniformsBuffer);

	uint32_t strides = sizeof(Vertex);
	uint32_t offsets = 0;

	//context->IASetVertexBuffers(0, 1, &vertices, &strides, &offsets);
	//context->IASetIndexBuffer(indices, DXGI_FORMAT_R32_UINT, 0);
	//context->DrawIndexed(numIndices, 0, 0);

	ID3D11ShaderResourceView* nullTexture = nullptr;

	for (const auto& mesh : ecs->modelCache["assets/crytekSponza_fbx/sponza.fbx"]) {
		auto& mat = ecs->materialCache["assets/crytekSponza_fbx/sponza.fbx"][mesh.materialId];

		ID3D11ShaderResourceView* views[] = {
			(mat.settings.useDiffuseTexture) ? ecs->textureCache[mat.diffuseTexture]->textureSRV : nullTexture,
			(mat.settings.useNormalTexture) ? ecs->textureCache[mat.normalTexture]->textureSRV : nullTexture,
			(mat.settings.useAlphaCutoutTexture) ? ecs->textureCache[mat.alphaCutoutTexture]->textureSRV : nullTexture,
			(mat.settings.useSpecularTexture) ? ecs->textureCache[mat.specularTexture]->textureSRV : nullTexture,
		};

		// TODO WT: Make a default sampler to fall back on instead of the gbuffer sampler
		ID3D11SamplerState* samplers[] = {
			(mat.settings.useDiffuseTexture) ? ecs->textureCache[mat.diffuseTexture]->sampler : ecs->application->gbufferSampler,
			(mat.settings.useNormalTexture) ? ecs->textureCache[mat.normalTexture]->sampler : ecs->application->gbufferSampler,
			(mat.settings.useAlphaCutoutTexture) ? ecs->textureCache[mat.alphaCutoutTexture]->sampler : ecs->application->gbufferSampler,
			(mat.settings.useSpecularTexture) ? ecs->textureCache[mat.specularTexture]->sampler : ecs->application->gbufferSampler,
		};

		ecs->graphicsCore.context->PSSetShaderResources(0, 4, views);
		ecs->graphicsCore.context->PSSetSamplers(0, 4, samplers);

		D3D11_MAPPED_SUBRESOURCE mappedSettings{};
		ecs->graphicsCore.context->Map(ecs->application->perMaterialUniformsBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSettings);
		memcpy(mappedSettings.pData, &mat.settings, sizeof(MaterialCbuffer));
		ecs->graphicsCore.context->Unmap(ecs->application->perMaterialUniformsBuffer, 0);

		ecs->graphicsCore.context->PSSetConstantBuffers(1, 1, &ecs->application->perMaterialUniformsBuffer);

		ecs->graphicsCore.context->IASetVertexBuffers(0, 1, &mesh.vertices, &strides, &offsets);
		ecs->graphicsCore.context->IASetIndexBuffer(mesh.indices, DXGI_FORMAT_R32_UINT, 0);
		ecs->graphicsCore.context->DrawIndexed(mesh.indexCount, 0, 0);
	}
	return true;
}

bool system_draw_lights(ECS* ecs) {
	// TODO WT: This should be a resource, it's pretty global.
	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

	ID3D11Texture2D* backBuffer;
	if (FAILED(ecs->graphicsCore.swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
		throw std::runtime_error("Failed to get a back buffer!");
	}

	ID3D11RenderTargetView* renderTarget;

	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = ecs->graphicsCore.swapChainFormat;
	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;

	if (FAILED(ecs->graphicsCore.device->CreateRenderTargetView(backBuffer, &rtvDesc, &renderTarget))) {
		throw std::runtime_error("Failed to create backbuffer RTV!");
	}

	backBuffer->Release();

	ecs->graphicsCore.context->ClearRenderTargetView(renderTarget, clearColor);
	ecs->graphicsCore.context->OMSetRenderTargets(1, &renderTarget, nullptr);

	ecs->application->lightingGraphicsPipeline->bind(ecs->graphicsCore.context);
	ecs->graphicsCore.context->PSSetShaderResources(0, GeometryBuffer::MAX_BUFFER, ecs->application->geometryBuffer.textureResourceViews);

	ecs->graphicsCore.context->PSSetSamplers(0, 1, &ecs->application->gbufferSampler);

	// Draw light mesh
	for (size_t i = 0; i < ecs->lights.size(); i++)
	{
		if (!ecs->lights[i].has_value()) continue;
		ecs->application->lighting->DrawPointLight(ecs->graphicsCore.context, ecs->lights[i].value());
	}

	ID3D11ShaderResourceView* nullSRVs[GeometryBuffer::MAX_BUFFER];
	memset(nullSRVs, 0, sizeof(nullSRVs));
	ecs->graphicsCore.context->PSSetShaderResources(0, GeometryBuffer::MAX_BUFFER, nullSRVs);

	// TODO WT: Might cause error releasing now?
	renderTarget->Release();

	return true;
}

bool system_post_draw(ECS* ecs) {
	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

	ecs->graphicsCore.swapChain->Present(1, 0);

	return true;
}

int main(int argc, char** argv) {
	try {
		ECS ecs;
		ecs.run();

		while (!glfwWindowShouldClose(ecs.window.window)) {
			glfwPollEvents();
			ecs.run();

		}
	}
	catch (std::runtime_error e) {
		std::cout << e.what() << std::endl;
		__debugbreak();

		return -1;
	}

	return 0;
}

void ResourceWindow::GlfwWindowSizeCallback(GLFWwindow* window, int width, int height) {
	auto ecs = reinterpret_cast<ECS*>(glfwGetWindowUserPointer(window));

	ecs->application->OnWindowResized(ecs, width, height);
}

void ResourceWindow::GlfwCursorPosCallback(GLFWwindow* window, double x, double y) {
	int width, height;
	glfwGetWindowSize(window, &width, &height);

	static bool isFirstTime = true;
	static double lastX = 0.0f;
	static double lastY = 0.0f;

	auto ecs = reinterpret_cast<ECS*>(glfwGetWindowUserPointer(window));

	if (isFirstTime) {
		lastX = x;
		lastY = y;
		isFirstTime = false;
	}

	if (glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED) {
		float extent = PI - 0.1f;

		ecs->flyCam.yaw += static_cast<float>(x - lastX) / width;
		ecs->flyCam.pitch += static_cast<float>(y - lastY) / height;

		ecs->flyCam.yaw = std::fmodf(ecs->flyCam.yaw, PI * 2.0f);
		ecs->flyCam.pitch = std::fmaxf(-extent, std::fminf(extent, ecs->flyCam.pitch));
	}

	lastX = (float)x;
	lastY = (float)y;
}

void ResourceWindow::GlfwKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	if (ImGui::GetIO().WantCaptureKeyboard) return;

	auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));

	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	}
}

void ResourceWindow::GlfwMouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
	if (ImGui::GetIO().WantCaptureMouse) return;

	auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));

	if (button == GLFW_MOUSE_BUTTON_1 && action == GLFW_PRESS) {
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	}
}

bool system_setup_window(ECS* ecs) {
	if (!glfwInit()) {
		throw std::runtime_error("Failed to init glfw");
	}

	glfwSetErrorCallback(ResourceWindow::GlfwErrorCallback);

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	ecs->window.window = glfwCreateWindow(1280, 720, "D3D11 Application", nullptr, nullptr);
	if (!ecs->window.window) {
		throw std::runtime_error("Failed to create window");
	}

	glfwSetWindowUserPointer(ecs->window.window, ecs);

	glfwSetInputMode(ecs->window.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	glfwSetWindowSizeCallback(ecs->window.window, ResourceWindow::GlfwWindowSizeCallback);
	glfwSetCursorPosCallback(ecs->window.window, ResourceWindow::GlfwCursorPosCallback);
	glfwSetKeyCallback(ecs->window.window, ResourceWindow::GlfwKeyCallback);
	glfwSetMouseButtonCallback(ecs->window.window, ResourceWindow::GlfwMouseButtonCallback);

	return false;
}

void ECS::Cleanup() {
	for (auto texture : textureCache) {
		delete texture.second;
	}

	for (auto meshes : modelCache) {
		for (auto mesh : meshes.second) {
			mesh.vertices->Release();
			mesh.indices->Release();
		}
	}

	// Delete pipelines
	// TODO WT: Want this to not be a pointer, requires moving stuff to separate files to define the dependency tree.
	application->Cleanup();
	delete application;

	imguiLifetime.Cleanup();
	graphicsCore.Cleanup();
	window.Cleanup();
}