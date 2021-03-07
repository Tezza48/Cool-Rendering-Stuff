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

class Application {
public:
	static void GlfwErrorCallback(int error, const char* description) {
		std::cout << "Glfw error " << std::hex << error << std::dec << ": " << description << "\n";
	}

	static void GlfwWindowSizeCallback(GLFWwindow* window, int width, int height) {
		auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));

		app->OnWindowResized(width, height);
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
	GraphicsPipeline* graphicsPipeline;

	ID3D11Texture2D* multisampleTexture;
	ID3D11RenderTargetView* multisampleRTV;

	bool useMultisampling = true;
	DXGI_FORMAT multisampleFormat = DXGI_FORMAT_UNKNOWN;
	uint32_t multisampleCount = 4;
	int multisampleQuality = D3D11_STANDARD_MULTISAMPLE_PATTERN;

public:
	Application() {
		createWindow();
		createDeviceAndSwapChain();
		createGraphicsPipeline();
	}

	~Application() {
		delete graphicsPipeline;

		multisampleRTV->Release();
		multisampleTexture->Release();

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

		glfwSetWindowSizeCallback(window, GlfwWindowSizeCallback);
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
		multisampleFormat = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;

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

		D3D11_TEXTURE2D_DESC msDesc{};
		msDesc.Width = width;
		msDesc.Height = height;
		msDesc.MipLevels = 1;
		msDesc.ArraySize = 1;
		msDesc.Format = multisampleFormat;
		msDesc.SampleDesc.Count = multisampleCount;
		msDesc.SampleDesc.Quality = multisampleQuality;
		msDesc.Usage = D3D11_USAGE_DEFAULT;
		msDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
		msDesc.CPUAccessFlags = 0;
		msDesc.MiscFlags = 0;

		device->CreateTexture2D(&msDesc, nullptr, &multisampleTexture);

		if (!multisampleTexture) {
			throw std::runtime_error("Failed to create multisample texture!");
		}

		D3D11_RENDER_TARGET_VIEW_DESC msRTVDesc{};
		msRTVDesc.Format = multisampleFormat;
		msRTVDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;

		device->CreateRenderTargetView(multisampleTexture, &msRTVDesc, &multisampleRTV);

		if (!multisampleRTV) {
			throw std::runtime_error("Failed to create multisample RTV!");
		}
	}

	void createGraphicsPipeline() {
		std::vector<char> vertexShaderCode = readFile("shaders/vertex.cso");
		std::vector<char> pixelShaderCode = readFile("shaders/pixel.cso");

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
		rasterizerDesc.MultisampleEnable = useMultisampling;
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

		graphicsPipeline = new GraphicsPipeline();
		graphicsPipeline->primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		graphicsPipeline->vertexShader = vertexShader;
		graphicsPipeline->rasterizerState = rasterizerState;
		graphicsPipeline->pixelShader = pixelShader;
		graphicsPipeline->viewport = viewport;
		graphicsPipeline->scissor = scissor;
	}

	void drawFrame() {
		graphicsPipeline->bind(context);

		float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		context->ClearRenderTargetView(multisampleRTV, clearColor);
		context->OMSetRenderTargets(1, &multisampleRTV, nullptr);

		// Drawing happens here.

		context->Draw(3, 0);

		ID3D11Texture2D* backBuffer;
		if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
			throw std::runtime_error("Failed to get a back buffer!");
		}

		context->ResolveSubresource(backBuffer, 0, multisampleTexture, 0, swapChainFormat);

		backBuffer->Release();

		swapChain->Present(1, 0);
	}

	void OnWindowResized(uint32_t width, uint32_t height) {
		swapChain->ResizeBuffers(numSwapChainBuffers, width, height, swapChainFormat, 0);

		D3D11_TEXTURE2D_DESC textureDesc;
		multisampleTexture->GetDesc(&textureDesc);
		multisampleTexture->Release();

		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc;
		multisampleRTV->GetDesc(&rtvDesc);
		multisampleRTV->Release();

		textureDesc.Width = width;
		textureDesc.Height = height;

		if (FAILED(device->CreateTexture2D(&textureDesc, nullptr, &multisampleTexture))) {
			throw std::runtime_error("Failed to recreate multisample texture!");
		}
		assert(multisampleTexture);

		if (FAILED(device->CreateRenderTargetView(multisampleTexture, &rtvDesc, &multisampleRTV))) {
			throw std::runtime_error("Failed to recreate multisample texture RTV!");
		}
		assert(multisampleRTV);

		graphicsPipeline->viewport.Width = static_cast<float>(width);
		graphicsPipeline->viewport.Height = static_cast<float>(height);

		graphicsPipeline->scissor.right = width;
		graphicsPipeline->scissor.bottom = height;
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