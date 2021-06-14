#define NOMINMAX
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
#include <iomanip>
#include <thread>
#include <unordered_map>
#include <filesystem>
#include <set>

#include <DirectXMath.h>
#include <DirectXColors.h>
#include <d3dcompiler.h>

#include "GraphicsPipeline.h"

#include "vendor/imgui/imgui.h"
#include "vendor/imgui/imgui_impl_glfw.h"
#include "vendor/imgui/imgui_impl_dx11.h"

#include "ECS.h"
#include "Lighting.h"
#include "Application.h"

using namespace DirectX;

#define PI 3.1415927f

bool system_setup_window(ECS* ecs) {
	if (!glfwInit()) {
		throw std::runtime_error("Failed to init glfw");
	}

	glfwSetErrorCallback(ResourceWindow::GlfwErrorCallback);

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	ecs->resources.window.window = glfwCreateWindow(1280, 720, "D3D11 Application", nullptr, nullptr);
	if (!ecs->resources.window.window) {
		throw std::runtime_error("Failed to create window");
	}

	glfwSetWindowUserPointer(ecs->resources.window.window, ecs);

	glfwSetInputMode(ecs->resources.window.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	glfwSetWindowSizeCallback(ecs->resources.window.window, ResourceWindow::GlfwWindowSizeCallback);
	glfwSetCursorPosCallback(ecs->resources.window.window, ResourceWindow::GlfwCursorPosCallback);
	glfwSetKeyCallback(ecs->resources.window.window, ResourceWindow::GlfwKeyCallback);
	glfwSetMouseButtonCallback(ecs->resources.window.window, ResourceWindow::GlfwMouseButtonCallback);

	return false;
}

bool system_setup_graphics_core(ECS* ecs) {
	auto& graphics = ecs->resources.graphicsCore;

	uint32_t flags = D3D11_CREATE_DEVICE_SINGLETHREADED;

#if DEBUG || _DEBUG
	flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0
	};

	int32_t width, height;
	glfwGetWindowSize(ecs->resources.window.window, &width, &height);

	ecs->resources.graphicsCore.numSwapChainBuffers = 2;
	ecs->resources.graphicsCore.swapChainFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
	//multisampleFormat = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;

	HWND hwnd = glfwGetWin32Window(ecs->resources.window.window);

	DXGI_SWAP_CHAIN_DESC swapChainDesc{};
	swapChainDesc.BufferDesc.Width = static_cast<uint32_t>(width);
	swapChainDesc.BufferDesc.Height = static_cast<uint32_t>(height);
	swapChainDesc.BufferDesc.RefreshRate.Numerator = 1;
	swapChainDesc.BufferDesc.RefreshRate.Denominator = 60;
	swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	swapChainDesc.BufferDesc.Format = ecs->resources.graphicsCore.swapChainFormat;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = ecs->resources.graphicsCore.numSwapChainBuffers; // TODO WT: 3 buffers for mailbox?
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
		&ecs->resources.graphicsCore.swapChain,
		&ecs->resources.graphicsCore.device,
		nullptr,
		&ecs->resources.graphicsCore.context);

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

	hr = ecs->resources.graphicsCore.device->CreateTexture2D(&depthTextureDesc, nullptr, &ecs->resources.graphicsCore.depthTexture);
	if (FAILED(hr)) {
		throw std::runtime_error("Failed to create depth texture!");
	}

	D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Texture2D.MipSlice = 0;

	hr = ecs->resources.graphicsCore.device->CreateDepthStencilView(ecs->resources.graphicsCore.depthTexture, &dsvDesc, &ecs->resources.graphicsCore.depthStencilView);
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

	ImGui_ImplGlfw_InitForOther(ecs->resources.window.window, true);
	ImGui_ImplDX11_Init(ecs->resources.graphicsCore.device, ecs->resources.graphicsCore.context);

	return false;
}

bool system_setup_deferred_graphics_pipelines(ECS* ecs) {
	// TODO WT: Move readFile to a better namespace
	std::vector<char> vertexShaderCode = Application::readFile("shaders/deferredVertex.cso");
	std::vector<char> pixelShaderCode = Application::readFile("shaders/deferredPixel.cso");

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
	glfwGetWindowSize(ecs->resources.window.window, &width, &height);

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

	ecs->resources.deferredGraphicsPipeline = new GraphicsPipeline(
		ecs->resources.graphicsCore.device,
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

	return false;
}

bool system_setup_lighting_graphics_pipelines(ECS* ecs) {
	ecs->resources.lighting = new Lighting(ecs->resources.graphicsCore.device);

	std::vector<char> vertexShaderCode = Application::readFile("shaders/lightAccVertex.cso");
	std::vector<char> pixelShaderCode = Application::readFile("shaders/lightAccPixel.cso");

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
	glfwGetWindowSize(ecs->resources.window.window, &width, &height);

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

	ecs->resources.lightingGraphicsPipeline = new GraphicsPipeline(
		ecs->resources.graphicsCore.device,
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

	return false;
}

bool system_init_application(ECS* ecs) {
	ecs->resources.application = new Application();
	ecs->resources.application->Init(ecs);

	return false;
}

bool system_start(ECS* ecs) {
	ecs->resources.flyCam.position = { 0.0, 1.0, 0.0 };

	auto light = ecs->addEntity();
	ecs->entities[light].light.emplace(XMFLOAT3(0.0f, 1.0f, 0.0f), 2.0f, XMFLOAT3(1.0f, 1.0f, 1.0f), 1.0f, XMFLOAT4(0.1f, 0.1f, 0.1f, 1.0f));
	ecs->entities[light].movesInCircle.emplace();

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
		ecs->entities[light].position.emplace(XMFLOAT3{ rand0 * 30.0f - 15.0f, rand2 * 10.0f, rand1 * 20.0f - 10.0f });
		ecs->entities[light].light.emplace(
			ecs->entities[light].position.value(),
			5.0f,
			XMFLOAT3{ 1.0f, 1.0f, 1.0f },
			1.0f,
			XMFLOAT4{ 0.0f, 0.0f, 0.0f, 0.0f }
		);
		ecs->entities[light].movesInCircle.emplace(ComponentMovesInCircle{ ecs->entities[light].position.value(), 1.0f });
		XMStoreFloat3(&ecs->entities[light].light.value().color, colors[i % 6]);
	}

	return false;
}

bool system_move_in_circle(ECS* ecs) {
	float time = static_cast<float>(glfwGetTime());

	for (int i = -0; i < ecs->nextEntity; i++) {
		auto& entity = ecs->entities[i];
		if (entity.light.has_value() && entity.movesInCircle.has_value() && entity.position.has_value()) {
			auto& position = entity.position.value();
			auto& light = entity.light.value();
			auto& moves = entity.movesInCircle.value();

			position.x = moves.origin.x + std::cosf(time) * 2.0f;
			position.z = moves.origin.z - std::sin(time) * 2.0f;
		}
	}

	return true;
}

bool system_flycam(ECS* ecs) {
	static double lastTime = 0.0f;
	double thisTime = glfwGetTime();
	float delta = static_cast<float>(thisTime - lastTime);
	lastTime = thisTime;

	auto look = XMMatrixRotationRollPitchYaw(ecs->resources.flyCam.pitch, ecs->resources.flyCam.yaw, 0.0f);

	float x = 0;
	x += glfwGetKey(ecs->resources.window.window, GLFW_KEY_D) == GLFW_PRESS ? 1.0f : 0.0f;
	x -= glfwGetKey(ecs->resources.window.window, GLFW_KEY_A) == GLFW_PRESS ? 1.0f : 0.0f;

	float y = 0;
	y += glfwGetKey(ecs->resources.window.window, GLFW_KEY_W) == GLFW_PRESS ? 1.0f : 0.0f;
	y -= glfwGetKey(ecs->resources.window.window, GLFW_KEY_S) == GLFW_PRESS ? 1.0f : 0.0f;

	auto moveDir = XMVectorSet(x, 0.0f, y, 0.0f);
	moveDir = XMVector3Normalize(moveDir);
	moveDir = XMVector3Transform(moveDir, look);

	auto moveDelta = XMVectorScale(moveDir, delta * 2.0f);

	auto newPosition = XMVectorAdd(XMLoadFloat3(&ecs->resources.flyCam.position), moveDelta);
	XMStoreFloat3(&ecs->resources.flyCam.position, newPosition);

	ecs->resources.application->perFrameUniforms.eyePos = ecs->resources.flyCam.position;

	auto trans = XMMatrixTranslation(ecs->resources.flyCam.position.x, ecs->resources.flyCam.position.y, ecs->resources.flyCam.position.z);

	auto camera = look * trans;
	auto det = XMMatrixDeterminant(camera);
	auto view = XMMatrixInverse(&det, camera);

	int width, height;
	glfwGetWindowSize(ecs->resources.window.window, &width, &height);
	ecs->resources.application->perFrameUniforms.screenDimensions = { static_cast<float>(width), static_cast<float>(height) };
	ecs->resources.application->perFrameUniforms.view = view;
	XMMATRIX proj;

	proj = XMMatrixPerspectiveFovLH(45.0f, static_cast<float>(width) / height, 0.1f, 1000.0f);
	ecs->resources.application->perFrameUniforms.viewProj = ecs->resources.application->perFrameUniforms.view * proj;

	return true;
}

bool system_new_imgui_frame(ECS* ecs) {
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	return true;
}

bool system_draw_imgui_debug_ui(ECS* ecs)
{
	int width, height;
	glfwGetWindowSize(ecs->resources.window.window, &width, &height);

	if (ImGui::BeginMainMenuBar()) {
		ImVec2 mainMenuSize = ImGui::GetWindowSize();

		ecs->resources.deferredGraphicsPipeline->scissor.top = ecs->resources.lightingGraphicsPipeline->scissor.top = static_cast<uint64_t>(mainMenuSize.y);

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
			ecs->resources.application->RecompileShaders(ecs);
		}

		ImGui::EndMainMenuBar();

		if (currentVisualizedBuffer >= 0) {
			ImGui::SetNextWindowPos({ 0.0f, 0.0f });
			ImGui::SetNextWindowSize({ (float)width, (float)height });
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2());
			ImGui::SetNextWindowContentSize({ (float)width, (float)height - mainMenuSize.y });
			if (ImGui::Begin("Visualize", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_NoBringToFrontOnFocus)) {
				ImGui::Image(ecs->resources.application->geometryBuffers.textureResourceViews[currentVisualizedBuffer], ImGui::GetWindowSize());

				ImGui::End();
			}
		}
	}

	return true;
}

// System which just draws the sponza model
bool system_draw_models(ECS* ecs)
{
	int width, height;
	glfwGetWindowSize(ecs->resources.window.window, &width, &height);

	ecs->resources.deferredGraphicsPipeline->bind(ecs->resources.graphicsCore.context);
	//context->ClearRenderTargetView(multisampleRTV, clearColor);
	//context->OMSetRenderTargets(1, &multisampleRTV, nullptr);

	D3D11_MAPPED_SUBRESOURCE mapped{};
	ecs->resources.graphicsCore.context->Map(ecs->resources.application->perFrameUniformsBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	memcpy(mapped.pData, &ecs->resources.application->perFrameUniforms, sizeof(PerFrameUniforms));
	ecs->resources.graphicsCore.context->Unmap(ecs->resources.application->perFrameUniformsBuffer, 0);

	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	for (size_t i = 0; i < GeometryBuffer::MAX_BUFFER; i++)
	{
		ecs->resources.graphicsCore.context->ClearRenderTargetView(ecs->resources.application->geometryBuffers.textureViews[i], clearColor);
	}
	ecs->resources.graphicsCore.context->ClearDepthStencilView(ecs->resources.graphicsCore.depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0, 0);
	//ecs->resources.graphicsCore.context->OMSetRenderTargets(GeometryBuffer::MAX_BUFFER, ecs->resources.application->geometryBuffer.textureViews, );
	ecs->resources.application->geometryBuffers.bindBuffers(ecs->resources.graphicsCore.context, ecs->resources.graphicsCore.depthStencilView);

	// Deferred passes to geometry buffer
	ecs->resources.graphicsCore.context->VSSetConstantBuffers(0, 1, &ecs->resources.application->perFrameUniformsBuffer);
	ecs->resources.graphicsCore.context->PSSetConstantBuffers(0, 1, &ecs->resources.application->perFrameUniformsBuffer);

	uint32_t strides = sizeof(Vertex);
	uint32_t offsets = 0;

	//context->IASetVertexBuffers(0, 1, &vertices, &strides, &offsets);
	//context->IASetIndexBuffer(indices, DXGI_FORMAT_R32_UINT, 0);
	//context->DrawIndexed(numIndices, 0, 0);

	ID3D11ShaderResourceView* nullTexture = nullptr;

	for (const auto& entity : ecs->entities) {
		if (!entity.position.has_value() || !entity.renderModel.has_value()) continue;

		auto& mesh = ecs->resources.modelCache[entity.renderModel.value().meshId];

		// TODO WT: Fall back to mesh.defaultMaterialId if materialId is not on component.
		auto& mat = ecs->resources.materialCache[entity.renderModel.value().materialId];

		ID3D11ShaderResourceView* views[] = {
			(mat.settings.useDiffuseTexture) ? ecs->resources.textureCache[mat.diffuseTexture]->textureSRV : nullTexture,
			(mat.settings.useNormalTexture) ? ecs->resources.textureCache[mat.normalTexture]->textureSRV : nullTexture,
			(mat.settings.useAlphaCutoutTexture) ? ecs->resources.textureCache[mat.alphaCutoutTexture]->textureSRV : nullTexture,
			(mat.settings.useSpecularTexture) ? ecs->resources.textureCache[mat.specularTexture]->textureSRV : nullTexture,
		};

		// TODO WT: Make a default sampler to fall back on instead of the gbuffer sampler
		const auto& defaultSampler = ecs->resources.application->geometryBuffers.sampler;
		ID3D11SamplerState* samplers[] = {
			(mat.settings.useDiffuseTexture) ? ecs->resources.textureCache[mat.diffuseTexture]->sampler : defaultSampler,
			(mat.settings.useNormalTexture) ? ecs->resources.textureCache[mat.normalTexture]->sampler : defaultSampler,
			(mat.settings.useAlphaCutoutTexture) ? ecs->resources.textureCache[mat.alphaCutoutTexture]->sampler : defaultSampler,
			(mat.settings.useSpecularTexture) ? ecs->resources.textureCache[mat.specularTexture]->sampler : defaultSampler,
		};

		ecs->resources.graphicsCore.context->PSSetShaderResources(0, 4, views);
		ecs->resources.graphicsCore.context->PSSetSamplers(0, 4, samplers);

		D3D11_MAPPED_SUBRESOURCE mappedSettings{};
		ecs->resources.graphicsCore.context->Map(ecs->resources.application->perMaterialUniformsBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSettings);
		memcpy(mappedSettings.pData, &mat.settings, sizeof(MaterialCbuffer));
		ecs->resources.graphicsCore.context->Unmap(ecs->resources.application->perMaterialUniformsBuffer, 0);

		ecs->resources.graphicsCore.context->PSSetConstantBuffers(1, 1, &ecs->resources.application->perMaterialUniformsBuffer);

		ecs->resources.graphicsCore.context->IASetVertexBuffers(0, 1, &mesh.vertices, &strides, &offsets);
		ecs->resources.graphicsCore.context->IASetIndexBuffer(mesh.indices, DXGI_FORMAT_R32_UINT, 0);
		ecs->resources.graphicsCore.context->DrawIndexed(mesh.indexCount, 0, 0);
	}
	return true;
}

bool system_draw_lights(ECS* ecs) {
	// TODO WT: This should be a resource, it's pretty global.
	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

	ID3D11Texture2D* backBuffer;
	if (FAILED(ecs->resources.graphicsCore.swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
		throw std::runtime_error("Failed to get a back buffer!");
	}

	ID3D11RenderTargetView* renderTarget;

	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = ecs->resources.graphicsCore.swapChainFormat;
	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;

	if (FAILED(ecs->resources.graphicsCore.device->CreateRenderTargetView(backBuffer, &rtvDesc, &renderTarget))) {
		throw std::runtime_error("Failed to create backbuffer RTV!");
	}

	backBuffer->Release();

	ecs->resources.graphicsCore.context->ClearRenderTargetView(renderTarget, clearColor);
	ecs->resources.graphicsCore.context->OMSetRenderTargets(1, &renderTarget, nullptr);

	ecs->resources.lightingGraphicsPipeline->bind(ecs->resources.graphicsCore.context);
	ecs->resources.graphicsCore.context->PSSetShaderResources(0, GeometryBuffer::MAX_BUFFER, ecs->resources.application->geometryBuffers.textureResourceViews);

	ecs->resources.graphicsCore.context->PSSetSamplers(0, 1, &ecs->resources.application->geometryBuffers.sampler);

	// Draw light mesh
	for (size_t i = 0; i < ecs->nextEntity; i++)
	{
		auto& light = ecs->entities[i];
		if (!light.light.has_value() || !light.position.has_value()) continue;
		light.light.value().position = light.position.value();
		ecs->resources.lighting->DrawPointLight(ecs->resources.graphicsCore.context, light.light.value());
	}

	ID3D11ShaderResourceView* nullSRVs[GeometryBuffer::MAX_BUFFER];
	memset(nullSRVs, 0, sizeof(nullSRVs));
	ecs->resources.graphicsCore.context->PSSetShaderResources(0, GeometryBuffer::MAX_BUFFER, nullSRVs);

	// TODO WT: Might cause error releasing now?
	renderTarget->Release();

	return true;
}

// TODO WT: Forward pass after light accumulation to render light meshes.
//bool system_draw_light_meshes(ECS* ecs) {
//
//	return true;
//}

bool system_post_draw(ECS* ecs) {
	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

	ecs->resources.graphicsCore.swapChain->Present(1, 0);

	return true;
}

bool system_debug_entities(ECS* ecs) {
	ImGui::ShowDemoWindow();

	if (ImGui::Begin("Entities")) {
		for (int i = 0; i < ecs->nextEntity; i++) {
			auto& entity = ecs->entities[i];
			if (i > 0) ImGui::Separator();
			if (entity.position.has_value()) {
				auto& position = entity.position.value();
				ImGui::Text("position");
				ImGui::Text("\t{ %f, %f, %f }", position.x, position.y, position.z);

			}
			if (entity.light.has_value()) {
				auto& light = entity.light.value();
				ImGui::Text("light");
				ImGui::Text("\tposition { %f, %f, %f }", light.position.x, light.position.y, light.position.z);
				ImGui::Text("\tradius %f ", light.radius);
				ImGui::Text("\tcolor{ %f, %f, %f }", light.color.x, light.color.y, light.color.z);
				ImGui::Text("\tintensity %f", light.intensity);
				ImGui::Text("\tambient { %f, %f, %f, %f }", light.ambient.x, light.ambient.y, light.ambient.z, light.ambient.w);
			}
			if (entity.movesInCircle.has_value()) {
				auto& moves = entity.movesInCircle.value();
				ImGui::Text("moves in circle");
				ImGui::Text("\torigin { %f, %f, %f }", moves.origin.x, moves.origin.y, moves.origin.z);
				ImGui::Text("\tradius %f", moves.radius);
			}
			if (entity.renderModel.has_value()) {
				auto& model = entity.renderModel.value();
				ImGui::Text("render model");
				ImGui::Text("\tmodel id %i", model.meshId);
				ImGui::Text("\tmaterial id %i", model.materialId);
			}
		}
	}

	return true;
}

int main(int argc, char** argv) {
	try {
		ECS ecs;
		ecs.systems = {
			// Initialization
			// TODO WT: Way of bundling systems together to be added together (like plugins)
			// TODO WT: Way of ordering systems/inserting systems in the right place.
			system_setup_window,
			system_setup_graphics_core,
			system_setup_deferred_graphics_pipelines,
			system_setup_lighting_graphics_pipelines,
			system_setup_imgui,
			system_init_application, // TODO WT: Separate this out even further.
			system_start,

			// Updates
			system_new_imgui_frame,
			system_debug_entities,
			system_move_in_circle,
			system_flycam,

			// Draws
			system_draw_imgui_debug_ui,
			system_draw_models,
			system_draw_lights,
			system_post_draw
		};
		ecs.run();

		while (!glfwWindowShouldClose(ecs.resources.window.window)) {
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