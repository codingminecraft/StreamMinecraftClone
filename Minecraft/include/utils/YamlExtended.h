#ifndef MINECRAFT_YAML_H
#define MINECRAFT_YAML_H

#include "core.h"

namespace YamlExtended
{
	void ultimateTest();

	void writeFile(const char* filename, YAML::Node& node);
	void writeVec2(const char* name, const glm::vec2& vec2, YAML::Node&& node);
	void writeVec2(const char* name, const glm::vec2& vec2, YAML::Node& node);

	YAML::Node readFile(const char* filename);
	glm::vec2 readVec2(const char* name, const YAML::Node& node);
}

#endif 
