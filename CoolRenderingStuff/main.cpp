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
	ecs->flyCam.position = { 0.0, 1.0, 0.0 };

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

bool system_pre_draw(ECS* ecs) {
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

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
		ecs.systems = {
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