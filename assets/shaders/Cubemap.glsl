#type vertex
#version 330 core
layout (location = 0) in vec3 aPos;

out vec3 TexCoords;

uniform mat4 uProjection;
uniform mat4 uView;

void main()
{
    TexCoords = aPos;
    gl_Position = uProjection * uView * vec4(aPos, 1.0);
} 

#type fragment
#version 330 core
out vec4 FragColor;

in vec3 TexCoords;

uniform samplerCube uSkybox;

void main()
{    
    FragColor = vec4(texture(uSkybox, TexCoords).rgb, 1);
}