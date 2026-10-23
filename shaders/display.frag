#version 430 core
/* display.frag - 相空间场：视图变换 + 最近邻像素 + Lua 调色板。
 * uSpan 是世界坐标下窗口覆盖的宽度（= range / zoom）；vUV.y=0 是窗口顶边。
 * periodic 时 fract() 平铺；否则超出 [-range/2, range/2] 的区域画成深色背景。 */
in vec2 vUV;
out vec4 oColor;

uniform sampler2D uField;   /* R32F 场值，GL_NEAREST */
uniform sampler2D uLut;     /* 1024x1 调色板，线性 */
uniform vec2  uCenter;
uniform float uSpan;
uniform float uRangeMin;
uniform float uRange;
uniform int   uPeriodic;

void main()
{
    vec2 world = uCenter + (vUV - 0.5) * uSpan;
    vec2 uv = (world - vec2(uRangeMin)) / uRange;
    vec3 bg = vec3(0.035, 0.045, 0.055);

    if (uPeriodic != 0) {
        uv = fract(uv);
    } else {
        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
            oColor = vec4(bg, 1.0);
            return;
        }
        uv = min(uv, vec2(0.999999));
    }

    float u = texture(uField, uv).r;
    vec3 c = texture(uLut, vec2(fract(u), 0.5)).rgb;
    oColor = vec4(c, 1.0);
}
