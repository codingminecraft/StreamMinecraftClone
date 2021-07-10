#type vertex
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoords;

out vec2 fTexCoords;

uniform mat4 uProjection;
uniform mat4 uView;

void main()
{
	fTexCoords = aTexCoords;
	gl_Position = uProjection * uView * vec4(aPos.x, aPos.y, aPos.z, 1.0);
}

#type fragment
#version 330 core
out vec4 FragColor;

in vec2 fTexCoords;

uniform sampler2D uTexture;

void main()
{
	FragColor = texture(uTexture, fTexCoords);;
}