#type vertex
#version 430 core
layout (location = 0) in vec3 aPosition;
layout (location = 1) in uint aColor;

uniform mat4 uProjection;
uniform mat4 uView;

out vec4 fColor;

void main()
{
    float r = float(aColor & 0xFF) / 255.0;
    float g = float((aColor & 0xFF00) >> 8) / 255.0;
    float b = float((aColor & 0xFF0000) >> 16) / 255.0;
    float a = float((aColor & 0xFF000000) >> 24) / 255.0;
    fColor = vec4(r, g, b, a);
	gl_Position = uProjection * uView * vec4(aPosition, 1.0);
}

#type fragment
#version 430 core
layout (location = 0) out vec4 FragColor;

in vec4 fColor;

void main()
{
	FragColor = fColor;
}