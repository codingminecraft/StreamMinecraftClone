#type vertex
#version 430 core
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
#version 430 core
layout (location = 0) out vec4 FragColor;

in vec3 TexCoords;

uniform samplerCube uInSkybox;
uniform samplerCube uOutSkybox;
uniform float uBlendValue;

void main()
{    
    FragColor = mix(vec4(texture(uInSkybox, TexCoords).rgb, 1), vec4(texture(uOutSkybox, TexCoords).rgb, 1), uBlendValue);
}