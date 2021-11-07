#type vertex
#version 430 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in uint aTexId;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in vec3 aNormal;

flat out uint fTexId;
out vec2 fTexCoord;
out vec3 fNormal;

uniform mat4 uProjection;
uniform mat4 uView;

void main()
{
	fNormal = aNormal;
  	fTexCoord = aTexCoord;
  	fTexId = aTexId;
	gl_Position = uProjection * uView * vec4(aPos, 1.0);
}

#type fragment
#version 430 core
#define numTextures 16
layout (location = 0) out vec4 FragColor;

flat in uint fTexId;
in vec2 fTexCoord;
in vec3 fNormal;

vec3 uSunDirection = normalize(vec3(-0.27, 0.57, -0.57));
uniform sampler2D uTextures[numTextures];

vec3 lightColor = vec3(1.0, 1.0, 1.0);

void main()
{
	// Calculate ambient light
    float ambientStrength = 0.4;
    vec3 ambient = ambientStrength * lightColor;

	// Turn that into diffuse lighting
	vec3 lightDir = normalize(uSunDirection);

	float diff = max(dot(fNormal, lightDir), 0.0);
	vec3 diffuse = diff * lightColor;

	vec4 objectColor = texture(uTextures[int(fTexId)], fTexCoord);
	FragColor = vec4((diffuse + ambient), 1.0) * objectColor;
}