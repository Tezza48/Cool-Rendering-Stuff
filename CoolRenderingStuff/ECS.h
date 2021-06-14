#pragma once

#include <vector>
#include <unordered_map>
#include "Resources.h"
#include <DirectXMath.h>
#include "Texture.h"
#include "Lighting.h"
#include <optional>
#include "GraphicsPipeline.h"
#include "Lighting.h"

struct Application;

struct Mesh {
	size_t defaultMaterialId = -1;

	size_t numVertices;
	ID3D11Buffer* vertices;

	uint32_t indexCount;
	ID3D11Buffer* indices;

	// This causes runtime error, maybe because of copys and stuff.
	// 	   for now clean up in ECS::Cleanup or manually.
	//~Mesh() {
	//	vertices->Release();
	//	vertices = nullptr;

	//	indices->Release();
	//	indices = nullptr;
	//}
};

struct ComponentMovesInCircle {
	DirectX::XMFLOAT3 origin;
	float radius = 1.0f;
};

struct ComponentRenderModel {
	size_t meshId;
	size_t materialId;
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

// TODO WT: This will end up wasting lots of space with many components and entities.
// Consider Groups for components which are always together. some kind of storage that allows for holes
// TODO WT: bitflags for which components are used instead of optional.
struct Entity {
	std::optional<DirectX::XMFLOAT3> position;
	std::optional<Light> light;
	std::optional<ComponentMovesInCircle> movesInCircle;
	std::optional<ComponentRenderModel> renderModel;
};

struct Resources {
	Application* application;

	ResourceWindow window;
	ResourceImguiLifetime imguiLifetime;
	ResourceGraphicsCore graphicsCore;
	std::vector<Mesh> modelCache;
	std::vector<Material> materialCache;

	std::unordered_map<std::string, Texture*> textureCache;
	ResourceFlycam flyCam;

	GraphicsPipeline* deferredGraphicsPipeline;
	GraphicsPipeline* lightingGraphicsPipeline;

	Lighting* lighting;

	~Resources();
};

struct ECS {
	using System = bool(ECS* ecs);
	std::vector<System*> systems;

	int nextEntity = 0;

	// Resources
	Resources resources;

	// Components
	std::vector<Entity> entities;

	int addEntity();

	void run();
};