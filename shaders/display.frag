#version 430 core
/* display.frag - phase field -> Lua palette (1024x1 RGBA8, repeat+linear). */
in vec2 vUV;
out vec4 oColor;

uniform sampler2D uField;
uniform sampler2D uLut;

void main()
{
    float u = texture(uField, vUV).r;
    vec3 c = texture(uLut, vec2(fract(u), 0.5)).rgb;
    oColor = vec4(c, 1.0);
}
