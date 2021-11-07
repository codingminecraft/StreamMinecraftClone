#type vertex
#version 430 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoords;

out vec2 fTexCoords;

void main()
{
	fTexCoords = aTexCoords;
	gl_Position = vec4(aPos, 0.0, 1.0);
}

#type fragment
#version 430 core
layout (location = 0) out vec4 FragColor;

in vec2 fTexCoords;

uniform sampler2D uMainTexture;

void main()
{
	FragColor = vec4(texture(uMainTexture, fTexCoords).rgb, 1.0);
}