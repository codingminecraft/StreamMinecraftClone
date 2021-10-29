#type vertex
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in uint aTexId;
layout (location = 2) in vec2 aTexCoord;

flat out uint fTexId;
out vec2 fTexCoord;

uniform mat4 uProjection;
uniform mat4 uView;

void main()
{
  	fTexCoord = aTexCoord;
  	fTexId = aTexId;
	gl_Position = uProjection * uView * vec4(aPos, 1.0);
}

#type fragment
#version 330 core
#define numTextures 16
out vec4 FragColor;

flat in uint fTexId;
in vec2 fTexCoord;

uniform sampler2D uTextures[numTextures];

void main()
{
	FragColor = texture(uTextures[int(fTexId)], fTexCoord);
}