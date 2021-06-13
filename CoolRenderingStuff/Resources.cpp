#define NOMINMAX

#include "Resources.h"

#include "vendor\imgui\imgui.h"
#include "vendor\imgui\imgui_impl_glfw.h"
#include "vendor\imgui\imgui_impl_dx11.h"

#include "ECS.h"
#include <GLFW\glfw3.h>

#include "Application.h"

#define PI 3.141593

void ResourceImguiLifetime::Cleanup() {
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
}

void ResourceGraphicsCore::Cleanup() {
	depthTexture->Release();
	depthStencilView->Release();

	swapChain->Release();
	context->Release();
	device->Release();
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
		float extent = float(PI - 0.1);

		ecs->flyCam.yaw += static_cast<float>(x - lastX) / width;
		ecs->flyCam.pitch += static_cast<float>(y - lastY) / height;

		ecs->flyCam.yaw = std::fmodf(ecs->flyCam.yaw, float(PI * 2.0));
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

void ResourceWindow::Cleanup() {
	glfwDestroyWindow(window);
	glfwTerminate();
}
