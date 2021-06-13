#pragma once

#include <DirectXMath.h>
#include <dxgi.h>
#include <d3d11.h>

#include <iostream>

#include <GLFW\glfw3.h>

struct ResourceImguiLifetime {
	void Cleanup();
};

struct ResourceFlycam {
	float yaw, pitch;
	DirectX::XMFLOAT3 position;
};

struct ResourceGraphicsCore {
	ID3D11Device* device;
	ID3D11DeviceContext* context;
	uint32_t numSwapChainBuffers;
	IDXGISwapChain* swapChain;
	DXGI_FORMAT swapChainFormat = DXGI_FORMAT_UNKNOWN;

	ID3D11Texture2D* depthTexture;
	ID3D11DepthStencilView* depthStencilView;

	void Cleanup();
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

	void Cleanup();
};