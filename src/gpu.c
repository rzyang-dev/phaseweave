/* gpu.c - WGL 4.3 core 上下文、计算着色器、两条显示路径（现代/兼容） */
#include "gpu.h"

#include "gl43.h"
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

    GLint u_res, u_steps;
    GLint u_m1, u_m2, u_l1, u_l2, u_g, u_damp, u_dt;
    GLint u_field, u_lut;

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
    LOAD_GL(Uniform1f);
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
    g->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    g->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    g->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    g->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g->gl.BindImageTexture(0, g->field_tex, 0, GL_FALSE, 0, DP_GL_READ_WRITE, DP_GL_R32F);

    if (!g->vao) {
        g->gl.GenVertexArrays(1, &g->vao);
        g->gl.BindVertexArray(g->vao);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
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
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, g->gl12 ? GL_CLAMP_TO_EDGE : GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, g->gl12 ? GL_CLAMP_TO_EDGE : GL_CLAMP);
        g->legacy_tex_res = res;
    }
    glBindTexture(GL_TEXTURE_2D, g->legacy_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, res, res, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

void gpu_draw(Gpu *g, int w, int h)
{
    if (!g || w <= 0 || h <= 0) return;
    glViewport(0, 0, w, h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (g->modern) {
        g->gl.UseProgram(g->draw_prog);
        g->gl.ActiveTexture(GL_TEXTURE0 + 1);
        g->gl.BindTexture(GL_TEXTURE_2D, g->lut_tex);
        g->gl.Uniform1i(g->u_lut, 1);
        g->gl.ActiveTexture(GL_TEXTURE0);
        g->gl.BindTexture(GL_TEXTURE_2D, g->field_tex);
        g->gl.Uniform1i(g->u_field, 0);
        g->gl.BindVertexArray(g->vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        return;
    }

    /* 兼容路径：GL 1.1 固定管线，一张 RGBA 纹理铺满窗口。 */
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g->legacy_tex);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f, -1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f( 1.0f, -1.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f( 1.0f,  1.0f);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f,  1.0f);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

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
    if (g->u_res < 0 || g->u_steps < 0 || g->u_field < 0 || g->u_lut < 0) {
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
        if (g->vao) g->gl.DeleteVertexArrays(1, &g->vao);
    }
    if (g->legacy_tex) glDeleteTextures(1, &g->legacy_tex);
    if (g->rc) {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(g->rc);
    }
    free(g);
}