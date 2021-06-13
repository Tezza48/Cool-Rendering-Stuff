#define NOMINMAX

#include "ECS.h"

#include "Application.h"

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