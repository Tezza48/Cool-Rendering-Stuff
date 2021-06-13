#define NOMINMAX

#include "Application.h"

#define STB_IMAGE_IMPLEMENTATION
#include "vendor\stb\stb_image.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/ProgressHandler.hpp>


class AssimpProgressHandler : public Assimp::ProgressHandler {
	virtual bool Update(float percentage) {
		std::cout << "\rAssimp: " << std::fixed << std::setprecision(1) << percentage * 100.0f << std::defaultfloat << "%\tloaded.";
		return true;
	}
};

std::vector<char> Application::readFile(const std::string& filename) {
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

void Application::Init(ECS* ecs) {
	createDeferredGraphicsPipeline(ecs);
	createLightingGraphicsPipeline(ecs);
	createConstantBuffers(ecs);
	createGbuffers(ecs);

	loadModel(ecs);

	lighting = new Lighting(ecs->graphicsCore.device);
}

void Application::Cleanup() {
	delete lighting;

	delete lightingGraphicsPipeline;
	delete deferredGraphicsPipeline;
}

void Application::createDeferredGraphicsPipeline(ECS* ecs) {
	std::vector<char> vertexShaderCode = readFile("shaders/deferredVertex.cso");
	std::vector<char> pixelShaderCode = readFile("shaders/deferredPixel.cso");

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
	glfwGetWindowSize(ecs->window.window, &width, &height);

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

	deferredGraphicsPipeline = new GraphicsPipeline(
		ecs->graphicsCore.device,
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
}

void Application::createLightingGraphicsPipeline(ECS* ecs) {
	std::vector<char> vertexShaderCode = readFile("shaders/lightAccVertex.cso");
	std::vector<char> pixelShaderCode = readFile("shaders/lightAccPixel.cso");

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
	glfwGetWindowSize(ecs->window.window, &width, &height);

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

	lightingGraphicsPipeline = new GraphicsPipeline(
		ecs->graphicsCore.device,
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

	if (FAILED(ecs->graphicsCore.device->CreateSamplerState(&samplerDesc, &gbufferSampler))) {
		throw std::runtime_error("Failed to create gbuffer sampler");
	}
}

void Application::createConstantBuffers(ECS* ecs) {
	D3D11_BUFFER_DESC desc{};
	desc.ByteWidth = sizeof(PerFrameUniforms);
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	desc.MiscFlags = 0;
	desc.StructureByteStride = 0;

	if (FAILED(ecs->graphicsCore.device->CreateBuffer(&desc, nullptr, &perFrameUniformsBuffer))) {
		throw std::runtime_error("Failed to create cbuffer!");
	}

	D3D11_BUFFER_DESC matDesc{};
	matDesc.ByteWidth = sizeof(MaterialCbuffer);
	matDesc.Usage = D3D11_USAGE_DYNAMIC;
	matDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	matDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	matDesc.MiscFlags = 0;
	matDesc.StructureByteStride = 0;

	if (FAILED(ecs->graphicsCore.device->CreateBuffer(&matDesc, nullptr, &perMaterialUniformsBuffer))) {
		throw std::runtime_error("Failed to create material settings cbuffer!");
	}
}

void Application::createGbuffers(ECS* ecs) {
	int32_t width, height;
	glfwGetWindowSize(ecs->window.window, &width, &height);

	for (size_t i = 0; i < GeometryBuffer::MAX_BUFFER; i++) {
		D3D11_TEXTURE2D_DESC textureDesc{};
		textureDesc.Width = width;
		textureDesc.Height = height;
		textureDesc.MipLevels = 1;
		textureDesc.ArraySize = 1;
		textureDesc.Format = geometryBuffer.formats[i];
		textureDesc.SampleDesc.Count = 1;
		textureDesc.SampleDesc.Quality = 0;

		textureDesc.Usage = D3D11_USAGE_DEFAULT;
		textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		textureDesc.CPUAccessFlags = 0;
		textureDesc.MiscFlags = 0;

		if (FAILED(ecs->graphicsCore.device->CreateTexture2D(&textureDesc, nullptr, &geometryBuffer.textures[i]))) {
			throw std::runtime_error("Failed to create gbuffer texture!");
		}

		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
		rtvDesc.Format = geometryBuffer.formats[i];
		rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		rtvDesc.Texture2D.MipSlice = 0;

		if (FAILED(ecs->graphicsCore.device->CreateRenderTargetView(geometryBuffer.textures[i], &rtvDesc, &geometryBuffer.textureViews[i]))) {
			throw std::runtime_error("Failed to create gbuffer RTV!");
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = geometryBuffer.formats[i];
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		srvDesc.Texture2D.MostDetailedMip = 0;

		if (FAILED(ecs->graphicsCore.device->CreateShaderResourceView(geometryBuffer.textures[i], &srvDesc, &geometryBuffer.textureResourceViews[i]))) {
			throw std::runtime_error("Failed to create gbuffer SRV!");
		}
	}
}

void Application::loadModel(ECS* ecs) {
	Assimp::Importer* importer = new Assimp::Importer();

	AssimpProgressHandler* handler = new AssimpProgressHandler();
	importer->SetProgressHandler(handler); // Taken ownership of handler

	stbi_set_flip_vertically_on_load(true);

	std::string basePath = "assets/crytekSponza_fbx/";
	std::string fullPath = basePath + "sponza.fbx";

	const aiScene* scene = importer->ReadFile(fullPath, aiProcess_CalcTangentSpace | aiProcess_Triangulate | aiProcess_JoinIdenticalVertices);

	//std::cout << "\n Loaded\n";

	ecs->modelCache[fullPath] = std::vector<Mesh>();//  = std::vector();
	ecs->materialCache[fullPath] = std::vector<Material>();

	ecs->modelCache[fullPath].reserve(scene->mNumMaterials);

	for (size_t i = 0; i < scene->mNumMaterials; i++) {
		Material mat;
		aiMaterial* data = scene->mMaterials[i];

		std::vector<aiMaterialProperty*> properties(data->mProperties, data->mProperties + data->mNumProperties);

		aiString name;
		if (data->Get(AI_MATKEY_NAME, name) == AI_SUCCESS) {
			//std::cout << "Loading material " << name.C_Str() << std::endl;
			mat.name = name.C_Str();
		}

		loadTexture(ecs, aiTextureType_DIFFUSE, data, basePath, mat.diffuseTexture, (bool&)mat.settings.useDiffuseTexture);
		loadTexture(ecs, aiTextureType_NORMALS, data, basePath, mat.normalTexture, (bool&)mat.settings.useNormalTexture);
		loadTexture(ecs, aiTextureType_OPACITY, data, basePath, mat.alphaCutoutTexture, (bool&)mat.settings.useAlphaCutoutTexture);
		loadTexture(ecs, aiTextureType_SPECULAR, data, basePath, mat.specularTexture, (bool&)mat.settings.useSpecularTexture);
		if (!mat.settings.useSpecularTexture)
			loadTexture(ecs, aiTextureType_SHININESS, data, basePath, mat.specularTexture, (bool&)mat.settings.useSpecularTexture);
		if (!mat.settings.useSpecularTexture)
			loadTexture(ecs, aiTextureType_DIFFUSE_ROUGHNESS, data, basePath, mat.specularTexture, (bool&)mat.settings.useSpecularTexture);

		ecs->materialCache[fullPath].push_back(mat);
	}

	ecs->modelCache[fullPath].reserve(scene->mNumMeshes);

	for (size_t i = 0; i < scene->mNumMeshes; i++) {
		Mesh mesh;
		aiMesh* data = scene->mMeshes[i];

		mesh.materialId = data->mMaterialIndex;

		std::vector<Vertex> vertices(data->mNumVertices);
		processVertices(data, vertices);

		auto numIndices = data->mNumFaces * 3u;
		std::vector<uint32_t> indices(numIndices);
		processIndices(data, indices);

		D3D11_BUFFER_DESC vDesc = {};
		vDesc.Usage = D3D11_USAGE_DEFAULT;
		vDesc.ByteWidth = sizeof(Vertex) * data->mNumVertices;
		vDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vDesc.CPUAccessFlags = 0;
		vDesc.MiscFlags = 0;
		vDesc.StructureByteStride = 0;

		D3D11_SUBRESOURCE_DATA vertData{};
		vertData.pSysMem = vertices.data();

		mesh.numVertices = data->mNumVertices;
		auto vbHR = ecs->graphicsCore.device->CreateBuffer(&vDesc, &vertData, &mesh.vertices);

		std::string vbufferName(data->mName.C_Str());
		vbufferName += "_VertexBuffer";
		mesh.vertices->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)vbufferName.size(), vbufferName.c_str());

		assert(SUCCEEDED(vbHR));

		D3D11_BUFFER_DESC iDesc = {};
		iDesc.Usage = D3D11_USAGE_DEFAULT;
		iDesc.ByteWidth = sizeof(unsigned int) * numIndices;
		iDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		iDesc.CPUAccessFlags = 0;
		iDesc.MiscFlags = 0;
		iDesc.StructureByteStride = 0;

		D3D11_SUBRESOURCE_DATA indexData{};
		indexData.pSysMem = indices.data();

		mesh.indexCount = numIndices;
		auto ibHF = ecs->graphicsCore.device->CreateBuffer(&iDesc, &indexData, &mesh.indices);

		std::string ibufferName(data->mName.C_Str());
		ibufferName += "_IndexBuffer";
		mesh.indices->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)ibufferName.size(), ibufferName.c_str());

		assert(SUCCEEDED(vbHR));

		mesh.materialId = data->mMaterialIndex;

		ecs->modelCache[fullPath].push_back(mesh);
	}

	importer->FreeScene();

	delete importer;
}

void Application::loadTexture(ECS* ecs, aiTextureType type, aiMaterial* materialData, const std::string& baseAssetPath, std::string& outPath, bool& outEnabled) {
	outEnabled = false;
	outPath.clear();

	aiString path;
	if (materialData->Get(AI_MATKEY_TEXTURE(type, 0), path) == AI_SUCCESS) {
		outPath += baseAssetPath;
		outPath += path.C_Str();

		//std::cout << outPath << std::endl;

		std::filesystem::path asPath(outPath);
		auto extension = asPath.extension();
		if (extension == ".dds") {
			std::cout << "Cant support DDS just yet." << outPath << std::endl;
			outPath.clear();
			outEnabled = false;
			return;
		}
		else {
			int width, height, bpp;
			byte* textureData = stbi_load(outPath.c_str(), &width, &height, &bpp, STBI_rgb_alpha);
			if (!textureData) {
				std::stringstream errorString("Failed to load texture ");
				errorString << outPath << " because " << stbi_failure_reason();
				throw std::runtime_error(errorString.str());
			}

			ecs->textureCache[outPath] = new Texture(ecs->graphicsCore.device, ecs->graphicsCore.context, outPath, width, height, bpp, textureData);
			stbi_image_free(textureData);
		}

		outEnabled = true;
	}
}

void Application::processVertices(const aiMesh* meshData, std::vector<Vertex>& vertices) {
	for (size_t v = 0; v < meshData->mNumVertices; v++) {
		auto pos = meshData->mVertices[v];
		auto normal = meshData->mNormals[v];
		auto tangent = meshData->mTangents[v];
		auto bitangent = meshData->mBitangents[v];
		aiVector3D uv;
		if (meshData->mTextureCoords) {
			uv = meshData->mTextureCoords[0][v];
		}

		vertices[v] = {
			{ pos.x / 100.0f, pos.y / 100.0f, pos.z / 100.0f },
			{ normal.x, normal.y, normal.z },
			{ tangent.x, tangent.y, tangent.z },
			{ bitangent.x, bitangent.y, bitangent.z },
			{ uv.x, uv.y }
		};
	}
}

void Application::processIndices(const aiMesh* meshData, std::vector<uint32_t>& indices) {
	for (size_t face = 0, index = 0; face < meshData->mNumFaces; face++)
	{
		indices[index++] = meshData->mFaces[face].mIndices[0];
		indices[index++] = meshData->mFaces[face].mIndices[1];
		indices[index++] = meshData->mFaces[face].mIndices[2];
	}
}

// TODO WT: make events handleable in ecs

void Application::RecompileShaders(ECS* ecs) {
	// TODO WT: Clean up this memory leak heaven!
	ID3D11VertexShader* newVertShader;
	ID3D11PixelShader* newPixelShader;
	ID3D10Blob* bytecode;
	ID3D10Blob* errors;

	// Graphics
	if (FAILED(D3DCompileFromFile(L"shaders/deferredVertex.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "vs_5_0", 0, 0, &bytecode, &errors))) {
		std::wcout << L"deferred vshader error " << (char*)errors->GetBufferPointer() << std::endl;
		if (errors)
			errors->Release();
		return;
	}

	ecs->graphicsCore.device->CreateVertexShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &newVertShader);
	if (errors)
		errors->Release();
	bytecode->Release();

	if (FAILED(D3DCompileFromFile(L"shaders/deferredPixel.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "ps_5_0", 0, 0, &bytecode, &errors))) {
		std::wcout << L"deferred pshader error" << (char*)errors->GetBufferPointer() << std::endl;
		if (errors)
			errors->Release();
		return;
	}

	ecs->graphicsCore.device->CreatePixelShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &newPixelShader);
	if (errors)
		errors->Release();
	bytecode->Release();

	deferredGraphicsPipeline->vertexShader->Release();
	deferredGraphicsPipeline->pixelShader->Release();
	deferredGraphicsPipeline->vertexShader = newVertShader;
	deferredGraphicsPipeline->pixelShader = newPixelShader;

	// Lighting
	if (FAILED(D3DCompileFromFile(L"shaders/lightAccVertex.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "vs_5_0", 0, 0, &bytecode, &errors))) {
		std::wcout << L"lightAcc vshader error " << (char*)errors->GetBufferPointer() << std::endl;
		if (errors)
			errors->Release();
		return;
	}

	ecs->graphicsCore.device->CreateVertexShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &newVertShader);
	if (errors)
		errors->Release();
	bytecode->Release();

	if (FAILED(D3DCompileFromFile(L"shaders/lightAccPixel.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "ps_5_0", 0, 0, &bytecode, &errors))) {
		std::wcout << L"lightAcc pshader error: " << (char*)errors->GetBufferPointer() << std::endl;
		if (errors)
			errors->Release();
		return;
	}

	ecs->graphicsCore.device->CreatePixelShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &newPixelShader);
	if (errors)
		errors->Release();
	bytecode->Release();

	lightingGraphicsPipeline->vertexShader->Release();
	lightingGraphicsPipeline->pixelShader->Release();
	lightingGraphicsPipeline->vertexShader = newVertShader;
	lightingGraphicsPipeline->pixelShader = newPixelShader;

	std::cout << "Successfully hot reloader lighting pass shaders" << std::endl;
}

void Application::OnWindowResized(ECS* ecs, uint32_t width, uint32_t height) {
	ecs->graphicsCore.swapChain->ResizeBuffers(ecs->graphicsCore.numSwapChainBuffers, width, height, ecs->graphicsCore.swapChainFormat, 0);

	D3D11_TEXTURE2D_DESC dstDesc;
	D3D11_DEPTH_STENCIL_VIEW_DESC dstViewDesc;
	ecs->graphicsCore.depthTexture->GetDesc(&dstDesc);
	ecs->graphicsCore.depthStencilView->GetDesc(&dstViewDesc);

	ecs->graphicsCore.depthTexture->Release();
	ecs->graphicsCore.depthStencilView->Release();

	dstDesc.Width = width;
	dstDesc.Height = height;

	HRESULT hr;
	hr = ecs->graphicsCore.device->CreateTexture2D(&dstDesc, nullptr, &ecs->graphicsCore.depthTexture);
	if (FAILED(hr) || !ecs->graphicsCore.depthTexture) {
		throw std::runtime_error("Failed to create resized Depth Stencil texture!");
	}

	hr = ecs->graphicsCore.device->CreateDepthStencilView(ecs->graphicsCore.depthTexture, &dstViewDesc, &ecs->graphicsCore.depthStencilView);
	if (FAILED(hr) || !ecs->graphicsCore.depthStencilView) {
		throw std::runtime_error("Failed to create resized Depth Stencil View!");
	}

	deferredGraphicsPipeline->viewport.Width = static_cast<float>(width);
	deferredGraphicsPipeline->viewport.Height = static_cast<float>(height);

	deferredGraphicsPipeline->scissor.right = width;
	deferredGraphicsPipeline->scissor.bottom = height;

	for (size_t i = 0; i < GeometryBuffer::MAX_BUFFER; i++)
	{
		D3D11_TEXTURE2D_DESC texDesc;
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc;
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		geometryBuffer.textures[i]->GetDesc(&texDesc);
		geometryBuffer.textureViews[i]->GetDesc(&rtvDesc);
		geometryBuffer.textureResourceViews[i]->GetDesc(&srvDesc);

		geometryBuffer.textures[i]->Release();
		geometryBuffer.textureViews[i]->Release();
		geometryBuffer.textureResourceViews[i]->Release();

		texDesc.Width = width;
		texDesc.Height = height;

		hr = ecs->graphicsCore.device->CreateTexture2D(&texDesc, nullptr, &geometryBuffer.textures[i]);
		if (FAILED(hr) || !geometryBuffer.textures[i]) {
			throw std::runtime_error("Failed to create resized GBuffer texture!");
		}

		hr = ecs->graphicsCore.device->CreateRenderTargetView(geometryBuffer.textures[i], &rtvDesc, &geometryBuffer.textureViews[i]);
		if (FAILED(hr) || !geometryBuffer.textureViews[i]) {
			throw std::runtime_error("Failed to create resized GBuffer texture RTV!");
		}

		hr = ecs->graphicsCore.device->CreateShaderResourceView(geometryBuffer.textures[i], &srvDesc, &geometryBuffer.textureResourceViews[i]);
		if (FAILED(hr) || !geometryBuffer.textureResourceViews[i]) {
			throw std::runtime_error("Failed to create resized GBuffer texture! SRV");
		}

	}

	lightingGraphicsPipeline->viewport.Width = static_cast<float>(width);
	lightingGraphicsPipeline->viewport.Height = static_cast<float>(height);

	lightingGraphicsPipeline->scissor.right = width;
	lightingGraphicsPipeline->scissor.bottom = height;
}
