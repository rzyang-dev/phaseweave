/* gpu.c - WGL 4.3 core 上下文、计算着色器、两条显示路径（现代/兼容） */
#include "gpu.h"

#include "gl43.h"
#include "phase_view.h"
#include "shaders_gen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Gpu {
    HWND  hwnd;
    HDC   hdc;
    HGLRC rc;
    int   modern;
    int   legacy;
    int   vsync;
    int   res;
    double range;
    int   field_filter;
    int   swap_ok;
    int   gl12;
    DpGl43 gl;

    GLuint ssbo;
    GLuint field_tex;
    GLuint lut_tex;
    GLuint comp_prog;
    GLuint draw_prog;
    GLuint vao;
    GLuint legacy_tex;
    int    legacy_tex_res;

    GLuint line_prog;
    GLuint line_vao;
    GLuint line_vbo;

    GLuint atlas_tex;
    GLuint atlas_prog;
    GLint  u_atlas_tex, u_atlas_center, u_atlas_span, u_atlas_periodic;
    int    atlas_res;

    GLint u_res, u_steps;
    GLint u_m1, u_m2, u_l1, u_l2, u_g, u_damp, u_dt;
    GLint u_field, u_lut;
    GLint u_center, u_span, u_range_min, u_range, u_periodic;

    GLuint custom_prog;
    GLuint custom_cells;
    GLuint custom_chans;
    GLint  u_custom_res, u_custom_steps;

    char version[160];
    char renderer[160];
    char reason[200];
};

/* ---------------------------------------------------------------- 工具 */

static void *gl_proc(const char *name)
{
    void *p = (void *)wglGetProcAddress(name);
    if (p == NULL || p == (void *)1 || p == (void *)2 || p == (void *)3 || p == (void *)-1) {
        HMODULE mod = GetModuleHandleA("opengl32.dll");
        if (mod) p = (void *)GetProcAddress(mod, name);
    }
    return p;
}

#define LOAD_GL(name) do { \
    g->gl.name = (__typeof__(g->gl.name))gl_proc("gl" #name); \
    if (!g->gl.name) missing = #name; \
} while (0)

static int load_gl(Gpu *g, char *missing, size_t missz)
{
    missing[0] = 0;
    LOAD_GL(CreateShader);      LOAD_GL(ShaderSource);     LOAD_GL(CompileShader);
    LOAD_GL(GetShaderiv);       LOAD_GL(GetShaderInfoLog); LOAD_GL(DeleteShader);
    LOAD_GL(CreateProgram);     LOAD_GL(AttachShader);     LOAD_GL(LinkProgram);
    LOAD_GL(GetProgramiv);      LOAD_GL(GetProgramInfoLog);LOAD_GL(UseProgram);
    LOAD_GL(DeleteProgram);     LOAD_GL(GetUniformLocation); LOAD_GL(Uniform1i);
    LOAD_GL(Uniform1f);          LOAD_GL(Uniform2f);
    LOAD_GL(EnableVertexAttribArray); LOAD_GL(VertexAttribPointer);
    LOAD_GL(GenBuffers);        LOAD_GL(BindBuffer);       LOAD_GL(BufferData);
    LOAD_GL(BufferSubData);     LOAD_GL(GetBufferSubData); LOAD_GL(DeleteBuffers);
    LOAD_GL(BindBufferBase);
    LOAD_GL(GenTextures);       LOAD_GL(BindTexture);      LOAD_GL(TexStorage2D);
    LOAD_GL(TexSubImage2D);     LOAD_GL(TexParameteri);    LOAD_GL(GetTexImage);
    LOAD_GL(DeleteTextures);    LOAD_GL(ActiveTexture);
    LOAD_GL(GenVertexArrays);   LOAD_GL(BindVertexArray);  LOAD_GL(DeleteVertexArrays);
    LOAD_GL(BindImageTexture);  LOAD_GL(DispatchCompute);  LOAD_GL(MemoryBarrier);
    LOAD_GL(GenFramebuffers);   LOAD_GL(BindFramebuffer);  LOAD_GL(FramebufferTexture2D);
    LOAD_GL(CheckFramebufferStatus); LOAD_GL(DeleteFramebuffers);
    (void)missz;
    return missing[0] == 0;
}

static GLuint compile_shader(Gpu *g, GLenum type, const char *src, const char *label,
                             char *err, size_t errsz)
{
    GLuint sh = g->gl.CreateShader(type);
    GLint ok = 0;
    if (!sh) {
        snprintf(err, errsz, "%s: glCreateShader failed", label);
        return 0;
    }
    g->gl.ShaderSource(sh, 1, &src, NULL);
    g->gl.CompileShader(sh);
    g->gl.GetShaderiv(sh, DP_GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        log[0] = 0;
        g->gl.GetShaderInfoLog(sh, (GLsizei)sizeof(log), NULL, log);
        snprintf(err, errsz, "%s: %s", label, log);
        g->gl.DeleteShader(sh);
        return 0;
    }
    return sh;
}

static GLuint link_program(Gpu *g, GLuint vs, GLuint fs, const char *label,
                           char *err, size_t errsz)
{
    GLuint prog = g->gl.CreateProgram();
    GLint ok = 0;
    if (!prog) {
        snprintf(err, errsz, "%s: glCreateProgram failed", label);
        return 0;
    }
    if (vs) g->gl.AttachShader(prog, vs);
    if (fs) g->gl.AttachShader(prog, fs);
    g->gl.LinkProgram(prog);
    g->gl.GetProgramiv(prog, DP_GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        log[0] = 0;
        g->gl.GetProgramInfoLog(prog, (GLsizei)sizeof(log), NULL, log);
        snprintf(err, errsz, "%s: %s", label, log);
        g->gl.DeleteProgram(prog);
        return 0;
    }
    if (vs) g->gl.DeleteShader(vs);
    if (fs) g->gl.DeleteShader(fs);
    return prog;
}

static void setf(Gpu *g, GLint loc, float v)
{
    if (loc >= 0) g->gl.Uniform1f(loc, v);
}

/* ------------------------------------------------------------ 资源管理 */

void gpu_set_res(Gpu *g, int res)
{
    if (!g || res < 16) return;
    g->res = res;
    if (!g->modern) return;

    if (g->ssbo) g->gl.DeleteBuffers(1, &g->ssbo);
    if (g->field_tex) g->gl.DeleteTextures(1, &g->field_tex);
    g->ssbo = 0;
    g->field_tex = 0;

    g->gl.GenBuffers(1, &g->ssbo);
    g->gl.BindBuffer(DP_GL_SHADER_STORAGE_BUFFER, g->ssbo);
    g->gl.BufferData(DP_GL_SHADER_STORAGE_BUFFER,
                     (ptrdiff_t)res * (ptrdiff_t)res * 16, NULL, DP_GL_DYNAMIC_DRAW);
    g->gl.BindBufferBase(DP_GL_SHADER_STORAGE_BUFFER, 0, g->ssbo);

    g->gl.GenTextures(1, &g->field_tex);
    g->gl.BindTexture(GL_TEXTURE_2D, g->field_tex);
    g->gl.TexStorage2D(GL_TEXTURE_2D, 1, DP_GL_R32F, res, res);
    /* 低分辨率下必须最近邻：线性过滤会把每个 texel 与邻居混合，
     * 64x64 放大到 512x512 时整幅图会变成糊的渐变。 */
    g->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    g->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    /* 循环视图靠 shader 的 fract() 平铺，纹理本身用 REPEAT 更稳。 */
    g->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    g->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    g->gl.BindImageTexture(0, g->field_tex, 0, GL_FALSE, 0, DP_GL_READ_WRITE, DP_GL_R32F);
    g->field_filter = GL_NEAREST;

    if (!g->vao) {
        g->gl.GenVertexArrays(1, &g->vao);
        g->gl.BindVertexArray(g->vao);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
}

void gpu_set_range(Gpu *g, double range)
{
    if (g && range > 1e-9) g->range = range;
}

void gpu_set_lut(Gpu *g, const unsigned char *lut)
{
    unsigned char rgba[DP_LUT_N * 4];
    int i;
    if (!g || !lut || !g->modern) return;
    for (i = 0; i < DP_LUT_N; ++i) {
        rgba[i * 4 + 0] = lut[i * 3 + 0];
        rgba[i * 4 + 1] = lut[i * 3 + 1];
        rgba[i * 4 + 2] = lut[i * 3 + 2];
        rgba[i * 4 + 3] = 255;
    }
    if (!g->lut_tex) {
        g->gl.GenTextures(1, &g->lut_tex);
        g->gl.BindTexture(GL_TEXTURE_2D, g->lut_tex);
        g->gl.TexStorage2D(GL_TEXTURE_2D, 1, DP_GL_RGBA8, DP_LUT_N, 1);
        g->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        g->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        g->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        g->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
        g->gl.BindTexture(GL_TEXTURE_2D, g->lut_tex);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    g->gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, DP_LUT_N, 1,
                        GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

int gpu_upload_state(Gpu *g, const float *state, int res)
{
    if (!g || !g->modern || !state || res != g->res) return 0;
    g->gl.BindBuffer(DP_GL_SHADER_STORAGE_BUFFER, g->ssbo);
    g->gl.BufferSubData(DP_GL_SHADER_STORAGE_BUFFER, 0,
                        (ptrdiff_t)res * (ptrdiff_t)res * 16, state);
    g->gl.BindBufferBase(DP_GL_SHADER_STORAGE_BUFFER, 0, g->ssbo);
    return 1;
}

int gpu_download_state(Gpu *g, float *state, int res)
{
    if (!g || !g->modern || !state || res != g->res) return 0;
    g->gl.BindBuffer(DP_GL_SHADER_STORAGE_BUFFER, g->ssbo);
    g->gl.GetBufferSubData(DP_GL_SHADER_STORAGE_BUFFER, 0,
                           (ptrdiff_t)res * (ptrdiff_t)res * 16, state);
    return 1;
}

void gpu_step(Gpu *g, int steps, const DpParams *p)
{
    GLuint groups;
    if (!g || !g->modern) return;
    if (steps < 0) steps = 0;
    groups = (GLuint)((g->res + 15) / 16);

    g->gl.UseProgram(g->comp_prog);
    g->gl.BindBufferBase(DP_GL_SHADER_STORAGE_BUFFER, 0, g->ssbo);
    g->gl.BindImageTexture(0, g->field_tex, 0, GL_FALSE, 0, DP_GL_READ_WRITE, DP_GL_R32F);
    g->gl.Uniform1i(g->u_res, g->res);
    g->gl.Uniform1i(g->u_steps, steps);
    setf(g, g->u_m1, (float)p->m1);
    setf(g, g->u_m2, (float)p->m2);
    setf(g, g->u_l1, (float)p->l1);
    setf(g, g->u_l2, (float)p->l2);
    setf(g, g->u_g, (float)p->g);
    setf(g, g->u_damp, (float)p->damping);
    setf(g, g->u_dt, (float)p->dt);

    g->gl.DispatchCompute(groups, groups, 1);
    g->gl.MemoryBarrier(DP_GL_SHADER_STORAGE_BARRIER_BIT |
                        DP_GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                        DP_GL_TEXTURE_FETCH_BARRIER_BIT);
}

void gpu_upload_field(Gpu *g, const float *field, int res)
{
    if (!g || !g->modern || !field || res != g->res) return;
    g->gl.ActiveTexture(GL_TEXTURE0);
    g->gl.BindTexture(GL_TEXTURE_2D, g->field_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    g->gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, res, res, GL_RED, GL_FLOAT, field);
}

void gpu_upload_rgba_legacy(Gpu *g, const unsigned char *rgba, int res)
{
    if (!g || !g->legacy || !rgba) return;
    if (!g->legacy_tex || g->legacy_tex_res != res) {
        if (!g->legacy_tex) glGenTextures(1, &g->legacy_tex);
        glBindTexture(GL_TEXTURE_2D, g->legacy_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, res, res, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, g->gl12 ? GL_CLAMP_TO_EDGE : GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, g->gl12 ? GL_CLAMP_TO_EDGE : GL_CLAMP);
        g->legacy_tex_res = res;
    }
    glBindTexture(GL_TEXTURE_2D, g->legacy_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, res, res, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

static void draw_modern_view(Gpu *g, const PhaseView *v)
{
    double span = g->range / (v->zoom > 0.0 ? v->zoom : 1.0);
    g->gl.UseProgram(g->draw_prog);
    g->gl.ActiveTexture(GL_TEXTURE0 + 1);
    g->gl.BindTexture(GL_TEXTURE_2D, g->lut_tex);
    g->gl.Uniform1i(g->u_lut, 1);
    g->gl.ActiveTexture(GL_TEXTURE0);
    g->gl.BindTexture(GL_TEXTURE_2D, g->field_tex);
    g->gl.Uniform1i(g->u_field, 0);
    g->gl.Uniform2f(g->u_center, (GLfloat)v->cx, (GLfloat)v->cy);
    setf(g, g->u_span, (float)span);
    setf(g, g->u_range_min, (float)(-g->range * 0.5));
    setf(g, g->u_range, (float)g->range);
    g->gl.Uniform1i(g->u_periodic, v->periodic ? 1 : 0);
    g->gl.BindVertexArray(g->vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

static void draw_legacy_view(Gpu *g, const PhaseView *v)
{
    double span = g->range / (v->zoom > 0.0 ? v->zoom : 1.0);
    double half = span * 0.5;
    double rmin = -g->range * 0.5, rmax = g->range * 0.5;
    double x0 = v->cx - half, x1 = v->cx + half;
    double y0 = v->cy - half, y1 = v->cy + half;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g->legacy_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    glBegin(GL_QUADS);
    if (v->periodic) {
        double u0 = (x0 - rmin) / g->range;
        double u1 = (x1 - rmin) / g->range;
        double v0 = (y0 - rmin) / g->range;
        double v1 = (y1 - rmin) / g->range;
        glTexCoord2d(u0, v0); glVertex2f(-1.0f,  1.0f);
        glTexCoord2d(u1, v0); glVertex2f( 1.0f,  1.0f);
        glTexCoord2d(u1, v1); glVertex2f( 1.0f, -1.0f);
        glTexCoord2d(u0, v1); glVertex2f(-1.0f, -1.0f);
    } else {
        double fx0 = x0 > rmin ? x0 : rmin;
        double fx1 = x1 < rmax ? x1 : rmax;
        double fy0 = y0 > rmin ? y0 : rmin;
        double fy1 = y1 < rmax ? y1 : rmax;
        if (fx1 > fx0 && fy1 > fy0) {
            float nx0 = (float)((fx0 - v->cx) / half);
            float nx1 = (float)((fx1 - v->cx) / half);
            float ny0 = (float)(-(fy0 - v->cy) / half);
            float ny1 = (float)(-(fy1 - v->cy) / half);
            double u0 = (fx0 - rmin) / g->range;
            double u1 = (fx1 - rmin) / g->range;
            double v0 = (fy0 - rmin) / g->range;
            double v1 = (fy1 - rmin) / g->range;
            glTexCoord2d(u0, v0); glVertex2f(nx0, ny0);
            glTexCoord2d(u1, v0); glVertex2f(nx1, ny0);
            glTexCoord2d(u1, v1); glVertex2f(nx1, ny1);
            glTexCoord2d(u0, v1); glVertex2f(nx0, ny1);
        }
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

void gpu_draw_view(Gpu *g, int w, int h, const PhaseView *v)
{
    if (!g || !v || w <= 0 || h <= 0) return;
    glViewport(0, 0, w, h);
    glClearColor(0.035f, 0.045f, 0.055f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (g->modern) draw_modern_view(g, v);
    else if (g->legacy) draw_legacy_view(g, v);
}

void gpu_draw(Gpu *g, int w, int h)
{
    PhaseView v;
    pv_init(&v);
    v.grid = 0;
    v.circle = 0;
    v.periodic = 0;
    v.zoom = 1.0;
    v.cx = 0.0;
    v.cy = 0.0;
    gpu_draw_view(g, w, h, &v);
}

void gpu_upload_atlas_rgba(Gpu *g, const unsigned char *rgba, int res)
{
    if (!g || !rgba || res <= 0) return;
    if (!g->modern) return;
    if (!g->atlas_tex || g->atlas_res != res) {
        if (g->atlas_tex) g->gl.DeleteTextures(1, &g->atlas_tex);
        g->gl.GenTextures(1, &g->atlas_tex);
        g->gl.BindTexture(GL_TEXTURE_2D, g->atlas_tex);
        g->gl.TexStorage2D(GL_TEXTURE_2D, 1, DP_GL_RGBA8, res, res);
        g->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        g->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        g->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        g->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        g->atlas_res = res;
    } else {
        g->gl.BindTexture(GL_TEXTURE_2D, g->atlas_tex);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    g->gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, res, res,
                        GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

void gpu_draw_atlas_view(Gpu *g, int w, int h, const PhaseView *v)
{
    if (!g || !g->modern || !v || !g->atlas_prog || !g->atlas_tex) return;
    glViewport(0, 0, w, h);
    glClearColor(0.035f, 0.045f, 0.055f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    g->gl.UseProgram(g->atlas_prog);
    g->gl.ActiveTexture(GL_TEXTURE0);
    g->gl.BindTexture(GL_TEXTURE_2D, g->atlas_tex);
    g->gl.Uniform1i(g->u_atlas_tex, 0);
    g->gl.Uniform2f(g->u_atlas_center, (GLfloat)v->cx, (GLfloat)v->cy);
    setf(g, g->u_atlas_span, (float)(1.0 / (v->zoom > 0.0 ? v->zoom : 1.0)));
    g->gl.Uniform1i(g->u_atlas_periodic, v->periodic ? 1 : 0);
    g->gl.BindVertexArray(g->vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void gpu_draw_lines(Gpu *g, const float *verts, int nverts, float width)
{
    int i;
    if (!g || !verts || nverts <= 0) return;
    if (g->modern) {
        if (!g->line_prog || !g->line_vao || !g->line_vbo) return;
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glLineWidth(width);
        g->gl.UseProgram(g->line_prog);
        g->gl.BindVertexArray(g->line_vao);
        g->gl.BindBuffer(GL_ARRAY_BUFFER, g->line_vbo);
        g->gl.BufferData(GL_ARRAY_BUFFER, (ptrdiff_t)nverts * 6 * (ptrdiff_t)sizeof(float),
                         verts, DP_GL_STREAM_DRAW);
        glDrawArrays(GL_LINES, 0, nverts);
        glLineWidth(1.0f);
        glDisable(GL_BLEND);
        return;
    }
    if (g->legacy) {
        glDisable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glLineWidth(width);
        glBegin(GL_LINES);
        for (i = 0; i < nverts; ++i) {
            const float *t = verts + i * 6;
            glColor4f(t[2], t[3], t[4], t[5]);
            glVertex2f(t[0], t[1]);
        }
        glEnd();
        glLineWidth(1.0f);
        glDisable(GL_BLEND);
    }
}

int gpu_read_cell(Gpu *g, int index, float out[4])
{
    if (!g || !g->modern || !out || index < 0 || index >= g->res * g->res) return 0;
    g->gl.BindBuffer(DP_GL_SHADER_STORAGE_BUFFER, g->ssbo);
    g->gl.GetBufferSubData(DP_GL_SHADER_STORAGE_BUFFER, (ptrdiff_t)index * 16, 16, out);
    return 1;
}

int gpu_field_filter(Gpu *g) { return g ? g->field_filter : 0; }

int gpu_read_field(Gpu *g, float *field, int res)
{
    if (!g || !g->modern || !field || res != g->res) return 0;
    g->gl.ActiveTexture(GL_TEXTURE0);
    g->gl.BindTexture(GL_TEXTURE_2D, g->field_tex);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    g->gl.GetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_FLOAT, field);
    return 1;
}

int gpu_capture_rgba(Gpu *g, int w, int h, unsigned char *out_topdown)
{
    unsigned char *tmp;
    int y;
    if (!g || w <= 0 || h <= 0 || !out_topdown) return 0;
    tmp = (unsigned char *)malloc((size_t)w * (size_t)h * 4u);
    if (!tmp) return 0;
    glFinish();
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
    for (y = 0; y < h; ++y) {
        memcpy(out_topdown + (size_t)y * (size_t)w * 4u,
               tmp + (size_t)(h - 1 - y) * (size_t)w * 4u,
               (size_t)w * 4u);
    }
    free(tmp);
    return 1;
}

int gpu_capture_offscreen(Gpu *g, int w, int h, unsigned char *out_topdown)
{
    GLuint tex = 0, fbo = 0;
    unsigned char *tmp;
    int y, ok = 0;
    if (!g || !g->modern || w <= 0 || h <= 0 || !out_topdown) return 0;
    tmp = (unsigned char *)malloc((size_t)w * (size_t)h * 4u);
    if (!tmp) return 0;

    g->gl.GenTextures(1, &tex);
    g->gl.BindTexture(GL_TEXTURE_2D, tex);
    g->gl.TexStorage2D(GL_TEXTURE_2D, 1, DP_GL_RGBA8, w, h);
    g->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    g->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    g->gl.GenFramebuffers(1, &fbo);
    g->gl.BindFramebuffer(DP_GL_FRAMEBUFFER, fbo);
    g->gl.FramebufferTexture2D(DP_GL_FRAMEBUFFER, DP_GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, tex, 0);
    if (g->gl.CheckFramebufferStatus(DP_GL_FRAMEBUFFER) == DP_GL_FRAMEBUFFER_COMPLETE) {
        gpu_draw(g, w, h);
        glFinish();
        glReadBuffer(DP_GL_COLOR_ATTACHMENT0);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
        for (y = 0; y < h; ++y)
            memcpy(out_topdown + (size_t)y * (size_t)w * 4u,
                   tmp + (size_t)(h - 1 - y) * (size_t)w * 4u,
                   (size_t)w * 4u);
        ok = 1;
    }
    g->gl.BindFramebuffer(DP_GL_FRAMEBUFFER, 0);
    g->gl.DeleteFramebuffers(1, &fbo);
    g->gl.DeleteTextures(1, &tex);
    free(tmp);
    return ok;
}

const char *gpu_version(Gpu *g) { return g ? g->version : ""; }
const char *gpu_renderer(Gpu *g) { return g ? g->renderer : ""; }
const char *gpu_fallback_reason(Gpu *g) { return g ? g->reason : ""; }
int gpu_compute(Gpu *g) { return g && g->modern ? 1 : 0; }
int gpu_legacy(Gpu *g) { return g && g->legacy ? 1 : 0; }
int gpu_vsync(Gpu *g) { return g ? g->vsync : 0; }
int gpu_res(Gpu *g) { return g ? g->res : 0; }

/* ------------------------------------------------------------ 创建/销毁 */

static void query_strings(Gpu *g)
{
    const char *v = (const char *)glGetString(GL_VERSION);
    const char *r = (const char *)glGetString(GL_RENDERER);
    strncpy(g->version, v ? v : "?", sizeof(g->version) - 1);
    strncpy(g->renderer, r ? r : "?", sizeof(g->renderer) - 1);
    g->gl12 = 0;
    if (v) {
        int major = 0, minor = 0;
        if (sscanf(v, "%d.%d", &major, &minor) == 2)
            g->gl12 = (major > 1 || (major == 1 && minor >= 2));
    }
}

static int build_modern(Gpu *g, char *err, size_t errsz)
{
    char missing[64];
    if (!load_gl(g, missing, sizeof(missing))) {
        snprintf(err, errsz, "missing GL function: %s", missing);
        return 0;
    }
    {
        GLuint cs = compile_shader(g, DP_GL_COMPUTE_SHADER, DP_SHADER_FIELD,
                                   "field.comp", err, errsz);
        if (!cs) return 0;
        g->comp_prog = link_program(g, cs, 0, "compute program", err, errsz);
        if (!g->comp_prog) return 0;
    }
    {
        GLuint vs = compile_shader(g, DP_GL_VERTEX_SHADER, DP_SHADER_VERT,
                                   "display.vert", err, errsz);
        GLuint fs;
        if (!vs) return 0;
        fs = compile_shader(g, DP_GL_FRAGMENT_SHADER, DP_SHADER_FRAG,
                            "display.frag", err, errsz);
        if (!fs) { g->gl.DeleteShader(vs); return 0; }
        g->draw_prog = link_program(g, vs, fs, "display program", err, errsz);
        if (!g->draw_prog) return 0;
    }
    {
        GLuint vs = compile_shader(g, DP_GL_VERTEX_SHADER, DP_SHADER_LINE_VERT,
                                   "line.vert", err, errsz);
        GLuint fs;
        if (!vs) return 0;
        fs = compile_shader(g, DP_GL_FRAGMENT_SHADER, DP_SHADER_LINE_FRAG,
                            "line.frag", err, errsz);
        if (!fs) { g->gl.DeleteShader(vs); return 0; }
        g->line_prog = link_program(g, vs, fs, "line program", err, errsz);
        if (!g->line_prog) return 0;
    }
    {
        GLuint vs = compile_shader(g, DP_GL_VERTEX_SHADER, DP_SHADER_VERT,
                                   "atlas.vert", err, errsz);
        GLuint fs;
        if (!vs) return 0;
        fs = compile_shader(g, DP_GL_FRAGMENT_SHADER, DP_SHADER_ATLAS_FRAG,
                            "atlas.frag", err, errsz);
        if (!fs) { g->gl.DeleteShader(vs); return 0; }
        g->atlas_prog = link_program(g, vs, fs, "atlas program", err, errsz);
        if (!g->atlas_prog) return 0;
    }
    if (!g->vao) {
        g->gl.GenVertexArrays(1, &g->vao);
        g->gl.BindVertexArray(g->vao);
    }
    g->gl.GenVertexArrays(1, &g->line_vao);
    g->gl.BindVertexArray(g->line_vao);
    g->gl.GenBuffers(1, &g->line_vbo);
    g->gl.BindBuffer(GL_ARRAY_BUFFER, g->line_vbo);
    g->gl.EnableVertexAttribArray(0);
    g->gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 24, (const void *)0);
    g->gl.EnableVertexAttribArray(1);
    g->gl.VertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 24, (const void *)8);

    g->u_res  = g->gl.GetUniformLocation(g->comp_prog, "uRes");
    g->u_steps= g->gl.GetUniformLocation(g->comp_prog, "uSteps");
    g->u_m1   = g->gl.GetUniformLocation(g->comp_prog, "uM1");
    g->u_m2   = g->gl.GetUniformLocation(g->comp_prog, "uM2");
    g->u_l1   = g->gl.GetUniformLocation(g->comp_prog, "uL1");
    g->u_l2   = g->gl.GetUniformLocation(g->comp_prog, "uL2");
    g->u_g    = g->gl.GetUniformLocation(g->comp_prog, "uG");
    g->u_damp = g->gl.GetUniformLocation(g->comp_prog, "uDamp");
    g->u_dt   = g->gl.GetUniformLocation(g->comp_prog, "uDt");
    g->u_field= g->gl.GetUniformLocation(g->draw_prog, "uField");
    g->u_lut  = g->gl.GetUniformLocation(g->draw_prog, "uLut");
    g->u_center = g->gl.GetUniformLocation(g->draw_prog, "uCenter");
    g->u_span = g->gl.GetUniformLocation(g->draw_prog, "uSpan");
    g->u_range_min = g->gl.GetUniformLocation(g->draw_prog, "uRangeMin");
    g->u_range = g->gl.GetUniformLocation(g->draw_prog, "uRange");
    g->u_periodic = g->gl.GetUniformLocation(g->draw_prog, "uPeriodic");
    g->u_atlas_tex = g->gl.GetUniformLocation(g->atlas_prog, "uAtlas");
    g->u_atlas_center = g->gl.GetUniformLocation(g->atlas_prog, "uCenter");
    g->u_atlas_span = g->gl.GetUniformLocation(g->atlas_prog, "uSpan");
    g->u_atlas_periodic = g->gl.GetUniformLocation(g->atlas_prog, "uPeriodic");
    if (g->u_atlas_tex < 0 || g->u_atlas_center < 0 || g->u_atlas_span < 0 ||
        g->u_atlas_periodic < 0) {
        snprintf(err, errsz, "atlas shader uniforms were optimized out");
        return 0;
    }
    if (g->u_res < 0 || g->u_steps < 0 || g->u_field < 0 || g->u_lut < 0 ||
        g->u_center < 0 || g->u_span < 0 || g->u_range_min < 0 ||
        g->u_range < 0 || g->u_periodic < 0) {
        snprintf(err, errsz, "shader uniforms were optimized out");
        return 0;
    }
    g->modern = 1;
    return 1;
}

Gpu *gpu_create(HWND hwnd, HDC hdc, int force_legacy, char *err, size_t errsz)
{
    Gpu *g;
    PIXELFORMATDESCRIPTOR pfd;
    int pf;
    HGLRC temp = NULL;
    HGLRC core = NULL;
    typedef HGLRC (APIENTRY *PFN_CCA)(HDC, HGLRC, const int *);
    typedef BOOL (APIENTRY *PFN_SWAP)(int);
    PFN_CCA cca = NULL;
    PFN_SWAP swap = NULL;

    if (err && errsz) err[0] = 0;
    if (!hwnd || !hdc) return NULL;

    g = (Gpu *)calloc(1, sizeof(Gpu));
    if (!g) return NULL;
    g->hwnd = hwnd;
    g->hdc = hdc;
    g->range = 4.0 * DP_PI;
    g->field_filter = GL_NEAREST;

    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 0;
    pfd.cStencilBits = 0;
    pfd.iLayerType = PFD_MAIN_PLANE;

    pf = ChoosePixelFormat(hdc, &pfd);
    if (!pf || !SetPixelFormat(hdc, pf, &pfd)) {
        snprintf(err, errsz, "SetPixelFormat failed (error %lu)", (unsigned long)GetLastError());
        free(g);
        return NULL;
    }

    temp = wglCreateContext(hdc);
    if (!temp) {
        snprintf(err, errsz, "wglCreateContext failed");
        free(g);
        return NULL;
    }
    wglMakeCurrent(hdc, temp);
    cca = (PFN_CCA)wglGetProcAddress("wglCreateContextAttribsARB");

    if (!force_legacy && cca) {
        const int attribs[] = {
            0x2091, 4,   /* WGL_CONTEXT_MAJOR_VERSION_ARB */
            0x2092, 3,   /* WGL_CONTEXT_MINOR_VERSION_ARB */
            0x9126, 1,   /* WGL_CONTEXT_PROFILE_MASK_ARB = CORE */
            0
        };
        core = cca(hdc, NULL, attribs);
    }
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(temp);

    if (core) {
        wglMakeCurrent(hdc, core);
        g->rc = core;
        if (!build_modern(g, err, errsz)) {
            snprintf(g->reason, sizeof(g->reason), "%s", err && err[0] ? err : "shader build failed");
            wglMakeCurrent(NULL, NULL);
            wglDeleteContext(core);
            g->rc = NULL;
            g->modern = 0;
        }
    }

    if (!g->modern) {
        g->rc = wglCreateContext(hdc);
        if (!g->rc) {
            snprintf(err, errsz, "no OpenGL context (legacy creation failed)");
            free(g);
            return NULL;
        }
        wglMakeCurrent(hdc, g->rc);
        g->legacy = 1;
        if (g->reason[0] == 0) snprintf(g->reason, sizeof(g->reason), "compatibility context");
    }

    query_strings(g);
    swap = (PFN_SWAP)wglGetProcAddress("wglSwapIntervalEXT");
    if (swap && swap(1)) {
        g->vsync = 1;
        g->swap_ok = 1;
    }
    g->res = 0;
    return g;
}

int gpu_compile_custom(Gpu *g, const char *src, char *err, size_t errsz)
{
    GLuint cs, prog;
    if (err && errsz) err[0] = 0;
    if (!g || !g->modern) { if (err) snprintf(err, errsz, "custom compute: OpenGL 4.3 context unavailable"); return 0; }
    cs = compile_shader(g, DP_GL_COMPUTE_SHADER, src, "custom.comp", err, errsz);
    if (!cs) return 0;
    prog = link_program(g, cs, 0, "custom compute program", err, errsz);
    if (!prog) return 0;
    if (g->custom_prog) g->gl.DeleteProgram(g->custom_prog);
    g->custom_prog = prog;
    g->u_custom_res = g->gl.GetUniformLocation(prog, "uRes");
    g->u_custom_steps = g->gl.GetUniformLocation(prog, "uSteps");
    if (g->u_custom_res < 0 || g->u_custom_steps < 0) {
        if (err) snprintf(err, errsz, "custom compute: uRes/uSteps optimized out");
        return 0;
    }
    return 1;
}

int gpu_run_custom(Gpu *g, const float *cells, int res, int steps,
                   float *out_cells, float *out_chans,
                   char *err, size_t errsz)
{
    size_t n, cn;
    if (err && errsz) err[0] = 0;
    if (!g || !g->custom_prog || !cells || !out_cells || !out_chans || res <= 0) {
        if (err) snprintf(err, errsz, "custom compute: invalid state");
        return 0;
    }
    n = (size_t)res * (size_t)res;
    cn = n * 16u;
    if (n > (size_t)32768u) { if (err) snprintf(err, errsz, "custom compute: res too large"); return 0; }
    if (g->custom_cells) g->gl.DeleteBuffers(1, &g->custom_cells);
    if (g->custom_chans) g->gl.DeleteBuffers(1, &g->custom_chans);
    g->gl.GenBuffers(1, &g->custom_cells);
    g->gl.GenBuffers(1, &g->custom_chans);
    g->gl.BindBuffer(DP_GL_SHADER_STORAGE_BUFFER, g->custom_cells);
    g->gl.BufferData(DP_GL_SHADER_STORAGE_BUFFER, (ptrdiff_t)(cn * sizeof(float)), cells, 0x88E4);
    g->gl.BindBuffer(DP_GL_SHADER_STORAGE_BUFFER, g->custom_chans);
    g->gl.BufferData(DP_GL_SHADER_STORAGE_BUFFER, (ptrdiff_t)(n * 4u * sizeof(float)), NULL, 0x88E4);
    g->gl.BindBufferBase(DP_GL_SHADER_STORAGE_BUFFER, 0, g->custom_cells);
    g->gl.BindBufferBase(DP_GL_SHADER_STORAGE_BUFFER, 1, g->custom_chans);
    g->gl.UseProgram(g->custom_prog);
    g->gl.Uniform1i(g->u_custom_res, res);
    g->gl.Uniform1i(g->u_custom_steps, steps);
    g->gl.DispatchCompute((GLuint)res, (GLuint)res, 1);
    g->gl.MemoryBarrier(DP_GL_SHADER_STORAGE_BARRIER_BIT | DP_GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | DP_GL_TEXTURE_FETCH_BARRIER_BIT);
    g->gl.BindBuffer(DP_GL_SHADER_STORAGE_BUFFER, g->custom_cells);
    g->gl.GetBufferSubData(DP_GL_SHADER_STORAGE_BUFFER, 0, (ptrdiff_t)(cn * sizeof(float)), out_cells);
    g->gl.BindBuffer(DP_GL_SHADER_STORAGE_BUFFER, g->custom_chans);
    g->gl.GetBufferSubData(DP_GL_SHADER_STORAGE_BUFFER, 0, (ptrdiff_t)(n * 4u * sizeof(float)), out_chans);
    return 1;
}

void gpu_destroy(Gpu *g)
{
    if (!g) return;
    if (g->rc) wglMakeCurrent(g->hdc, g->rc);
    if (g->modern) {
        if (g->ssbo) g->gl.DeleteBuffers(1, &g->ssbo);
        if (g->field_tex) g->gl.DeleteTextures(1, &g->field_tex);
        if (g->lut_tex) g->gl.DeleteTextures(1, &g->lut_tex);
        if (g->comp_prog) g->gl.DeleteProgram(g->comp_prog);
        if (g->draw_prog) g->gl.DeleteProgram(g->draw_prog);
        if (g->line_prog) g->gl.DeleteProgram(g->line_prog);
        if (g->line_vbo) g->gl.DeleteBuffers(1, &g->line_vbo);
    if (g->atlas_tex) g->gl.DeleteTextures(1, &g->atlas_tex);
    if (g->atlas_prog) g->gl.DeleteProgram(g->atlas_prog);
        if (g->vao) g->gl.DeleteVertexArrays(1, &g->vao);
        if (g->line_vao) g->gl.DeleteVertexArrays(1, &g->line_vao);
    }
    if (g->legacy_tex) glDeleteTextures(1, &g->legacy_tex);
    if (g->rc) {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(g->rc);
    }
    free(g);
}