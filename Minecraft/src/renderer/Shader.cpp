#include "renderer/Shader.h"
#include "utils/CMath.h"


namespace Minecraft
{
	// Internal Structures
	struct ShaderVariable
	{
		std::string name;
		GLint varLocation;
		uint32 shaderProgramId;

		bool operator==(const ShaderVariable& other) const
		{
			return other.shaderProgramId == shaderProgramId && other.name == name;
		}
	};

	struct hashShaderVar
	{
		std::size_t operator()(const ShaderVariable& key) const
		{
			return (std::hash<std::string>()(key.name) ^ std::hash<uint32>()(key.shaderProgramId));
		}
	};

	// Internal Variables
	static auto mAllShaderVariableLocations = robin_hood::unordered_map<ShaderVariable, GLint, hashShaderVar>();

	// Forward Declarations
	static GLint GetVariableLocation(const Shader& shader, const char* varName);
	static GLenum ShaderTypeFromString(const std::string& type);
	static std::string ReadFile(const char* filepath);

	Shader::Shader()
	{
		filepath = nullptr;
		programId = UINT32_MAX;
		startIndex = UINT32_MAX;
	}

	void Shader::compile(const char* shaderFilepath)
	{
		int strLength = (int)std::strlen(shaderFilepath);
		filepath = (char*)g_memory_allocate(sizeof(char) * (strLength + 1));
		std::strcpy(filepath, shaderFilepath);
		filepath[strLength] = '\0';
		g_logger_info("Compiling shader: %s", filepath);
		std::string fileSource = ReadFile(filepath);

		robin_hood::unordered_map<GLenum, std::string> shaderSources;

		const char* typeToken = "#type";
		size_t typeTokenLength = strlen(typeToken);
		size_t pos = fileSource.find(typeToken, 0);
		while (pos != std::string::npos)
		{
			size_t eol = fileSource.find_first_of("\r\n", pos);
			g_logger_assert(eol != std::string::npos, "Syntax error");
			size_t begin = pos + typeTokenLength + 1;
			std::string type = fileSource.substr(begin, eol - begin);
			g_logger_assert(ShaderTypeFromString(type), "Invalid shader type specified.");

			size_t nextLinePos = fileSource.find_first_not_of("\r\n", eol);
			pos = fileSource.find(typeToken, nextLinePos);
			shaderSources[ShaderTypeFromString(type)] = fileSource.substr(nextLinePos, pos - (nextLinePos == std::string::npos ? fileSource.size() - 1 : nextLinePos));
		}

		GLuint program = glCreateProgram();
		g_logger_assert(shaderSources.size() <= 2, "Shader source must be less than 2.");
		std::array<GLenum, 2> glShaderIDs;
		int glShaderIDIndex = 0;

		for (auto& kv : shaderSources)
		{
			GLenum shaderType = kv.first;
			const std::string& source = kv.second;

			// Create an empty vertex shader handle
			GLuint shader = glCreateShader(shaderType);

			// Send the vertex shader source code to GL
			// Note that std::string's .c_str is NULL character terminated.
			const GLchar* sourceCStr = source.c_str();
			glShaderSource(shader, 1, &sourceCStr, 0);

			// Compile the vertex shader
			glCompileShader(shader);

			GLint isCompiled = 0;
			glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
			if (isCompiled == GL_FALSE)
			{
				GLint maxLength = 0;
				glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);

				// The maxLength includes the NULL character
				std::vector<GLchar> infoLog(maxLength);
				glGetShaderInfoLog(shader, maxLength, &maxLength, &infoLog[0]);

				// We don't need the shader anymore.
				glDeleteShader(shader);

				g_logger_error("%s", infoLog.data());
				g_logger_assert(false, "Shader compilation failed!");

				programId = UINT32_MAX;
				return;
			}

			glAttachShader(program, shader);
			glShaderIDs[glShaderIDIndex++] = shader;
		}

		// Link our program
		glLinkProgram(program);

		// Note the different functions here: glGetProgram* instead of glGetShader*.
		GLint isLinked = 0;
		glGetProgramiv(program, GL_LINK_STATUS, (int*)&isLinked);
		if (isLinked == GL_FALSE)
		{
			GLint maxLength = 0;
			glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);

			// The maxLength includes the NULL character
			std::vector<GLchar> infoLog(maxLength);
			glGetProgramInfoLog(program, maxLength, &maxLength, &infoLog[0]);

			// We don't need the program anymore.
			glDeleteProgram(program);
			// Don't leak shaders either.
			for (auto id : glShaderIDs)
				glDeleteShader(id);

			g_logger_error("%s", infoLog.data());
			g_logger_assert(false, "Shader linking failed!");
			programId = UINT32_MAX;
			return;
		}

		startIndex = (uint32)mAllShaderVariableLocations.size();

		// Get all the active vertex attributes and store them in our map of uniform variable locations
		int numUniforms;
		glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &numUniforms);

		int maxCharLength;
		glGetProgramiv(program, GL_ACTIVE_UNIFORM_MAX_LENGTH, &maxCharLength);
		if (numUniforms > 0 && maxCharLength > 0)
		{
			char* charBuffer = (char*)g_memory_allocate(sizeof(char) * maxCharLength);

			for (int i = 0; i < numUniforms; i++)
			{
				int length, size;
				GLenum type;
				glGetActiveUniform(program, i, maxCharLength, &length, &size, &type, charBuffer);
				GLint varLocation = glGetUniformLocation(program, charBuffer);
				mAllShaderVariableLocations[{
					std::string(charBuffer, length),
						varLocation,
						program
				}] = varLocation;
			}

			g_memory_free(charBuffer);
		}

		// Always detach shaders after a successful link.
		for (auto id : glShaderIDs)
			glDetachShader(program, id);

		programId = program;

		g_logger_info("Shader compilation succeeded %s", filepath);
	}

	void Shader::destroy()
	{
		if (programId != UINT32_MAX)
		{
			glDeleteProgram(programId);
			programId = UINT32_MAX;
		}

		if (filepath != nullptr)
		{
			g_memory_free(filepath);
			filepath = nullptr;
		}
	}

	void Shader::bind() const
	{
		glUseProgram(programId);
	}

	void Shader::unbind() const
	{
		glUseProgram(0);
	}

	void Shader::uploadVec4(const char* varName, const glm::vec4& vec4) const
	{
		int varLocation = GetVariableLocation(*this, varName);
		glUniform4f(varLocation, vec4.x, vec4.y, vec4.z, vec4.w);
	}

	void Shader::uploadVec3(const char* varName, const glm::vec3& vec3) const
	{
		int varLocation = GetVariableLocation(*this, varName);
		glUniform3f(varLocation, vec3.x, vec3.y, vec3.z);
	}

	void Shader::uploadVec2(const char* varName, const glm::vec2& vec2) const
	{
		int varLocation = GetVariableLocation(*this, varName);
		glUniform2f(varLocation, vec2.x, vec2.y);
	}

	void Shader::uploadIVec4(const char* varName, const glm::ivec4& vec4) const
	{
		int varLocation = GetVariableLocation(*this, varName);
		glUniform4i(varLocation, vec4.x, vec4.y, vec4.z, vec4.w);
	}

	void Shader::uploadIVec3(const char* varName, const glm::ivec3& vec3) const
	{
		int varLocation = GetVariableLocation(*this, varName);
		glUniform3i(varLocation, vec3.x, vec3.y, vec3.z);
	}

	void Shader::uploadIVec2(const char* varName, const glm::ivec2& vec2) const
	{
		int varLocation = GetVariableLocation(*this, varName);
		glUniform2i(varLocation, vec2.x, vec2.y);
	}

	void Shader::uploadFloat(const char* varName, float value) const
	{
		int varLocation = GetVariableLocation(*this, varName);
		glUniform1f(varLocation, value);
	}

	void Shader::uploadInt(const char* varName, int value) const
	{
		int varLocation = GetVariableLocation(*this, varName);
		glUniform1i(varLocation, value);
	}

	void Shader::uploadUInt(const char* varName, uint32 value) const
	{
		int varLocation = GetVariableLocation(*this, varName);
		glUniform1ui(varLocation, value);
	}

	void Shader::uploadMat4(const char* varName, const glm::mat4& mat4) const
	{
		int varLocation = GetVariableLocation(*this, varName);
		glUniformMatrix4fv(varLocation, 1, GL_FALSE, glm::value_ptr(mat4));
	}

	void Shader::uploadMat3(const char* varName, const glm::mat3& mat3) const
	{
		int varLocation = GetVariableLocation(*this, varName);
		glUniformMatrix3fv(varLocation, 1, GL_FALSE, glm::value_ptr(mat3));
	}

	void Shader::uploadIntArray(const char* varName, int length, const int* array) const
	{
		int varLocation = GetVariableLocation(*this, varName);
		glUniform1iv(varLocation, length, array);
	}

	void Shader::uploadBool(const char* varName, bool value) const
	{
		int varLocation = GetVariableLocation(*this, varName);
		glUniform1i(varLocation, value ? 1 : 0);
	}

	bool Shader::isNull() const
	{
		return programId == UINT32_MAX;
	}

	void clearAllShaderVariables()
	{
		mAllShaderVariableLocations.clear();
	}

	// Private functions
	static GLint GetVariableLocation(const Shader& shader, const char* varName)
	{
		ShaderVariable match = {
			varName,
			0,
			shader.programId
		};
		auto iter = mAllShaderVariableLocations.find(match);
		if (iter != mAllShaderVariableLocations.end())
		{
			return iter->second;
		}

		//g_logger_warning("Could not find shader variable '%s' for shader '%s'. Hint, maybe the shader variable is not used in the program? If so, then it will be compiled out of existence.",
		//	varName, shader.filepath.string().c_str());
		return -1;
	}

	static GLenum ShaderTypeFromString(const std::string& type)
	{
		if (type == "vertex")
			return GL_VERTEX_SHADER;
		else if (type == "fragment" || type == "pixel")
			return GL_FRAGMENT_SHADER;

		g_logger_assert(false, "Unkown shader type.");
		return 0;
	}

	static std::string ReadFile(const char* filepath)
	{
		std::string result;
		std::ifstream in(filepath, std::ios::in | std::ios::binary);
		if (in)
		{
			in.seekg(0, std::ios::end);
			result.resize(in.tellg());
			in.seekg(0, std::ios::beg);
			in.read(&result[0], result.size());
			in.close();
		}
		else
		{
			g_logger_error("Could not open file: '%s'", filepath);
		}

		return result;
	}
}
