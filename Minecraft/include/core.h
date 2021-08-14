#ifndef MINECRAFT_CORE_H
#define MINECRAFT_CORE_H

// Glm
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/hash.hpp"
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/type_ptr.hpp>

// My libraries
#include <logger/logger.h>
#include <memory/memory.h>

// Standard
#include <filesystem>
#include <cstring>
#include <iostream>
#include <fstream>
#include <array>
#include <cstdio>
#include <vector>
#include <unordered_map>
#include <string>
#include <unordered_set>
#include <thread>
#include <mutex>

// GLFW/glad
#include <glad/glad.h>
#include <GLFW/glfw3.h>

// stb
#include <stb/stb_image.h>
#include <stb/stb_write.h>

// Yaml
#include <yaml-cpp/yaml.h>

// Simplex 
#include <SimplexNoise.h>

#endif
