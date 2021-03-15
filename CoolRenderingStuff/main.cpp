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
		MAX_BUFFER,
	};

	const DXGI_FORMAT formats[MAX_BUFFER] = {
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		DXGI_FORMAT_R16G16B16A16_SNORM,
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
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
	int useDiffuseTexture = false;
	int useNormalTexture = false;
	int useAlphaCutoutTexture = false;
	int pad;
};

struct Material {
	std::string name;
	std::string diffuseTexture;
	std::string normalTexture;
	std::string alphaCutoutTexture;
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

class Application {
public:
	static void GlfwErrorCallback(int error, const char* description) {
		std::cout << "Glfw error " << std::hex << error << std::dec << ": " << description << "\n";
	}

	static void GlfwWindowSizeCallback(GLFWwindow* window, int width, int height) {
		auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));

		app->OnWindowResized(width, height);
	}

	static void GlfwCursorPosCallback(GLFWwindow* window, double x, double y) {
		int width, height;
		glfwGetWindowSize(window, &width, &height);

		static bool isFirstTime = true;
		static double lastX = 0.0f;
		static double lastY = 0.0f;

		auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));

		if (isFirstTime) {
			lastX = x;
			lastY = y;
			isFirstTime = false;
		}

		if (glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED) {
			float extent = PI - 0.1f;

			app->yaw += static_cast<float>(x - lastX) / width;
			app->pitch += static_cast<float>(y - lastY) / height;

			app->yaw = std::fmodf(app->yaw, PI * 2.0f);
			app->pitch = std::fmaxf(-extent, std::fminf(extent, app->pitch));
		}

		lastX = (float)x;
		lastY = (float)y;
	}

	static void GlfwKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
		if (ImGui::GetIO().WantCaptureKeyboard) return;

		auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));

		if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		}
	}

	static void GlfwMouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
	{
		if (ImGui::GetIO().WantCaptureMouse) return;

		auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));

		if (button == GLFW_MOUSE_BUTTON_1 && action == GLFW_PRESS) {
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		}
	}

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
protected:
private:
	GLFWwindow* window;

	ID3D11Device* device;
	ID3D11DeviceContext* context;
	uint32_t numSwapChainBuffers;
	IDXGISwapChain* swapChain;
	DXGI_FORMAT swapChainFormat = DXGI_FORMAT_UNKNOWN;

	GraphicsPipeline *deferredGraphicsPipeline;
	GraphicsPipeline *lightingGraphicsPipeline;

	ID3D11Texture2D* depthTexture;
	ID3D11DepthStencilView* depthStencilView;

	float yaw;
	float pitch;

	XMFLOAT3 cameraPosition = { 0.0f, 1.5f, 0.0f };

	ID3D11Buffer* perFrameUniformsBuffer;
	PerFrameUniforms perFrameUniforms;

	ID3D11Buffer* perMaterialUniformsBuffer;

	ID3D11SamplerState* gbufferSampler;
	GeometryBuffer geometryBuffer;

	std::unordered_map<std::string, Texture*> textureCache;

	std::vector<Mesh> loadedMesh;
	std::vector<Material> loadedMaterials;

	Lighting* lighting;

	std::vector<Light> lights;

public:
	Application() {
		createWindow();
		createDeviceAndSwapChain();
		initImgui();
		createDeferredGraphicsPipeline();
		createLightingGraphicsPipeline();
		createConstantBuffers();
		createGbuffers();

		loadModel();

		lighting = new Lighting(device);

		XMVECTOR colors[] = {
			Colors::Red,
			Colors::Green,
			Colors::Blue,
			Colors::Cyan,
			Colors::Magenta,
			Colors::Yellow,
		};

		lights.emplace_back(XMFLOAT3(0.0f, 0.5f, 0.0f), 2.0f, XMFLOAT3(1.0f, 1.0f, 1.0f), 2.0f, XMFLOAT4());

		for (size_t i = 0; i < 50; i++)
		{
			float rand0 = (float)rand() / RAND_MAX;
			float rand1 = (float)rand() / RAND_MAX;
			float rand2 = (float)rand() / RAND_MAX;
			lights.emplace_back(
				XMFLOAT3 { rand0 * 30.0f - 15.0f, rand2 * 10.0f, rand1 * 20.0f - 10.0f },
				5.0f,
				XMFLOAT3 { 1.0f, 1.0f, 1.0f },
				1.0f,
				XMFLOAT4 { 0.0f, 0.0f, 0.0f, 0.0f}
			);
			XMStoreFloat3(&lights[i].color, colors[i % 6]);
		}

		//lights[0].ambient = { 0.1f, 0.1f, 0.1f, 0.0f };
	}

	~Application() {
		for (auto texture : textureCache) {
			delete texture.second;
		}

		for (auto mesh : loadedMesh) {
			mesh.vertices->Release();
			mesh.indices->Release();
		}
		delete lighting;

		delete lightingGraphicsPipeline;
		delete deferredGraphicsPipeline;

		ImGui_ImplDX11_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();

		swapChain->Release();
		context->Release();
		device->Release();

		glfwDestroyWindow(window);
		glfwTerminate();
	}

public:
	void run() {
		while (!glfwWindowShouldClose(window)) {
			glfwPollEvents();

			ImGui_ImplDX11_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();

			updateFrame();
			drawFrame();
		}
	}
protected:
private:
	void createWindow() {
		if (!glfwInit()) {
			throw std::runtime_error("Failed to init glfw");
		}

		glfwSetErrorCallback(GlfwErrorCallback);

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

		window = glfwCreateWindow(1280, 720, "D3D11 Application", nullptr, nullptr);
		if (!window) {
			throw std::runtime_error("Failed to create window");
		}

		glfwSetWindowUserPointer(window, this);

		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

		glfwSetWindowSizeCallback(window, GlfwWindowSizeCallback);
		glfwSetCursorPosCallback(window, GlfwCursorPosCallback);
		glfwSetKeyCallback(window, GlfwKeyCallback);
		glfwSetMouseButtonCallback(window, GlfwMouseButtonCallback);
	}

	void createDeviceAndSwapChain() {
		uint32_t flags = D3D11_CREATE_DEVICE_SINGLETHREADED;

#if DEBUG || _DEBUG
		flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

		D3D_FEATURE_LEVEL featureLevels[] = {
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0
		};

		int32_t width, height;
		glfwGetWindowSize(window, &width, &height);

		numSwapChainBuffers = 2;
		swapChainFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
		//multisampleFormat = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;

		HWND hwnd = glfwGetWin32Window(window);

		DXGI_SWAP_CHAIN_DESC swapChainDesc{};
		swapChainDesc.BufferDesc.Width = static_cast<uint32_t>(width);
		swapChainDesc.BufferDesc.Height = static_cast<uint32_t>(height);
		swapChainDesc.BufferDesc.RefreshRate.Numerator = 1;
		swapChainDesc.BufferDesc.RefreshRate.Denominator = 60;
		swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
		swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
		swapChainDesc.BufferDesc.Format = swapChainFormat;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.SampleDesc.Quality = 0;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = numSwapChainBuffers; // TODO WT: 3 buffers for mailbox?
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
			&swapChain,
			&device,
			nullptr,
			&context);

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

		hr = device->CreateTexture2D(&depthTextureDesc, nullptr, &depthTexture);
		if (FAILED(hr)) {
			throw std::runtime_error("Failed to create depth texture!");
		}

		D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		dsvDesc.Texture2D.MipSlice = 0;

		hr = device->CreateDepthStencilView(depthTexture, &dsvDesc, &depthStencilView);
		if (FAILED(hr)) {
			throw std::runtime_error("Failed to create depth texture RTV!");
		}
	}

	void initImgui() {
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO(); (void)io;

		ImGui::StyleColorsDark();

		ImGui_ImplGlfw_InitForOther(window, true);
		ImGui_ImplDX11_Init(device, context);
	}

	void createDeferredGraphicsPipeline() {
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
		glfwGetWindowSize(window, &width, &height);

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
			device,
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

	void createLightingGraphicsPipeline() {
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
		glfwGetWindowSize(window, &width, &height);

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
			device,
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

		if (FAILED(device->CreateSamplerState(&samplerDesc, &gbufferSampler))) {
			throw std::runtime_error("Failed to create gbuffer sampler");
		}
	}

	void createConstantBuffers() {
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = sizeof(PerFrameUniforms);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = 0;
		desc.StructureByteStride = 0;

		if (FAILED(device->CreateBuffer(&desc, nullptr, &perFrameUniformsBuffer))) {
			throw std::runtime_error("Failed to create cbuffer!");
		}

		D3D11_BUFFER_DESC matDesc{};
		matDesc.ByteWidth = sizeof(MaterialCbuffer);
		matDesc.Usage = D3D11_USAGE_DYNAMIC;
		matDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		matDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		matDesc.MiscFlags = 0;
		matDesc.StructureByteStride = 0;

		if (FAILED(device->CreateBuffer(&matDesc, nullptr, &perMaterialUniformsBuffer))) {
			throw std::runtime_error("Failed to create material settings cbuffer!");
		}
	}

	void createGbuffers() {
		int32_t width, height;
		glfwGetWindowSize(window, &width, &height);

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

			if (FAILED(device->CreateTexture2D(&textureDesc, nullptr, &geometryBuffer.textures[i]))) {
				throw std::runtime_error("Failed to create gbuffer texture!");
			}

			D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
			rtvDesc.Format = geometryBuffer.formats[i];
			rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
			rtvDesc.Texture2D.MipSlice = 0;

			if (FAILED(device->CreateRenderTargetView(geometryBuffer.textures[i], &rtvDesc, &geometryBuffer.textureViews[i]))) {
				throw std::runtime_error("Failed to create gbuffer RTV!");
			}

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = geometryBuffer.formats[i];
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = 1;
			srvDesc.Texture2D.MostDetailedMip = 0;

			if (FAILED(device->CreateShaderResourceView(geometryBuffer.textures[i], &srvDesc, &geometryBuffer.textureResourceViews[i]))) {
				throw std::runtime_error("Failed to create gbuffer SRV!");
			}
		}
	}

	void loadModel() {
		Assimp::Importer* importer = new Assimp::Importer();

		AssimpProgressHandler* handler = new AssimpProgressHandler();
		importer->SetProgressHandler(handler); // Taken ownership of handler

		stbi_set_flip_vertically_on_load(true);

		std::string basePath = "assets/crytekSponza_fbx/";

		const aiScene* scene = importer->ReadFile(basePath + "sponza.fbx", aiProcess_CalcTangentSpace | aiProcess_Triangulate | aiProcess_JoinIdenticalVertices);

		std::cout << "\n Loaded\n";

		loadedMaterials.reserve(scene->mNumMaterials);

		for (size_t i = 0; i < scene->mNumMaterials; i++) {
			Material mat;
			aiMaterial* data = scene->mMaterials[i];

			std::vector<aiMaterialProperty*> properties(data->mProperties, data->mProperties + data->mNumProperties);

			aiString name;
			if (data->Get(AI_MATKEY_NAME, name) == AI_SUCCESS) {
				std::cout << "Loading material " << name.C_Str() << std::endl;
				mat.name = name.C_Str();
			}

			loadTexture(aiTextureType_DIFFUSE, data, basePath, mat.diffuseTexture, (bool&)mat.settings.useDiffuseTexture);
			loadTexture(aiTextureType_NORMALS, data, basePath, mat.normalTexture, (bool&)mat.settings.useNormalTexture);
			loadTexture(aiTextureType_OPACITY, data, basePath, mat.alphaCutoutTexture, (bool&)mat.settings.useAlphaCutoutTexture);

			loadedMaterials.push_back(mat);
		}

		loadedMesh.reserve(scene->mNumMeshes);

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
			auto vbHR = device->CreateBuffer(&vDesc, &vertData, &mesh.vertices);

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
			auto ibHF = device->CreateBuffer(&iDesc, &indexData, &mesh.indices);

			assert(SUCCEEDED(vbHR));

			mesh.materialId = data->mMaterialIndex;

			loadedMesh.push_back(mesh);
		}

		importer->FreeScene();

		delete importer;
	}

	void loadTexture(aiTextureType type, aiMaterial* materialData, const std::string& baseAssetPath, std::string& outPath, bool& outEnabled) {
		outEnabled = false;
		outPath.clear();
		
		aiString path;
		if (materialData->Get(AI_MATKEY_TEXTURE(type, 0), path) == AI_SUCCESS) {
			outPath += baseAssetPath;
			outPath += path.C_Str();
			
			int width, height, bpp;
			byte* textureData = stbi_load(outPath.c_str(), &width, &height, &bpp, STBI_rgb_alpha);
			if (!textureData) {
				std::stringstream errorString("Failed to load texture ");
				errorString << outPath << " because " << stbi_failure_reason();
				throw std::runtime_error(errorString.str());
			}

			textureCache[outPath] = new Texture(device, context, outPath, width, height, bpp, textureData);

			stbi_image_free(textureData);

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

	void updateFrame() {
		static double lastTime = 0.0f;
		double thisTime = glfwGetTime();
		float delta = static_cast<float>(thisTime - lastTime);
		lastTime = thisTime;

		auto look = XMMatrixRotationRollPitchYaw(pitch, yaw, 0.0f);

		float x = 0;
		x += glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS ? 1.0f : 0.0f;
		x -= glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS ? 1.0f : 0.0f;

		float y = 0;
		y += glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS ? 1.0f : 0.0f;
		y -= glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS ? 1.0f : 0.0f;

		auto moveDir = XMVectorSet(x, 0.0f, y, 0.0f);
		moveDir = XMVector3Normalize(moveDir);
		moveDir = XMVector3Transform(moveDir, look);

		auto moveDelta = XMVectorScale(moveDir, delta * 2.0f);

		auto newPosition = XMVectorAdd(XMLoadFloat3(&cameraPosition), moveDelta);
		XMStoreFloat3(&cameraPosition, newPosition);

		perFrameUniforms.eyePos = cameraPosition;

		auto trans = XMMatrixTranslation(cameraPosition.x, cameraPosition.y, cameraPosition.z);

		auto camera = look * trans;
		auto det = XMMatrixDeterminant(camera);
		auto view = XMMatrixInverse(&det, camera);

		int width, height;
		glfwGetWindowSize(window, &width, &height);
		perFrameUniforms.screenDimensions = { static_cast<float>(width), static_cast<float>(height) };
		perFrameUniforms.view = view;
		XMMATRIX proj;

		float time = static_cast<float>(glfwGetTime());

		lights[0].position.x = std::cosf(time) * 2.0f;
		lights[0].position.z = -std::sin(time) * 2.0f;

		proj = XMMatrixPerspectiveFovLH(45.0f, static_cast<float>(width) / height, 0.1f, 1000.0f);
		perFrameUniforms.viewProj = perFrameUniforms.view * proj;
	}

	void drawFrame() {
		int width, height;
		glfwGetWindowSize(window, &width, &height);

		if (ImGui::BeginMainMenuBar()) {
			ImVec2 mainMenuSize = ImGui::GetWindowSize();

			deferredGraphicsPipeline->viewport.TopLeftY = lightingGraphicsPipeline->viewport.TopLeftY = mainMenuSize.y;
			deferredGraphicsPipeline->scissor.top = lightingGraphicsPipeline->scissor.top = static_cast<uint64_t>(mainMenuSize.y);

			static int currentVisualizedBuffer = -1;
			if (ImGui::BeginMenu("Visualize Buffer")) {
				if (ImGui::MenuItem("Position", nullptr, currentVisualizedBuffer == GeometryBuffer::Buffer::POSITION)) currentVisualizedBuffer = GeometryBuffer::Buffer::POSITION;
				if (ImGui::MenuItem("Normals", nullptr, currentVisualizedBuffer == GeometryBuffer::Buffer::NORMAL)) currentVisualizedBuffer = GeometryBuffer::Buffer::NORMAL;
				if (ImGui::MenuItem("Albedo", nullptr, currentVisualizedBuffer == GeometryBuffer::Buffer::ALBEDO)) currentVisualizedBuffer = GeometryBuffer::Buffer::ALBEDO;
				if (ImGui::MenuItem("None", nullptr, currentVisualizedBuffer == -1)) currentVisualizedBuffer = -1;
				ImGui::EndMenu();
			}

			if (ImGui::MenuItem("Recompile Shaders")) {
				RecompileShaders();
			}

			ImGui::EndMainMenuBar();

			if (currentVisualizedBuffer >= 0) {
				ImGui::SetNextWindowPos({ 0.0f, 0.0f });
				ImGui::SetNextWindowSize({ (float)width, (float)height });
				ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2());
				ImGui::SetNextWindowContentSize({ (float)width, (float)height - mainMenuSize.y });
				if (ImGui::Begin("Visualize", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_NoBringToFrontOnFocus)) {
					ImGui::Image(geometryBuffer.textureResourceViews[currentVisualizedBuffer], ImGui::GetWindowSize());

					ImGui::End();
				}
			}
		}


		//ImGui::Begin("Render Targets");
		//auto size = ImGui::GetItemRectSize();
		//ImGui::Text("GBuffer");
		//float imgWidth = size.x, imgHeight = size.x * height / width;
		//for (size_t i = 0; i < GeometryBuffer::MAX_BUFFER; i++) {
		//	ImGui::Image(geometryBuffer.textureResourceViews[i], { imgWidth, imgHeight });
		//}
		//ImGui::End();

		deferredGraphicsPipeline->bind(context);
		//context->ClearRenderTargetView(multisampleRTV, clearColor);
		//context->OMSetRenderTargets(1, &multisampleRTV, nullptr);

		D3D11_MAPPED_SUBRESOURCE mapped{};
		context->Map(perFrameUniformsBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		memcpy(mapped.pData, &perFrameUniforms, sizeof(PerFrameUniforms));
		context->Unmap(perFrameUniformsBuffer, 0);

		float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		for (size_t i = 0; i < GeometryBuffer::MAX_BUFFER; i++)
		{
			context->ClearRenderTargetView(geometryBuffer.textureViews[i], clearColor);
		}
		context->ClearDepthStencilView(depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0, 0);
		context->OMSetRenderTargets(GeometryBuffer::MAX_BUFFER, geometryBuffer.textureViews, depthStencilView);

		// Deferred passes to geometry buffer
		context->VSSetConstantBuffers(0, 1, &perFrameUniformsBuffer);
		context->PSSetConstantBuffers(0, 1, &perFrameUniformsBuffer);

		uint32_t strides = sizeof(Vertex);
		uint32_t offsets = 0;

		//context->IASetVertexBuffers(0, 1, &vertices, &strides, &offsets);
		//context->IASetIndexBuffer(indices, DXGI_FORMAT_R32_UINT, 0);
		//context->DrawIndexed(numIndices, 0, 0);

		ID3D11SamplerState* nullSampler = nullptr;
		ID3D11ShaderResourceView* nullTexture = nullptr;

		for (const auto& mesh : loadedMesh) {
			auto& mat = loadedMaterials[mesh.materialId];

			if (mat.settings.useDiffuseTexture) {
				auto tex = textureCache[mat.diffuseTexture];

				context->PSSetSamplers(0, 1, &tex->sampler);
				context->PSSetShaderResources(0, 1, &tex->textureSRV);
			}

			if (mat.settings.useNormalTexture) {
				auto tex = textureCache[mat.normalTexture];

				context->PSSetSamplers(1, 1, &tex->sampler);
				context->PSSetShaderResources(1, 1, &tex->textureSRV);
			}

			if (mat.settings.useAlphaCutoutTexture) {
				auto tex = textureCache[mat.alphaCutoutTexture];

				context->PSSetSamplers(2, 1, &tex->sampler);
				context->PSSetShaderResources(2, 1, &tex->textureSRV);
			}

			D3D11_MAPPED_SUBRESOURCE mappedSettings{};
			context->Map(perMaterialUniformsBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSettings);
			memcpy(mappedSettings.pData, &mat.settings, sizeof(MaterialCbuffer));
			context->Unmap(perMaterialUniformsBuffer, 0);

			context->PSSetConstantBuffers(1, 1, &perMaterialUniformsBuffer);

			context->IASetVertexBuffers(0, 1, &mesh.vertices, &strides, &offsets);
			context->IASetIndexBuffer(mesh.indices, DXGI_FORMAT_R32_UINT, 0);
			context->DrawIndexed(mesh.indexCount, 0, 0);
		}

		// Light accumulation pass to the backbuffer
		ID3D11Texture2D* backBuffer;
		if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
			throw std::runtime_error("Failed to get a back buffer!");
		}

		ID3D11RenderTargetView* renderTarget;

		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
		rtvDesc.Format = swapChainFormat;
		rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		rtvDesc.Texture2D.MipSlice = 0;

		if (FAILED(device->CreateRenderTargetView(backBuffer, &rtvDesc, &renderTarget))) {
			throw std::runtime_error("Failed to create backbuffer RTV!");
		}

		backBuffer->Release();

		context->ClearRenderTargetView(renderTarget, clearColor);
		context->OMSetRenderTargets(1, &renderTarget, nullptr);

		lightingGraphicsPipeline->bind(context);
		context->PSSetShaderResources(0, GeometryBuffer::MAX_BUFFER, geometryBuffer.textureResourceViews);

		context->PSSetSamplers(0, 1, &gbufferSampler);

		// Draw light mesh
		for (size_t i = 0; i < lights.size(); i++)
		{
			lighting->DrawPointLight(context, lights[i]);
		}

		ID3D11ShaderResourceView* nullSRVs[GeometryBuffer::MAX_BUFFER];
		memset(nullSRVs, 0, sizeof(nullSRVs));
		context->PSSetShaderResources(0, GeometryBuffer::MAX_BUFFER, nullSRVs);

		//context->ResolveSubresource(backBuffer, 0, multisampleTexture, 0, swapChainFormat);

		ImGui::Render();
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		swapChain->Present(1, 0);
		renderTarget->Release();
	}

	void RecompileShaders() {
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

		device->CreateVertexShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &newVertShader);
		if (errors)
			errors->Release();
		bytecode->Release();

		if (FAILED(D3DCompileFromFile(L"shaders/deferredPixel.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "ps_5_0", 0, 0, &bytecode, &errors))) {
			std::wcout << L"deferred pshader error" << (char*)errors->GetBufferPointer() << std::endl;
			if (errors)
				errors->Release();
			return;
		}

		device->CreatePixelShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &newPixelShader);
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

		device->CreateVertexShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &newVertShader);
		if (errors)
			errors->Release();
		bytecode->Release();

		if (FAILED(D3DCompileFromFile(L"shaders/lightAccPixel.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "ps_5_0", 0, 0, &bytecode, &errors))) {
			std::wcout << L"lightAcc pshader error: " << (char*)errors->GetBufferPointer() << std::endl;
			if (errors)
				errors->Release();
			return;
		}

		device->CreatePixelShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &newPixelShader);
		if (errors)
			errors->Release();
		bytecode->Release();

		lightingGraphicsPipeline->vertexShader->Release();
		lightingGraphicsPipeline->pixelShader->Release();
		lightingGraphicsPipeline->vertexShader = newVertShader;
		lightingGraphicsPipeline->pixelShader = newPixelShader;

		std::cout << "Successfully hot reloader lighting pass shaders" << std::endl;
	}

	void OnWindowResized(uint32_t width, uint32_t height) {
		swapChain->ResizeBuffers(numSwapChainBuffers, width, height, swapChainFormat, 0);

		D3D11_TEXTURE2D_DESC dstDesc;
		D3D11_DEPTH_STENCIL_VIEW_DESC dstViewDesc;
		depthTexture->GetDesc(&dstDesc);
		depthStencilView->GetDesc(&dstViewDesc);

		depthTexture->Release();
		depthStencilView->Release();

		dstDesc.Width = width;
		dstDesc.Height = height;

		HRESULT hr;
		hr = device->CreateTexture2D(&dstDesc, nullptr, &depthTexture);
		if (FAILED(hr) || !depthTexture) {
			throw std::runtime_error("Failed to create resized Depth Stencil texture!");
		}
		
		hr = device->CreateDepthStencilView(depthTexture, &dstViewDesc, &depthStencilView);
		if (FAILED(hr) || !depthStencilView) {
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

			hr = device->CreateTexture2D(&texDesc, nullptr, &geometryBuffer.textures[i]);
			if (FAILED(hr) || !geometryBuffer.textures[i]) {
				throw std::runtime_error("Failed to create resized GBuffer texture!");
			}

			hr = device->CreateRenderTargetView(geometryBuffer.textures[i], &rtvDesc, &geometryBuffer.textureViews[i]);
			if (FAILED(hr) || !geometryBuffer.textureViews[i]) {
				throw std::runtime_error("Failed to create resized GBuffer texture RTV!");
			}

			hr = device->CreateShaderResourceView(geometryBuffer.textures[i], &srvDesc, &geometryBuffer.textureResourceViews[i]);
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

int main(int argc, char** argv) {
	try {
		Application app;
		app.run();
	}
	catch (std::runtime_error e) {
		std::cout << e.what() << std::endl;
		__debugbreak();

		return -1;
	}

	return 0;
}