#version 430 core
/* atlas.frag - CPU-computed RGBA map, normalized view + nearest sampling. */
in vec2 vUV;
out vec4 oColor;
uniform sampler2D uAtlas;
uniform vec2  uCenter;
uniform float uSpan;
uniform int   uPeriodic;

void main()
{
    vec2 world = uCenter + (vUV - 0.5) * uSpan;
    vec2 uv = world + 0.5;
    if (uPeriodic != 0) {
        uv = fract(uv);
    } else {
        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
            oColor = vec4(0.035, 0.045, 0.055, 1.0);
            return;
        }
        uv = min(uv, vec2(0.999999));
    }
    oColor = texture(uAtlas, uv);
}
