#type vertex
#version 330 core
layout (location = 0) in uint aData;
layout (location = 1) in vec2 aTexCoords;

out vec2 fTexCoords;
flat out uint fFace;
out vec3 fFragPosition;

uniform sampler2D uTexCoordTexture;
uniform mat4 uProjection;
uniform mat4 uView;
uniform ivec2 uChunkPos;

#define POSITION_INDEX_BITMASK uint(0x1FFFF)
#define FACE_BITMASK uint(0xE0000000)
#define BASE_17_WIDTH uint(17)
#define BASE_17_DEPTH uint(17)
#define BASE_17_HEIGHT uint(289)

void extractPosition(in uint data, out vec3 position)
{
	uint positionIndex = data & POSITION_INDEX_BITMASK;
	uint z = positionIndex % BASE_17_WIDTH;
	uint x = (positionIndex % BASE_17_HEIGHT) / BASE_17_DEPTH;
	uint y = (positionIndex - (x * BASE_17_DEPTH) - z) / BASE_17_HEIGHT;
	position = vec3(float(x), float(y), float(z));
}

void extractFace(in uint data, out uint face)
{
	face = ((data & FACE_BITMASK) >> 29);
}

void main()
{
	vec3 position;
	extractPosition(aData, position);
	extractFace(aData, fFace);

	// Convert from local Chunk Coords to world Coords
	position.x += float(uChunkPos.x) * 16.0;
	position.z += float(uChunkPos.y) * 16.0;

	fTexCoords = aTexCoords;
	fFragPosition = position;
	gl_Position = uProjection * uView * vec4(position, 1.0);
}

#type fragment
#version 330 core
out vec4 FragColor;

in vec2 fTexCoords;
flat in uint fFace;
in vec3 fFragPosition;

uniform sampler2D uTexture;
uniform vec3 uSunPosition;

vec3 lightColor = vec3(1, 1, 1);

void faceToNormal(in uint face, out vec3 normal)
{
	switch(face)
	{
		case uint(0):
			normal = vec3(0, 0, -1);
			break;
		case uint(1):
			normal = vec3(0, 0, 1);
			break;
		case uint(2):
			normal = vec3(0, -1, 0);
			break;
		case uint(3):
			normal = vec3(0, 1, 0);
			break;
		case uint(4):
			normal = vec3(-1, 0, 0);
			break;
		case uint(5):
			normal = vec3(1, 0, 0);
			break;
	}
}

void main()
{
	// Calculate ambient light
    float ambientStrength = 0.4;
    vec3 ambient = ambientStrength * lightColor;

	// Turn that into diffuse lighting
	vec3 lightDir = normalize(uSunPosition - fFragPosition);

	vec3 normal;
	faceToNormal(fFace, normal);
	float diff = max(dot(normal, lightDir), 0.0);
	vec3 diffuse = diff * lightColor;

	vec3 objectColor = texture(uTexture, fTexCoords).rgb;
	vec3 result = (diffuse + ambient) * objectColor;
	FragColor = vec4(result, 1.0);
}