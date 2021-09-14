#type vertex
#version 330 core
layout (location = 0) in vec3 aStart;
layout (location = 1) in vec3 aEnd;
layout (location = 2) in float aIsStart;
layout (location = 3) in float aDirection;
layout (location = 4) in float aStrokeWidth;
layout (location = 5) in vec4 aColor;

out vec4 fColor;

uniform mat4 uProjection;
uniform mat4 uView;
uniform float uAspectRatio;

void main()
{
	fColor = aColor;

	mat4 projView = uProjection * uView;

	// Into clip space
	vec3 pos = aIsStart == 1.0 ? aStart : aEnd;
	vec4 currentProjected = projView * vec4(pos, 1.0);
	vec4 endProjected = projView * vec4(aEnd, 1.0);
	vec4 startProjected = projView * vec4(aStart, 1.0);

	// Into NDC space [-1, 1]
	vec2 currentScreen = currentProjected.xy / currentProjected.w;
	vec2 endScreen = endProjected.xy / endProjected.w;
	vec2 startScreen = startProjected.xy / startProjected.w;

	// Correct for aspect ratio
	currentScreen.x *= uAspectRatio;
	endScreen.x *= uAspectRatio;
	startScreen.x *= uAspectRatio;

	// Normal of line (B - A)
	vec2 dir = aIsStart == 1.0
		? normalize(endScreen - currentScreen)
		: normalize(currentScreen - startScreen);
	vec2 normal = vec2(-dir.y, dir.x);

	// Extrude from the center and correct aspect ratio
	normal *= aStrokeWidth / 2.0; //strokeWidth / 2.0;
	normal.x /= uAspectRatio;

	// Offset by the direction of this point in the pair (-1 or 1)
	vec4 offset = vec4(normal * aDirection, 0.0, 0.0);
	gl_Position = currentProjected + offset;
}

#type fragment
#version 330 core
out vec4 FragColor;

in vec4 fColor;

void main()
{
	FragColor = fColor;
}