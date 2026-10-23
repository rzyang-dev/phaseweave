/* model_glsl.c - hand-written GLSL bodies for logistic/Hénon/double pendulum.
 * Cells layout matches Atlas: state[4] + model vals[12]; channels stride 4. */
#include "model_glsl.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int app(char *out, size_t outsz, size_t *pos, const char *fmt, ...)
{
    va_list ap;
    int n;
    if (!out || !pos || *pos >= outsz) return 0;
    va_start(ap, fmt);
    n = vsnprintf(out + *pos, outsz - *pos, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= outsz - *pos) return 0;
    *pos += (size_t)n;
    return 1;
}

static int header(const Experiment *e, char *out, size_t outsz, size_t *pos, int ode)
{
    (void)e;
    app(out, outsz, pos,
        "#version 430\n"
        "layout(local_size_x=1, local_size_y=1) in;\n"
        "layout(std430, binding=0) buffer Cells { float cells[]; };\n"
        "layout(std430, binding=1) buffer Chans { float chans[]; };\n"
        "uniform int uRes;\nuniform int uSteps;\n");
    if (ode) {
        app(out, outsz, pos, "uniform float uDt;\n");
        app(out, outsz, pos,
            "const float PI_F = 3.14159265358979323846;\n"
            "const float TWO_PI_F = 6.28318530717958647692;\n"
            "float norm_angle(float a) { return mod(a + PI_F, TWO_PI_F) - PI_F; }\n");
    }
    app(out, outsz, pos,
        "void main() {\n"
        "  int i = int(gl_GlobalInvocationID.x) + int(gl_GlobalInvocationID.y) * uRes;\n");
    return 1;
}

static int logistic(const Experiment *e, char *out, size_t outsz, size_t *pos)
{
    (void)e;
    header(e, out, outsz, pos, 0);
    app(out, outsz, pos,
        "  float sx = cells[i*16+0];\n"
        "  float lam = cells[i*16+1];\n"
        "  float mean = cells[i*16+2];\n"
        "  float count = cells[i*16+3];\n"
        "  float r = cells[i*16+4];\n"
        "  for (int k=0; k<uSteps; ++k) {\n"
        "    float x = sx;\n"
        "    float d = abs(r * (1.0 - 2.0*x));\n"
        "    if (d > 1e-12) lam += log(d);\n"
        "    mean += x; count += 1.0;\n"
        "    x = r*x*(1.0-x);\n"
        "    if (isnan(x) || x < 0.0 || x > 1.0) x = 0.0;\n"
        "    sx = x;\n"
        "  }\n"
        "  cells[i*16+0] = sx; cells[i*16+1] = lam;\n"
        "  cells[i*16+2] = mean; cells[i*16+3] = count;\n"
        "  float n = count > 0.0 ? count : 1.0;\n"
        "  chans[i*4+0] = sx;\n"
        "  chans[i*4+1] = lam / n;\n"
        "  chans[i*4+2] = mean / n;\n"
        "  chans[i*4+3] = abs(sx);\n"
        "}\n");
    return 1;
}

static int henon(const Experiment *e, char *out, size_t outsz, size_t *pos)
{
    (void)e;
    header(e, out, outsz, pos, 0);
    app(out, outsz, pos,
        "  float x = cells[i*16+0];\n"
        "  float y = cells[i*16+1];\n"
        "  float a = cells[i*16+4];\n"
        "  float b = cells[i*16+5];\n"
        "  for (int k=0; k<uSteps; ++k) {\n"
        "    float nx = 1.0 - a*x*x + y;\n"
        "    float ny = b*x;\n"
        "    if (isnan(nx) || isnan(ny) || isinf(nx) || isinf(ny) ||\n"
        "        abs(nx) > 1e6 || abs(ny) > 1e6) { nx = 0.0; ny = 0.0; }\n"
        "    x = nx; y = ny;\n"
        "  }\n"
        "  cells[i*16+0] = x; cells[i*16+1] = y;\n"
        "  chans[i*4+0] = x;\n"
        "  chans[i*4+1] = y;\n"
        "  chans[i*4+2] = sqrt(x*x + y*y);\n"
        "  chans[i*4+3] = abs(x);\n"
        "}\n");
    return 1;
}

static int dp(const Experiment *e, char *out, size_t outsz, size_t *pos)
{
    (void)e;
    header(e, out, outsz, pos, 1);
    app(out, outsz, pos,
        "  float t1 = cells[i*16+0];\n"
        "  float t2 = cells[i*16+1];\n"
        "  float w1 = cells[i*16+2];\n"
        "  float w2 = cells[i*16+3];\n"
        "  float m1 = cells[i*16+8];\n"
        "  float m2 = cells[i*16+9];\n"
        "  float l1 = cells[i*16+10];\n"
        "  float l2 = cells[i*16+11];\n"
        "  float g  = cells[i*16+12];\n"
        "  float damp = cells[i*16+13];\n"
        "  for (int k=0; k<uSteps; ++k) {\n"
        "    float d = t1 - t2; float sd = sin(d); float cd = cos(d);\n"
        "    float den = m1 + m2*sd*sd;\n"
        "    float o1 = (m2*g*sin(t2)*cd - m2*sd*(l1*w1*w1*cd + l2*w2*w2)\n"
        "               - (m1+m2)*g*sin(t1)) / (l1*den) - damp*w1;\n"
        "    float o2 = ((m1+m2)*(l1*w1*w1*sd - g*sin(t2) + g*sin(t1)*cd)\n"
        "               + m2*l2*w2*w2*sd*cd) / (l2*den) - damp*w2;\n"
        "    float k1t1 = w1, k1t2 = w2, k1w1 = o1, k1w2 = o2;\n"
        "    float h = uDt;\n"
        "    float s1=t1+0.5*h*k1t1, s2=t2+0.5*h*k1t2, v1=w1+0.5*h*k1w1, v2=w2+0.5*h*k1w2;\n"
        "    d=s1-s2; sd=sin(d); cd=cos(d); den=m1+m2*sd*sd;\n"
        "    float q1=(m2*g*sin(s2)*cd - m2*sd*(l1*v1*v1*cd + l2*v2*v2) - (m1+m2)*g*sin(s1))/(l1*den)-damp*v1;\n"
        "    float q2=((m1+m2)*(l1*v1*v1*sd - g*sin(s2) + g*sin(s1)*cd) + m2*l2*v2*v2*sd*cd)/(l2*den)-damp*v2;\n"
        "    float k2t1=v1,k2t2=v2,k2w1=q1,k2w2=q2;\n"
        "    s1=t1+0.5*h*k2t1; s2=t2+0.5*h*k2t2; v1=w1+0.5*h*k2w1; v2=w2+0.5*h*k2w2;\n"
        "    d=s1-s2; sd=sin(d); cd=cos(d); den=m1+m2*sd*sd;\n"
        "    q1=(m2*g*sin(s2)*cd - m2*sd*(l1*v1*v1*cd + l2*v2*v2) - (m1+m2)*g*sin(s1))/(l1*den)-damp*v1;\n"
        "    q2=((m1+m2)*(l1*v1*v1*sd - g*sin(s2) + g*sin(s1)*cd) + m2*l2*v2*v2*sd*cd)/(l2*den)-damp*v2;\n"
        "    float k3t1=v1,k3t2=v2,k3w1=q1,k3w2=q2;\n"
        "    s1=t1+h*k3t1; s2=t2+h*k3t2; v1=w1+h*k3w1; v2=w2+h*k3w2;\n"
        "    d=s1-s2; sd=sin(d); cd=cos(d); den=m1+m2*sd*sd;\n"
        "    q1=(m2*g*sin(s2)*cd - m2*sd*(l1*v1*v1*cd + l2*v2*v2) - (m1+m2)*g*sin(s1))/(l1*den)-damp*v1;\n"
        "    q2=((m1+m2)*(l1*v1*v1*sd - g*sin(s2) + g*sin(s1)*cd) + m2*l2*v2*v2*sd*cd)/(l2*den)-damp*v2;\n"
        "    float k4t1=v1,k4t2=v2,k4w1=q1,k4w2=q2;\n"
        "    t1 += (h/6.0)*(k1t1+2.0*k2t1+2.0*k3t1+k4t1);\n"
        "    t2 += (h/6.0)*(k1t2+2.0*k2t2+2.0*k3t2+k4t2);\n"
        "    w1 += (h/6.0)*(k1w1+2.0*k2w1+2.0*k3w1+k4w1);\n"
        "    w2 += (h/6.0)*(k1w2+2.0*k2w2+2.0*k3w2+k4w2);\n"
        "    t1 = norm_angle(t1); t2 = norm_angle(t2);\n"
        "    if (isnan(t1)||isnan(t2)||isnan(w1)||isnan(w2)||\n"
        "        isinf(t1)||isinf(t2)||isinf(w1)||isinf(w2)) {\n"
        "      t1=t2=w1=w2=0.0;\n"
        "    }\n"
        "  }\n"
        "  cells[i*16+0]=t1; cells[i*16+1]=t2; cells[i*16+2]=w1; cells[i*16+3]=w2;\n"
        "  float ke = 0.5*(m1+m2)*l1*l1*w1*w1 + 0.5*m2*l2*l2*w2*w2 + m2*l1*l2*w1*w2*cos(t1-t2);\n"
        "  float pe = -(m1+m2)*g*l1*cos(t1) - m2*g*l2*cos(t2);\n"
        "  chans[i*4+0]=t1; chans[i*4+1]=t2; chans[i*4+2]=ke+pe;\n"
        "  chans[i*4+3]=max(abs(w1),abs(w2));\n"
        "}\n");
    return 1;
}

int model_builtin_glsl(const Experiment *e, char *out, size_t outsz,
                       char *err, size_t errsz)
{
    size_t pos = 0;
    int ok = 0;
    if (err && errsz) err[0] = 0;
    if (!e || !out || outsz == 0) return 0;
    if (strcmp(e->model, "logistic") == 0) ok = logistic(e, out, outsz, &pos);
    else if (strcmp(e->model, "henon") == 0) ok = henon(e, out, outsz, &pos);
    else if (strcmp(e->model, "double_pendulum") == 0) ok = dp(e, out, outsz, &pos);
    else {
        if (err) snprintf(err, errsz, "no built-in GPU shader for model %s", e->model);
        return 0;
    }
    if (!ok || pos >= outsz) {
        if (err) snprintf(err, errsz, "GPU shader too long");
        return 0;
    }
    return 1;
}