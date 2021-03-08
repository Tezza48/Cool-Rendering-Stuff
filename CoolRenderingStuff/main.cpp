#include <d3d11_4.h>
#include <dxgi.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <stdexcept>
#include <iostream>
#include <sstream>
#include <vector>
#include <fstream>
#include <cassert>

#include <DirectXMath.h>

using namespace DirectX;

#define PI 3.1415927f

struct GraphicsPipeline {
	D3D_PRIMITIVE_TOPOLOGY primitiveTopology;

	ID3D11VertexShader* vertexShader;
	ID3D11RasterizerState* rasterizerState;
	ID3D11PixelShader* pixelShader;

	D3D11_VIEWPORT viewport;
	D3D11_RECT scissor;

	~GraphicsPipeline() {
		vertexShader->Release();
		rasterizerState->Release();
		pixelShader->Release();
	}

	void bind(ID3D11DeviceContext* context) {
		context->IASetPrimitiveTopology(primitiveTopology);

		if (vertexShader)
			context->VSSetShader(vertexShader, nullptr, 0);

		context->RSSetState(rasterizerState);
		context->RSSetViewports(1, &viewport);
		context->RSSetScissorRects(1, &scissor);

		if (pixelShader)
			context->PSSetShader(pixelShader, nullptr, 0);
	}
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
		DXGI_FORMAT_R16G16B16A16_UNORM,
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
	};

	ID3D11Texture2D* textures[MAX_BUFFER];
	ID3D11RenderTargetView* textureViews[MAX_BUFFER];
	ID3D11ShaderResourceView* textureResourceViews[MAX_BUFFER];

	~GeometryBuffer() {
		for (size_t i = 0; i < MAX_BUFFER; i++) {
			textureResourceViews[i]->Release();
			textureViews[i]->Release();
			textures[i]->Release();
		}
	}

	void bind(ID3D11DeviceContext* context, ID3D11DepthStencilView* depthStencilView = nullptr) {
		context->OMSetRenderTargets(MAX_BUFFER, textureViews, depthStencilView);
	}
};

struct PerFrameUniforms {
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
		static float lastX = 0.0f;
		static float lastY = 0.0f;

		auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));

		if (isFirstTime) {
			lastX = x;
			lastY = y;
			isFirstTime = false;
		}

		float extent = PI - 0.01f;

		app->yaw += (static_cast<float>(x) - lastX) / width;
		app->pitch += (static_cast<float>(y) - lastY) / height;

		app->yaw = std::fmodf(app->yaw, PI * 2.0f);
		app->pitch = std::fmaxf(-extent, std::fminf(extent, app->pitch));

		lastX = (float)x;
		lastY = (float)y;
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

	GraphicsPipeline deferredGraphicsPipeline;
	GraphicsPipeline lightingGraphicsPipeline;

	//ID3D11Texture2D* multisampleTexture;
	//ID3D11RenderTargetView* multisampleRTV;

	//bool useMultisampling = true;
	//DXGI_FORMAT multisampleFormat = DXGI_FORMAT_UNKNOWN;
	//uint32_t multisampleCount = 4;
	//int multisampleQuality = D3D11_STANDARD_MULTISAMPLE_PATTERN;

	float yaw;
	float pitch;

	ID3D11Buffer* perFrameUniformsBuffer;
	PerFrameUniforms perFrameUniforms;

	ID3D11SamplerState* gbufferSampler;
	GeometryBuffer geometryBuffer;

public:
	Application() {
		createWindow();
		createDeviceAndSwapChain();
		createDeferredGraphicsPipeline();
		createLightingGraphicsPipeline();
		createConstantBuffers();
		createGbuffers();
	}

	~Application() {
		//multisampleRTV->Release();
		//multisampleTexture->Release();

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

			if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
				glfwSetWindowShouldClose(window, true);
			}

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

		window = glfwCreateWindow(1600, 900, "D3D11 Application", nullptr, nullptr);
		if (!window) {
			throw std::runtime_error("Failed to create window");
		}

		glfwSetWindowUserPointer(window, this);

		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

		glfwSetWindowSizeCallback(window, GlfwWindowSizeCallback);
		glfwSetCursorPosCallback(window, GlfwCursorPosCallback);
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

		//D3D11_TEXTURE2D_DESC msDesc{};
		//msDesc.Width = width;
		//msDesc.Height = height;
		//msDesc.MipLevels = 1;
		//msDesc.ArraySize = 1;
		//msDesc.Format = multisampleFormat;
		//msDesc.SampleDesc.Count = multisampleCount;
		//msDesc.SampleDesc.Quality = multisampleQuality;
		//msDesc.Usage = D3D11_USAGE_DEFAULT;
		//msDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
		//msDesc.CPUAccessFlags = 0;
		//msDesc.MiscFlags = 0;

		//device->CreateTexture2D(&msDesc, nullptr, &multisampleTexture);

		//if (!multisampleTexture) {
		//	throw std::runtime_error("Failed to create multisample texture!");
		//}

		//D3D11_RENDER_TARGET_VIEW_DESC msRTVDesc{};
		//msRTVDesc.Format = multisampleFormat;
		//msRTVDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;

		//device->CreateRenderTargetView(multisampleTexture, &msRTVDesc, &multisampleRTV);

		//if (!multisampleRTV) {
		//	throw std::runtime_error("Failed to create multisample RTV!");
		//}
	}

	void createDeferredGraphicsPipeline() {
		std::vector<char> vertexShaderCode = readFile("shaders/deferredVertex.cso");
		std::vector<char> pixelShaderCode = readFile("shaders/deferredPixel.cso");

		ID3D11VertexShader* vertexShader;
		ID3D11PixelShader* pixelShader;

		device->CreateVertexShader(vertexShaderCode.data(), vertexShaderCode.size(), nullptr, &vertexShader);
		device->CreatePixelShader(pixelShaderCode.data(), pixelShaderCode.size(), nullptr, &pixelShader);

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

		ID3D11RasterizerState* rasterizerState;
		device->CreateRasterizerState(&rasterizerDesc, &rasterizerState);

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

		deferredGraphicsPipeline.primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		deferredGraphicsPipeline.vertexShader = vertexShader;
		deferredGraphicsPipeline.rasterizerState = rasterizerState;
		deferredGraphicsPipeline.pixelShader = pixelShader;
		deferredGraphicsPipeline.viewport = viewport;
		deferredGraphicsPipeline.scissor = scissor;
	}

	void createLightingGraphicsPipeline() {
		std::vector<char> vertexShaderCode = readFile("shaders/lightAccVertex.cso");
		std::vector<char> pixelShaderCode = readFile("shaders/lightAccPixel.cso");

		ID3D11VertexShader* vertexShader;
		ID3D11PixelShader* pixelShader;

		device->CreateVertexShader(vertexShaderCode.data(), vertexShaderCode.size(), nullptr, &vertexShader);
		device->CreatePixelShader(pixelShaderCode.data(), pixelShaderCode.size(), nullptr, &pixelShader);

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

		ID3D11RasterizerState* rasterizerState;
		device->CreateRasterizerState(&rasterizerDesc, &rasterizerState);

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

		lightingGraphicsPipeline.primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
		lightingGraphicsPipeline.vertexShader = vertexShader;
		lightingGraphicsPipeline.rasterizerState = rasterizerState;
		lightingGraphicsPipeline.pixelShader = pixelShader;
		lightingGraphicsPipeline.viewport = viewport;
		lightingGraphicsPipeline.scissor = scissor;

		float borderColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
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
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(PerFrameUniforms);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = 0;
		desc.StructureByteStride = 0;

		if (FAILED(device->CreateBuffer(&desc, nullptr, &perFrameUniformsBuffer))) {
			throw std::runtime_error("Failed to create cbuffer!");
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
			
			//if (useMultisampling) {
			//	textureDesc.SampleDesc.Count = multisampleCount;
			//	textureDesc.SampleDesc.Quality = multisampleQuality;
			//}
			//else {
			//}
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

	void updateFrame() {
		auto look = XMMatrixRotationRollPitchYaw(pitch, yaw, 0.0f);
		auto trans = XMMatrixTranslation(0.0f, 0.0f, -1.0f);

		auto camera = look * trans;
		auto det = XMMatrixDeterminant(camera);
		auto view = XMMatrixInverse(&det, camera);

		perFrameUniforms.view = view;
		XMMATRIX proj;
		int width, height;
		glfwGetWindowSize(window, &width, &height);

		proj = XMMatrixPerspectiveFovLH(90.0f, static_cast<float>(width) / height, 0.1f, 100.0f);
		perFrameUniforms.viewProj = perFrameUniforms.view * proj;
	}

	void drawFrame() {
		deferredGraphicsPipeline.bind(context);
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
		context->OMSetRenderTargets(GeometryBuffer::MAX_BUFFER, geometryBuffer.textureViews, nullptr);

		// Drawing happens here.

		context->VSSetConstantBuffers(0, 1, &perFrameUniformsBuffer);
		context->PSSetConstantBuffers(0, 1, &perFrameUniformsBuffer);

		context->Draw(3, 0);

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

		lightingGraphicsPipeline.bind(context);
		context->PSSetShaderResources(0, GeometryBuffer::MAX_BUFFER, geometryBuffer.textureResourceViews);

		// Draw light mesh
		context->Draw(4, 0);

		ID3D11ShaderResourceView* nullSRVs[GeometryBuffer::MAX_BUFFER];
		memset(nullSRVs, 0, sizeof(nullSRVs));
		context->PSSetShaderResources(0, GeometryBuffer::MAX_BUFFER, nullSRVs);

		context->PSSetSamplers(0, 1, &gbufferSampler);

		//context->ResolveSubresource(backBuffer, 0, multisampleTexture, 0, swapChainFormat);

		swapChain->Present(1, 0);
		renderTarget->Release();
	}

	void OnWindowResized(uint32_t width, uint32_t height) {
		swapChain->ResizeBuffers(numSwapChainBuffers, width, height, swapChainFormat, 0);

		//D3D11_TEXTURE2D_DESC textureDesc;
		//multisampleTexture->GetDesc(&textureDesc);
		//multisampleTexture->Release();

		//D3D11_RENDER_TARGET_VIEW_DESC rtvDesc;
		//multisampleRTV->GetDesc(&rtvDesc);
		//multisampleRTV->Release();

		//textureDesc.Width = width;
		//textureDesc.Height = height;

		//if (FAILED(device->CreateTexture2D(&textureDesc, nullptr, &multisampleTexture))) {
		//	throw std::runtime_error("Failed to recreate multisample texture!");
		//}
		//assert(multisampleTexture);

		//if (FAILED(device->CreateRenderTargetView(multisampleTexture, &rtvDesc, &multisampleRTV))) {
		//	throw std::runtime_error("Failed to recreate multisample texture RTV!");
		//}
		//assert(multisampleRTV);

		deferredGraphicsPipeline.viewport.Width = static_cast<float>(width);
		deferredGraphicsPipeline.viewport.Height = static_cast<float>(height);

		deferredGraphicsPipeline.scissor.right = width;
		deferredGraphicsPipeline.scissor.bottom = height;
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