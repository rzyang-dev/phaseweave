/* gl43.h - 只声明本程序用到的 OpenGL 4.3 接口。
 * MinGW 自带的 GL/glext.h 是 2000 年的 SGI 版本，没有 compute shader，
 * 所以这里自带一份最小子集；函数在运行时经 wglGetProcAddress 取址。 */
#ifndef DP_GL43_H
#define DP_GL43_H

#include <windows.h>
#include <GL/gl.h>
#include <stddef.h>

#define DP_GL_COMPUTE_SHADER                0x91B9
#define DP_GL_VERTEX_SHADER                 0x8B31
#define DP_GL_FRAGMENT_SHADER               0x8B30
#define DP_GL_COMPILE_STATUS                0x8B81
#define DP_GL_LINK_STATUS                   0x8B82
#define DP_GL_INFO_LOG_LENGTH               0x8B84
#define DP_GL_SHADING_LANGUAGE_VERSION      0x8B8C
#define DP_GL_SHADER_STORAGE_BUFFER         0x90D2
#define DP_GL_SHADER_STORAGE_BARRIER_BIT    0x00002000
#define DP_GL_SHADER_IMAGE_ACCESS_BARRIER_BIT 0x00000020
#define DP_GL_TEXTURE_FETCH_BARRIER_BIT     0x00000008
#define DP_GL_R32F                          0x822E
#define DP_GL_RGBA8                         0x8058
#define DP_GL_READ_WRITE                    0x88BA
#define DP_GL_DYNAMIC_DRAW                  0x88E8
#define DP_GL_STREAM_DRAW                   0x88E0
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER                     0x8892
#endif
#define DP_GL_FRAMEBUFFER                   0x8D40
#define DP_GL_COLOR_ATTACHMENT0             0x8CE0
#define DP_GL_FRAMEBUFFER_COMPLETE          0x8CD5

/* MinGW 的 GL/gl.h 很旧，下面这些 1.2/1.3 常量不一定有 */
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_CLAMP
#define GL_CLAMP 0x2900
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_TEXTURE_ENV
#define GL_TEXTURE_ENV 0x2300
#endif
#ifndef GL_MODULATE
#define GL_MODULATE 0x2100
#endif

typedef struct {
    GLuint (APIENTRY *CreateShader)(GLenum type);
    void   (APIENTRY *ShaderSource)(GLuint shader, GLsizei count, const char *const *string, const GLint *length);
    void   (APIENTRY *CompileShader)(GLuint shader);
    void   (APIENTRY *GetShaderiv)(GLuint shader, GLenum pname, GLint *params);
    void   (APIENTRY *GetShaderInfoLog)(GLuint shader, GLsizei bufSize, GLsizei *length, char *infoLog);
    void   (APIENTRY *DeleteShader)(GLuint shader);

    GLuint (APIENTRY *CreateProgram)(void);
    void   (APIENTRY *AttachShader)(GLuint program, GLuint shader);
    void   (APIENTRY *LinkProgram)(GLuint program);
    void   (APIENTRY *GetProgramiv)(GLuint program, GLenum pname, GLint *params);
    void   (APIENTRY *GetProgramInfoLog)(GLuint program, GLsizei bufSize, GLsizei *length, char *infoLog);
    void   (APIENTRY *UseProgram)(GLuint program);
    void   (APIENTRY *DeleteProgram)(GLuint program);

    GLint  (APIENTRY *GetUniformLocation)(GLuint program, const char *name);
    void   (APIENTRY *Uniform1i)(GLint location, GLint v0);
    void   (APIENTRY *Uniform1f)(GLint location, GLfloat v0);
    void   (APIENTRY *Uniform2f)(GLint location, GLfloat v0, GLfloat v1);

    void   (APIENTRY *GenBuffers)(GLsizei n, GLuint *buffers);
    void   (APIENTRY *BindBuffer)(GLenum target, GLuint buffer);
    void   (APIENTRY *BufferData)(GLenum target, ptrdiff_t size, const void *data, GLenum usage);
    void   (APIENTRY *BufferSubData)(GLenum target, ptrdiff_t offset, ptrdiff_t size, const void *data);
    void   (APIENTRY *GetBufferSubData)(GLenum target, ptrdiff_t offset, ptrdiff_t size, void *data);
    void   (APIENTRY *DeleteBuffers)(GLsizei n, const GLuint *buffers);
    void   (APIENTRY *BindBufferBase)(GLenum target, GLuint index, GLuint buffer);

    void   (APIENTRY *GenTextures)(GLsizei n, GLuint *textures);
    void   (APIENTRY *BindTexture)(GLenum target, GLuint texture);
    void   (APIENTRY *TexStorage2D)(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height);
    void   (APIENTRY *TexSubImage2D)(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                                     GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels);
    void   (APIENTRY *TexParameteri)(GLenum target, GLenum pname, GLint param);
    void   (APIENTRY *GetTexImage)(GLenum target, GLint level, GLenum format, GLenum type, void *pixels);
    void   (APIENTRY *DeleteTextures)(GLsizei n, const GLuint *textures);
    void   (APIENTRY *ActiveTexture)(GLenum texture);

    void   (APIENTRY *GenVertexArrays)(GLsizei n, GLuint *arrays);
    void   (APIENTRY *BindVertexArray)(GLuint array);
    void   (APIENTRY *DeleteVertexArrays)(GLsizei n, const GLuint *arrays);

    void   (APIENTRY *BindImageTexture)(GLuint unit, GLuint texture, GLint level, GLboolean layered,
                                        GLint layer, GLenum access, GLenum format);
    void   (APIENTRY *EnableVertexAttribArray)(GLuint index);
    void   (APIENTRY *VertexAttribPointer)(GLuint index, GLint size, GLenum type,
                                           GLboolean normalized, GLsizei stride, const void *pointer);
    void   (APIENTRY *DispatchCompute)(GLuint num_groups_x, GLuint num_groups_y, GLuint num_groups_z);
    void   (APIENTRY *MemoryBarrier)(GLbitfield barriers);

    void   (APIENTRY *GenFramebuffers)(GLsizei n, GLuint *framebuffers);
    void   (APIENTRY *BindFramebuffer)(GLenum target, GLuint framebuffer);
    void   (APIENTRY *FramebufferTexture2D)(GLenum target, GLenum attachment,
                                            GLenum textarget, GLuint texture, GLint level);
    GLenum (APIENTRY *CheckFramebufferStatus)(GLenum target);
    void   (APIENTRY *DeleteFramebuffers)(GLsizei n, const GLuint *framebuffers);
} DpGl43;

#endif