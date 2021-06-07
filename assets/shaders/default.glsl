#type vertex
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec2 aTexCoords;

out vec4 fColor;
out vec2 fTexCoords;

void main()
{
	fTexCoords = aTexCoords;
	fColor = aColor;
	gl_Position = vec4(aPos.x, aPos.y, aPos.z, 1.0);
}

#type fragment
#version 330 core
out vec4 FragColor;

in vec4 fColor;
in vec2 fTexCoords;

uniform sampler2D uTexture;

void main()
{
	FragColor = texture(uTexture, fTexCoords);;
}    