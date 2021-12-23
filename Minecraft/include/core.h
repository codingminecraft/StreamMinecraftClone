#ifndef MINECRAFT_CORE_H
#define MINECRAFT_CORE_H

// Glm
#define GLM_EXT_INCLUDED
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/integer.hpp>
#include <glm/gtx/string_cast.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/quaternion.hpp>

// My libraries
#include <cppUtils/cppUtils.hpp>

// Standard
#include <filesystem>
#include <cstring>
#include <iostream>
#include <fstream>
#include <array>
#include <cstdio>
#include <vector>
#include <string>
#include <string_view>
#include <thread>
#include <mutex>
#include <random>
#include <future>
#include <queue>
#include <algorithm>
#include <bitset>
#include <optional>
//#include <unordered_set>
//#include <unordered_map>

// Use this for hash map and hash sets instead of the crappy std lib
#include <robin_hood.h>

// GLFW/glad
#include <glad/glad.h>
#include <GLFW/glfw3.h>

// stb
#include <stb/stb_image.h>
#include <stb/stb_write.h>
#include <stb/stb_image_resize.h>

// Yaml
#include <yaml-cpp/yaml.h>

// Fast Noise 
#include <FastNoiseLite.h>

// Freetype
#include <freetype/freetype.h>

// Enum reflection
#include <magic_enum.hpp>

// Optick
#ifdef _USE_OPTICK
#define OPTICK_ENABLE_GPU_D3D12 0
#define OPTICK_ENABLE_GPU_VULKAN 0
#include <optick.h>
#endif

// User defined literals
glm::vec4 operator""_hex(const char* hexColor, size_t length);

struct RawMemory
{
	uint8* data;
	size_t size;
	size_t offset;

	void init(size_t initialSize);
	void free();
	void shrinkToFit();
	void resetReadWriteCursor();

	void writeDangerous(const uint8* data, size_t dataSize);
	void readDangerous(uint8* data, size_t dataSize);

	template<typename T>
	void write(const T* data)
	{
		writeDangerous((uint8*)data, sizeof(T));
	}

	template<typename T>
	void read(T* data)
	{
		readDangerous((uint8*)data, sizeof(T));
	}

	void setCursor(size_t offset);
};

#endif
