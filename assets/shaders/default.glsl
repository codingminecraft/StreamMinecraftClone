#type vertex
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoords;
layout (location = 2) in uint aFace;

out vec2 fTexCoords;
flat out uint fFace;
out vec3 fFragPosition;

uniform mat4 uProjection;
uniform mat4 uView;

void main()
{
	fTexCoords = aTexCoords;
	fFace = aFace;
	gl_Position = uProjection * uView * vec4(aPos.x, aPos.y, aPos.z, 1.0);
	fFragPosition = aPos;
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