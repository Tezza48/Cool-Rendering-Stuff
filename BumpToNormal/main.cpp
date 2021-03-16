#define STB_IMAGE_IMPLEMENTATION
#include "../CoolRenderingStuff/vendor/stb/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_MSC_SECURE_CRT
#include "../CoolRenderingStuff/vendor/stb/stb_image_write.h"

#include <vector>
#include <iostream>
#include <sstream>
#include <istream>
#include <filesystem>

#include <Windows.h>
#include <DirectXMath.h>

using namespace DirectX;

struct pixelData3 {
	byte x, y, z;
};

int main(int argc, char** argv) {
	std::vector<char*> args(argv, argv + argc);

	std::string command;

	while (1) {
		std::cout << "File to convert (quit to quit): ";
		std::cin >> command;
		if (command == "quit" || command == "") {
			break;
		}

		std::filesystem::path inPath(command);
		auto inPathString = inPath.string();

		int width, height, bpp;
		unsigned char* fileData = stbi_load(inPathString.c_str(), &width, &height, &bpp, STBI_rgb);
		if (!fileData) {
			std::cout << stbi_failure_reason() << std::endl;
			continue;
		}

		std::cout << inPath.filename() << " " << width << ", " << height << " bpp: " << bpp << std::endl;

		//if (bpp != 3) {
		//	std::cout << "Only supporting RGB bump maps right now." << std::endl;
		//	stbi_image_free(fileData);
		//	continue;
		//}

		auto bumpPixels = reinterpret_cast<pixelData3*>(fileData);
		auto normalPixels = new pixelData3[width * height];

		for (size_t y = 0; y < height; y++)
		{
			for (size_t x = 0; x < width; x++)
			{
				size_t rightX = (x + 1) % width;
				size_t leftX = (width + (x - 1)) % width;
				size_t downY = (y + 1) % height;
				size_t upY = (height + (y - 1)) % height;

				size_t texelId = y * width + x;
				size_t rightTexelId = y * width + rightX;
				size_t downTexelId = downY * width + x;
				size_t leftTexelId = y * width + leftX;
				size_t upTexelId = upY * width + x;

				float currentHeight = static_cast<float>(bumpPixels[texelId].x) / 255.0f;
				float rightHeight = static_cast<float>(bumpPixels[rightTexelId].x) / 255.0f;
				float downHeight = static_cast<float>(bumpPixels[downTexelId].x) / 255.0f;
				float leftHeight = static_cast<float>(bumpPixels[leftTexelId].x) / 255.0f;
				float upHeight = static_cast<float>(bumpPixels[upTexelId].x) / 255.0f;

				float heightScale = 2.0f;

				auto current = XMVectorSet(static_cast<float>(x), static_cast<float>(y), currentHeight * heightScale, 1.0f);
				auto right = XMVectorSet(static_cast<float>(x + 1), static_cast<float>(y), rightHeight * heightScale, 1.0f);
				auto down = XMVectorSet(static_cast<float>(x), static_cast<float>(y - 1), downHeight * heightScale, 1.0f);
				auto left = XMVectorSet(static_cast<float>(x - 1), static_cast<float>(y), leftHeight * heightScale, 1.0f);
				auto up = XMVectorSet(static_cast<float>(x), static_cast<float>(y + 1), upHeight * heightScale, 1.0f);

				auto toRight = XMVector3Normalize(XMVectorSubtract(right, current));
				auto toDown = XMVector3Normalize(XMVectorSubtract(current, down));
				auto toLeft = XMVector3Normalize(XMVectorSubtract(left, current));
				auto toUp = XMVector3Normalize(XMVectorSubtract(current, up));

				auto crossPve = XMVector3Normalize(XMVector3Cross(toRight, toDown));
				auto crossNve = XMVector3Normalize(XMVector3Cross(toLeft, toUp));

				auto avg = XMVector3Normalize(XMVectorAdd(crossPve, crossNve));

				auto normalVec = XMVectorScale(avg, 0.5f);
				normalVec = XMVectorAdd(normalVec, XMVectorSet(0.5f, 0.5f, 0.5f, 0.0f));

				XMFLOAT3 normal; 
				XMStoreFloat3(&normal, normalVec);

				normalPixels[y * width + x] = { static_cast<byte>(normal.x * 255), static_cast<byte>(normal.y * 255), static_cast<byte>(normal.z * 255) };
			}
		}

		stbi_image_free(fileData);

		std::filesystem::path outPath(inPath);
		outPath.replace_extension("_normal.png");

		std::cout << "Saving normal map: " << outPath.string().c_str() << std::endl;

		bool success = stbi_write_png(outPath.string().c_str(), width, height, 3, reinterpret_cast<void*>(normalPixels), width * 3);
		if (!success) {
			std::cout << stbi_failure_reason() << std::endl;
		}

		delete[] normalPixels;

	}
}