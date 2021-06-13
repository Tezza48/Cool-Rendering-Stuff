#pragma once

#include <vector>
#include <unordered_map>
#include "Resources.h"
#include <DirectXMath.h>
#include "Texture.h"
#include "Lighting.h"
#include <optional>

struct Application;

struct Mesh {
	uint32_t materialId;

	size_t numVertices;
	ID3D11Buffer* vertices;

	uint32_t indexCount;
	ID3D11Buffer* indices;
};

struct ComponentMovesInCircle {
	DirectX::XMFLOAT3 origin;
	float radius = 1.0f;
};

struct ComponentRenderModel {
	std::string path;
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

struct ECS {
	using System = bool(ECS* ecs);
	std::vector<System*> systems;

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