#include "core.h"
#include "utils/YamlExtended.h"


namespace YamlExtended
{
	void ultimateTest()
	{
		YAML::Node node;  // starts out as null
		node["File"] = "TextureFormat";
		writeVec2("uv", glm::vec2(1.2f, 2.4f), node);
		writeFile("textureFormat.yaml", node);

		YAML::Node node2 = readFile("textureFormat.yaml");
		glm::vec2 vector = readVec2("uv", node2);
		g_logger_info("Vec2: %2.3f %2.3f", vector.x, vector.y);
	}

	void writeFile(const char* filename, YAML::Node& node)
	{
		std::ofstream outStream(filename);
		outStream << node;
		outStream.close();
	}

	YAML::Node readFile (const char* filename)
	{
		return YAML::LoadFile(filename);
	}

	void writeVec2(const char* name, const glm::vec2& vec2, YAML::Node&& node)
	{
		node[name]["x"] = vec2.x;
		node[name]["y"] = vec2.y;
	}

	void writeVec2(const char* name, const glm::vec2& vec2, YAML::Node& node) 
	{
		node[name]["x"] = vec2.x;
		node[name]["y"] = vec2.y;
	}

	glm::vec2 readVec2(const char* name, const YAML::Node& node)
	{
		return {
			node[name]["x"].as<float>(),
			node[name]["y"].as<float>()
		};
	}
}
