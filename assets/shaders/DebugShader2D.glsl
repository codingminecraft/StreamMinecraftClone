#type vertex
#version 430 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec4 aColor;
layout (location = 2) in uint aTexId;
layout (location = 3) in vec2 aTexCoord;

out vec4 fColor;
flat out uint fTexId;
out vec2 fTexCoord;

uniform mat4 uProjection;
uniform mat4 uView;
uniform int uZIndex;

void main()
{
	fColor = aColor;
  fTexCoord = aTexCoord;
  fTexId = aTexId;
	gl_Position = uProjection * uView * vec4(aPos.x, aPos.y, float(uZIndex), 1.0);
}

#type fragment
#version 430 core
#define numTextures 8
out vec4 FragColor;

in vec4 fColor;
flat in uint fTexId;
in vec2 fTexCoord;

uniform usampler2D uFontTextures[numTextures];
uniform sampler2D uTextures[numTextures];

void main()
{
    if (int(fTexId) == 0)
    {
	    FragColor = fColor;
    }
    else if (int(fTexId) < 9)
    {
      float c = float(texture(uFontTextures[int(fTexId) - 1], fTexCoord).r);
      FragColor = vec4(c / 255.0, c / 255.0, c / 255.0, c / 255.0) * fColor;
    }
    else
    {
      FragColor = texture(uTextures[int(fTexId) - numTextures - 1], fTexCoord) * fColor;
    }
}