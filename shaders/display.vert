#version 430 core
/* display.vert - fullscreen triangle generated from gl_VertexID (no VBO).
 * v = 0 is the top edge, matching grid row 0. */
out vec2 vUV;

void main()
{
    const vec2 pos[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    const vec2 uv[3]  = vec2[3](vec2(0.0, 1.0),  vec2(2.0, 1.0),  vec2(0.0, -1.0));
    vUV = uv[gl_VertexID];
    gl_Position = vec4(pos[gl_VertexID], 0.0, 1.0);
}
