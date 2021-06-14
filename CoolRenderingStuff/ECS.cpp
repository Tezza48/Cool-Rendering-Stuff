#define NOMINMAX

#include "ECS.h"

#include "Application.h"

int ECS::addEntity() {
	entities.emplace_back();

	return nextEntity++;
}

void ECS::run() {
	std::vector<System*> nextSystems;
	for (const auto system : systems) {
		if (system(this)) {
			nextSystems.push_back(system);
		}
	}

	systems = nextSystems;
}

Resources::~Resources()
{
	delete lighting;

	delete deferredGraphicsPipeline;
	delete lightingGraphicsPipeline;

	for (auto texture : textureCache) {
		delete texture.second;
	}

	for (auto mesh : modelCache) {
		mesh.vertices->Release();
		mesh.indices->Release();
	}

	// Delete pipelines
	// TODO WT: Want this to not be a pointer, requires moving stuff to separate files to define the dependency tree.
	delete application;

	imguiLifetime.Cleanup();
	graphicsCore.Cleanup();
	window.Cleanup();
}
